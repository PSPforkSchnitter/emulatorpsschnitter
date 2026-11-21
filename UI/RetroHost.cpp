#include "UI/RetroHost.h"

#if defined(__ANDROID__)
#include <dlfcn.h>
#endif

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <dirent.h>
#include <mutex>
#include <vector>

#include "Common/Log.h"
#include "Common/File/FileUtil.h"
#include "Common/System/System.h"
#include "Core/Config.h"
#include "Core/System.h"

enum RetroPixelFormat : unsigned {
	RETRO_PIXEL_FORMAT_0RGB1555 = 0,
	RETRO_PIXEL_FORMAT_XRGB8888 = 1,
	RETRO_PIXEL_FORMAT_RGB565 = 2,
};

using retro_environment_t = bool (*)(unsigned, void *);
using retro_video_refresh_t = void (*)(const void *, unsigned, unsigned, size_t);
using retro_audio_sample_t = void (*)(int16_t, int16_t);
using retro_audio_sample_batch_t = size_t (*)(const int16_t *, size_t);
using retro_input_poll_t = void (*)(void);
using retro_input_state_t = int16_t (*)(unsigned, unsigned, unsigned, unsigned);

static RetroPixelFormat s_pixelFormat = RETRO_PIXEL_FORMAT_XRGB8888;
static bool s_ps2HwRenderRequested = false;
static bool s_ps2VulkanSupported = false;

struct RetroLogInterface {
	unsigned (*getLevel)(void);
	void (*log)(unsigned level, const char *fmt, ...);
};

static RetroHost::CoreCandidate MakeCandidate(const char *name, const char *file, const char *description) {
	return { name, file, description };
}

RetroHost &RetroHost::Instance() {
	static RetroHost host;
	return host;
}

void RetroHost::SetNativeLibraryDirectory(std::string_view directory) {
	std::lock_guard<std::mutex> lock(mutex_);
	nativeLibDir_ = directory;
}

void RetroHost::SetExternalCoreDirectory(std::string_view directory) {
	std::lock_guard<std::mutex> lock(mutex_);
	externalCoreDir_ = directory;
}

std::string RetroHost::LastError() const {
	std::lock_guard<std::mutex> lock(mutex_);
	return lastError_;
}

int32_t RetroHost::ConvertAudioSample(int16_t sample) {
	return static_cast<int32_t>(sample) * 65536;
}

std::string RetroHost::StateFilePath(const std::string &saveDirectory, const std::string &corePath, const std::string &contentPath, unsigned slot) {
	if (slot >= 10)
		return {};
	uint64_t hash = 14695981039346656037ull;
	auto addToHash = [&hash](const std::string &value) {
		for (unsigned char ch : value) {
			hash ^= ch;
			hash *= 1099511628211ull;
		}
		hash ^= 0xFF;
		hash *= 1099511628211ull;
	};
	addToHash(corePath);
	addToHash(contentPath);
	char filename[40];
	std::snprintf(filename, sizeof(filename), "%016llx.slot%u.state", static_cast<unsigned long long>(hash), slot);
	return saveDirectory + "/retro_states/" + filename;
}

bool RetroHost::CanSaveState() const {
	std::lock_guard<std::recursive_mutex> lock(coreMutex_);
	return contentLoaded_ && retro_serialize_size_ && retro_serialize_ && retro_unserialize_;
}

bool RetroHost::IsLoaded() const {
	std::lock_guard<std::recursive_mutex> lock(coreMutex_);
	return contentLoaded_;
}

std::string RetroHost::CoreForCategory(RetroCoreCategory category) {
	auto candidates = CoreCandidates(category);
	if (candidates.empty())
		return {};
	return candidates.front().file;
}

std::vector<RetroHost::CoreCandidate> RetroHost::CoreCandidates(RetroCoreCategory category) {
	switch (category) {
	case RETRO_PS2:
		return {
			MakeCandidate("ARMSX2", "armsx2_libretro.so", "PS2: .iso, .chd, .cso, .elf; fallback for .cue/.bin"),
			MakeCandidate("Play!", "play_libretro_android.so", "PS2: .cue, .iso, .chd, .cso (optional core)"),
		};
	case RETRO_NINTENDO:
		return {
			MakeCandidate("Gambatte", "gambatte_libretro_android.so", "Game Boy / Game Boy Color"),
			MakeCandidate("SameBoy", "sameboy_libretro_android.so", "Game Boy / Game Boy Color"),
			MakeCandidate("mGBA", "mgba_libretro_android.so", "Game Boy Advance"),
			MakeCandidate("melonDS", "melonds_libretro_android.so", "Nintendo DS"),
		};
	default:
		return {};
	}
}

std::string RetroHost::ResolveCoreForContent(RetroCoreCategory category, const std::string &contentPath, const std::string &preferredCore) {
	std::string lower = contentPath;
	for (char &ch : lower)
		ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
	const auto lastDot = lower.find_last_of('.');
	const std::string extension = lastDot == std::string::npos ? std::string() : lower.substr(lastDot);
	std::vector<std::string> candidates;
	if (category == RETRO_NINTENDO) {
		if (extension == ".nds" || extension == ".dsi")
			candidates = { "melonds_libretro_android.so" };
		else if (extension == ".gba")
			candidates = { "mgba_libretro_android.so" };
		else if (extension == ".gb" || extension == ".gbc" || extension == ".sgb")
			candidates = { "gambatte_libretro_android.so", "sameboy_libretro_android.so" };
	} else if (category == RETRO_PS2) {
		if (extension == ".cue" || extension == ".bin") {
			const Path cuePath = extension == ".bin" ? Path(contentPath).WithReplacedExtension(".cue") : Path(contentPath);
			if (extension == ".cue" || File::Exists(cuePath))
				candidates = { "play_libretro_android.so", "armsx2_libretro.so" };
		} else if (extension == ".iso" || extension == ".chd" || extension == ".cso") {
			candidates = { "armsx2_libretro.so", "play_libretro_android.so" };
		} else if (extension == ".elf") {
			candidates = { "armsx2_libretro.so" };
		}
	}
	if (candidates.empty())
		return {};

	if (!preferredCore.empty()) {
		auto preferred = std::find(candidates.begin(), candidates.end(), preferredCore);
		if (preferred != candidates.end())
			std::rotate(candidates.begin(), preferred, preferred + 1);
	}

	std::string nativeDir;
	{
		RetroHost &host = Instance();
		std::lock_guard<std::mutex> lock(host.mutex_);
		nativeDir = host.nativeLibDir_;
	}
	if (nativeDir.empty())
		return {};
	for (const std::string &candidate : candidates) {
		const std::string path = nativeDir + "/" + candidate;
		if (File::Exists(Path(path)))
			return path;
	}
	return {};
}

bool RetroHost::IsCoreAvailable(RetroCoreCategory category, const std::string &coreFile) const {
	std::lock_guard<std::mutex> lock(mutex_);
	if (nativeLibDir_.empty())
		return false;
	for (const auto &candidate : CoreCandidates(category)) {
		if (candidate.file == coreFile)
			return File::Exists(Path(nativeLibDir_ + "/" + candidate.file));
	}
	return false;
}

std::string RetroHost::ResolveCorePath(RetroCoreCategory category) const {
	std::lock_guard<std::mutex> lock(mutex_);
	for (const auto &candidate : CoreCandidates(category)) {
		const std::string nativePath = nativeLibDir_.empty()
			? candidate.file
			: nativeLibDir_ + "/" + candidate.file;
		if (!nativeLibDir_.empty()) {
			if (File::Exists(Path(nativePath)))
				return nativePath;
		}
	}
	return {};
}

static size_t AudioBatch(const int16_t *data, size_t frames) {
	if (!data)
		return frames;
	const size_t totalFrames = frames;
	constexpr size_t maxBatchFrames = 1024;
	std::array<int32_t, maxBatchFrames * 2> mixed;
	while (frames > 0) {
		const size_t batchFrames = std::min(frames, maxBatchFrames);
		for (size_t i = 0; i < batchFrames * 2; ++i)
			mixed[i] = RetroHost::ConvertAudioSample(data[i]);
		System_AudioPushSamples(mixed.data(), static_cast<int>(batchFrames), 1.0f);
		data += batchFrames * 2;
		frames -= batchFrames;
	}
	return totalFrames;
}

static void AudioSample(int16_t left, int16_t right) {
	const int32_t stereo[] = {
		RetroHost::ConvertAudioSample(left),
		RetroHost::ConvertAudioSample(right),
	};
	System_AudioPushSamples(stereo, 1, 1.0f);
}

static void InputPoll() {}

static int16_t InputState(unsigned port, unsigned device, unsigned index, unsigned id) {
	(void)port;
	(void)device;
	(void)index;
	if (port == 0 && device == 1 && id < 16)
		return RetroHost::Instance().ControllerState(id);
	return 0;
}

bool RetroHost::ValidatePs2BiosDirectory(const std::string &systemDirectory, std::string *description) {
	const std::string biosDir = systemDirectory + "/pcsx2/bios";
	DIR *dir = opendir(biosDir.c_str());
	if (!dir) {
		if (description)
			*description = "PS2 BIOS folder missing: " + biosDir;
		return false;
	}

	bool valid = false;
	std::string validName;
	std::string validRegion;
	dirent *entry;
	while ((entry = readdir(dir)) != nullptr) {
		const std::string name = entry->d_name;
		if (name.size() < 5)
			continue;
		const std::string lower = [&]() {
			std::string out = name;
			for (char &ch : out)
				ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
			return out;
		}();
		if (lower.size() < 4 || lower.substr(lower.size() - 4) != ".bin")
			continue;

		const std::string path = biosDir + "/" + name;
		std::FILE *fp = std::fopen(path.c_str(), "rb");
		if (!fp)
			continue;
		std::fseek(fp, 0, SEEK_END);
		const long fileSize = std::ftell(fp);
		std::rewind(fp);
		if (fileSize < 4 * 1024 * 1024 || fileSize > 8 * 1024 * 1024) {
			std::fclose(fp);
			continue;
		}

		bool foundRomVer = false;
		char romver[15] = {};
		long romDirOffset = 0;
		for (long i = 0; i < 512 * 1024; i += 16) {
			std::fseek(fp, i, SEEK_SET);
			char entryName[11] = {};
			std::uint16_t extInfoSize = 0;
			std::uint32_t romSize = 0;
			if (std::fread(entryName, 10, 1, fp) != 1)
				break;
			entryName[10] = '\0';
			if (std::fread(&extInfoSize, sizeof(extInfoSize), 1, fp) != 1)
				break;
			if (std::fread(&romSize, sizeof(romSize), 1, fp) != 1)
				break;
			if (std::strncmp(entryName, "RESET", 5) == 0)
				break;
			if (std::strncmp(entryName, "ROMVER", 6) == 0) {
				std::fseek(fp, romDirOffset, SEEK_SET);
				if (std::fread(romver, 14, 1, fp) == 1) {
					romver[14] = '\0';
					foundRomVer = true;
				}
			}
			romDirOffset += (romSize % 16) == 0 ? romSize : (romSize + 16) & ~15u;
		}
		std::fclose(fp);

		if (foundRomVer && romver[4] != '\0') {
			const char region = romver[4];
			validRegion = region == 'J' ? "Japan" :
				region == 'A' ? "USA" :
				region == 'E' ? "Europe" :
				region == 'H' ? "Asia" :
				region == 'C' ? "China" :
				region == 'T' ? "T10K/Arcade" :
				region == 'X' ? "Test" : "Region " + std::string(1, region);
			if (validRegion != "T10K/Arcade") {
				valid = true;
				validName = name;
				break;
			}
		}
	}
	closedir(dir);

	if (valid) {
		if (description)
			*description = validName + " (" + validRegion + ")";
		return true;
	}
	if (description)
		*description = "No valid PS2 BIOS found in " + biosDir;
	return false;
}

static bool Environment(unsigned cmd, void *data) {
	RetroHost &host = RetroHost::Instance();
	switch (cmd) {
	case 2: { // RETRO_ENVIRONMENT_GET_CAN_DUPE
		bool *canDupe = static_cast<bool *>(data);
		if (canDupe)
			*canDupe = true;
		return true;
	}
	case 9: { // RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY
		const char **dir = static_cast<const char **>(data);
		static std::string systemDir;
		systemDir = GetSysDirectory(DIRECTORY_PSP).ToString() + "/SYSTEM";
		if (dir)
			*dir = systemDir.c_str();
		return true;
	}
	case 14: { // RETRO_ENVIRONMENT_SET_HW_RENDER
		s_ps2HwRenderRequested = true;
		s_ps2VulkanSupported = false;
		return false;
	}
	case 15: { // RETRO_ENVIRONMENT_GET_VARIABLE
		struct Variable { const char *key; const char *value; };
		auto *variable = static_cast<Variable *>(data);
		if (!variable)
			return false;
		if (std::strcmp(variable->key, "armsx2_bios") == 0) {
			variable->value = "auto";
			return true;
		}
		variable->value = "disabled";
		return true;
	}
	case 10: { // RETRO_ENVIRONMENT_SET_PIXEL_FORMAT
		unsigned *format = static_cast<unsigned *>(data);
		if (format)
			s_pixelFormat = static_cast<RetroPixelFormat>(*format);
		return true;
	}
	case 16: // RETRO_ENVIRONMENT_SET_VARIABLES
		return true;
	case 31: // RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY
	case 23: { // RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY (legacy alias)
		const char **dir = static_cast<const char **>(data);
		static std::string systemDir;
		systemDir = GetSysDirectory(DIRECTORY_PSP).ToString() + "/SYSTEM/save";
		File::CreateFullPath(Path(systemDir));
		if (dir)
			*dir = systemDir.c_str();
		return true;
	}
	case 26: // RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL
		return true;
	case 27: // RETRO_ENVIRONMENT_SET_LOG_INTERFACE
		return true;
	case 29: // RETRO_ENVIRONMENT_SET_GEOMETRY
		return true;
	case 32: // RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO
		return true;
	case 67: // RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2
	case 68: // RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL
		return true;
	case 52: { // RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION
		unsigned *version = static_cast<unsigned *>(data);
		if (version)
			*version = 2;
		return true;
	}
	case 18: // RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME
		return true;
	default:
		return false;
	}
}

bool RetroHost::LoadCore(RetroCoreCategory category, const std::string &corePath) {
	std::lock_guard<std::recursive_mutex> coreLock(coreMutex_);
	std::lock_guard<std::mutex> lock(mutex_);
	Shutdown();
	lastError_.clear();
	s_ps2HwRenderRequested = false;
	s_ps2VulkanSupported = false;
#ifndef __ANDROID__
	lastError_ = "External libretro cores are supported on Android arm64-v8a";
	return false;
#else

	std::string path = corePath;
	if (path.empty()) {
		path = externalCoreDir_.empty() ? nativeLibDir_ + "/" + CoreForCategory(category) : externalCoreDir_;
	}
	handle_ = dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
	if (!handle_) {
		lastError_ = dlerror();
		return false;
	}

#define DLSYM(name, member) do { *(void **)(&(member)) = dlsym(handle_, name); } while (0)
	DLSYM("retro_init", retro_init_);
	DLSYM("retro_deinit", retro_deinit_);
	DLSYM("retro_load_game", retro_load_game_);
	DLSYM("retro_unload_game", retro_unload_game_);
	DLSYM("retro_run", retro_run_);
	DLSYM("retro_serialize_size", retro_serialize_size_);
	DLSYM("retro_serialize", retro_serialize_);
	DLSYM("retro_unserialize", retro_unserialize_);
	DLSYM("retro_set_controller_port_device", retro_set_controller_port_device_);
#undef DLSYM

	void (*setEnvironment)(retro_environment_t) = reinterpret_cast<void (*)(retro_environment_t)>(dlsym(handle_, "retro_set_environment"));
	void (*setVideo)(retro_video_refresh_t) = reinterpret_cast<void (*)(retro_video_refresh_t)>(dlsym(handle_, "retro_set_video_refresh"));
	void (*setAudio)(retro_audio_sample_t) = reinterpret_cast<void (*)(retro_audio_sample_t)>(dlsym(handle_, "retro_set_audio_sample"));
	void (*setAudioBatch)(retro_audio_sample_batch_t) = reinterpret_cast<void (*)(retro_audio_sample_batch_t)>(dlsym(handle_, "retro_set_audio_sample_batch"));
	void (*setInputPoll)(retro_input_poll_t) = reinterpret_cast<void (*)(retro_input_poll_t)>(dlsym(handle_, "retro_set_input_poll"));
	void (*setInputState)(retro_input_state_t) = reinterpret_cast<void (*)(retro_input_state_t)>(dlsym(handle_, "retro_set_input_state"));

	if (!retro_init_ || !retro_load_game_ || !retro_run_ || !setEnvironment || !setVideo || !setInputPoll || !setInputState) {
		lastError_ = "Core is missing required libretro symbols";
		dlclose(handle_);
		handle_ = nullptr;
		return false;
	}

	setEnvironment(Environment);
	setVideo([](const void *data, unsigned width, unsigned height, size_t pitch) {
		if (!data || !width || !height)
			return;
		RetroHost &host = RetroHost::Instance();
		std::vector<uint32_t> pixels(width * height);
		const uint8_t *src = static_cast<const uint8_t *>(data);
		for (unsigned y = 0; y < height; ++y) {
			const uint8_t *row = src + y * pitch;
			for (unsigned x = 0; x < width; ++x) {
				uint32_t rgba;
				if (s_pixelFormat == RETRO_PIXEL_FORMAT_RGB565) {
					uint16_t p = static_cast<uint16_t>(row[x * 2] | (row[x * 2 + 1] << 8));
					uint32_t r = (p >> 11) & 31;
					uint32_t g = (p >> 5) & 63;
					uint32_t b = p & 31;
					rgba = 0xFF000000u | ((r * 255 / 31) << 16) | ((g * 255 / 63) << 8) | (b * 255 / 31);
				} else if (s_pixelFormat == RETRO_PIXEL_FORMAT_0RGB1555) {
					uint16_t p = static_cast<uint16_t>(row[x * 2] | (row[x * 2 + 1] << 8));
					uint32_t r = (p >> 10) & 31;
					uint32_t g = (p >> 5) & 31;
					uint32_t b = p & 31;
					rgba = 0xFF000000u | ((r * 255 / 31) << 16) | ((g * 255 / 31) << 8) | (b * 255 / 31);
				} else {
					rgba = 0xFF000000u | (row[x * 4] << 16) | (row[x * 4 + 1] << 8) | row[x * 4 + 2];
				}
				pixels[y * width + x] = rgba;
			}
		}
		std::lock_guard<std::mutex> frameLock(host.mutex_);
		host.frame_.swap(pixels);
		host.frameWidth_ = width;
		host.frameHeight_ = height;
	});
	if (setAudio)
		setAudio(AudioSample);
	if (setAudioBatch)
		setAudioBatch(AudioBatch);
	setInputPoll(InputPoll);
	setInputState(InputState);
	retro_init_();
	loaded_ = true;
	loadedCorePath_ = path;
	return true;
#endif
}

bool RetroHost::LoadContent(const std::string &corePath, const std::string &contentPath) {
	return LoadContent(RETRO_PS2, corePath, contentPath);
}

bool RetroHost::LoadContent(RetroCoreCategory category, const std::string &corePath, const std::string &contentPath) {
	std::lock_guard<std::recursive_mutex> coreLock(coreMutex_);
	const std::string systemDir = GetSysDirectory(DIRECTORY_PSP).ToString() + "/SYSTEM";
	if (category == RETRO_PS2 && Path(corePath).GetFilename() != "play_libretro_android.so") {
		std::string biosDescription;
		if (!ValidatePs2BiosDirectory(systemDir, &biosDescription)) {
			lastError_ = biosDescription;
			return false;
		}
	}
	if (!LoadCore(category, corePath))
		return false;
	struct RetroGameInfo {
		const char *path;
		const void *data;
		size_t size;
		const char *meta;
	};
	RetroGameInfo info{};
	info.path = contentPath.c_str();
	if (!retro_load_game_(&info)) {
		lastError_ = "Libretro core rejected the content";
		Shutdown();
		return false;
	}
	if (s_ps2HwRenderRequested && !s_ps2VulkanSupported) {
		lastError_ = "ARMSX2 requires Vulkan >= 1.1, but this frontend cannot create a libretro Vulkan HW-render context yet.";
		if (retro_unload_game_)
			retro_unload_game_();
		return false;
	}
	loadedContentPath_ = contentPath;
	contentLoaded_ = true;
	return true;
}

void RetroHost::RunFrame() {
	std::lock_guard<std::recursive_mutex> lock(coreMutex_);
	if (contentLoaded_ && retro_run_)
		retro_run_();
}

void RetroHost::SetControllerState(unsigned id, int16_t value) {
	if (id < 16)
		controllerState_[id].store(value, std::memory_order_relaxed);
}

int16_t RetroHost::ControllerState(unsigned id) const {
	if (id >= controllerState_.size())
		return 0;
	return controllerState_[id].load(std::memory_order_relaxed);
}

bool RetroHost::SetControllerKeyMapping(unsigned id, int keyCode) {
	if (id >= controllerKeyMapping_.size() || keyCode <= 0)
		return false;
	std::lock_guard<std::mutex> lock(inputMutex_);
	controllerKeyMapping_[id] = keyCode;
	controllerState_[id].store(0, std::memory_order_relaxed);
	return true;
}

int RetroHost::ControllerKeyMapping(unsigned id) const {
	if (id >= controllerKeyMapping_.size())
		return -1;
	std::lock_guard<std::mutex> lock(inputMutex_);
	return controllerKeyMapping_[id];
}

bool RetroHost::SetControllerKeyState(int keyCode, bool pressed) {
	std::lock_guard<std::mutex> lock(inputMutex_);
	bool mapped = false;
	for (size_t id = 0; id < controllerKeyMapping_.size(); ++id) {
		if (controllerKeyMapping_[id] == keyCode) {
			controllerState_[id].store(pressed ? 1 : 0, std::memory_order_relaxed);
			mapped = true;
		}
	}
	return mapped;
}

bool RetroHost::SaveStateToSlot(unsigned slot) {
	std::lock_guard<std::recursive_mutex> lock(coreMutex_);
	if (!CanSaveState()) {
		lastError_ = "This libretro core does not support save states or no game is loaded";
		return false;
	}
	const size_t stateSize = retro_serialize_size_();
	if (!stateSize || stateSize > 512ull * 1024 * 1024) {
		lastError_ = "Core returned an invalid save-state size";
		return false;
	}
	const std::string saveDirectory = GetSysDirectory(DIRECTORY_PSP).ToString() + "/SYSTEM/save";
	const std::string statePathString = StateFilePath(saveDirectory, Path(loadedCorePath_).GetFilename(), loadedContentPath_, slot);
	if (statePathString.empty()) {
		lastError_ = "Save-state slot must be between 1 and 10";
		return false;
	}
	if (!File::CreateFullPath(Path(saveDirectory + "/retro_states"))) {
		lastError_ = "Could not create the save-state directory";
		return false;
	}

	std::vector<uint8_t> state(stateSize);
	if (!retro_serialize_(state.data(), state.size())) {
		lastError_ = "The libretro core failed to serialize its state";
		return false;
	}

	Path tempPath(statePathString + ".tmp");
	File::IOFile file(tempPath, "wb");
	if (!file || !file.WriteBytes(state.data(), state.size()) || !file.Flush() || !file.Close()) {
		File::Delete(tempPath, true);
		lastError_ = "Could not write the save-state file";
		return false;
	}
	if (!File::Rename(tempPath, Path(statePathString))) {
		File::Delete(tempPath, true);
		lastError_ = "Could not finalize the save-state file";
		return false;
	}
	lastError_.clear();
	return true;
}

bool RetroHost::LoadStateFromSlot(unsigned slot) {
	std::lock_guard<std::recursive_mutex> lock(coreMutex_);
	if (!CanSaveState()) {
		lastError_ = "This libretro core does not support save states or no game is loaded";
		return false;
	}
	const std::string saveDirectory = GetSysDirectory(DIRECTORY_PSP).ToString() + "/SYSTEM/save";
	const std::string statePathString = StateFilePath(saveDirectory, Path(loadedCorePath_).GetFilename(), loadedContentPath_, slot);
	if (statePathString.empty()) {
		lastError_ = "Save-state slot must be between 1 and 10";
		return false;
	}

	File::IOFile file(Path(statePathString), "rb");
	if (!file) {
		lastError_ = "Save-state slot is empty";
		return false;
	}
	const size_t stateSize = retro_serialize_size_();
	if (!stateSize || stateSize > 512ull * 1024 * 1024 || file.GetSize() != stateSize) {
		lastError_ = "Save-state size does not match the loaded core";
		return false;
	}
	std::vector<uint8_t> state(stateSize);
	if (!file.ReadBytes(state.data(), state.size()) || !file.Close()) {
		lastError_ = "Could not read the save-state file";
		return false;
	}
	if (!retro_unserialize_(state.data(), state.size())) {
		lastError_ = "The libretro core rejected the save-state";
		return false;
	}
	lastError_.clear();
	return true;
}

void RetroHost::Shutdown() {
	std::lock_guard<std::recursive_mutex> lock(coreMutex_);
	if (contentLoaded_ && retro_unload_game_)
		retro_unload_game_();
	if (loaded_ && retro_deinit_)
		retro_deinit_();
#ifdef __ANDROID__
	if (handle_)
		dlclose(handle_);
#endif
	handle_ = nullptr;
	retro_init_ = nullptr;
	retro_deinit_ = nullptr;
	retro_load_game_ = nullptr;
	retro_unload_game_ = nullptr;
	retro_run_ = nullptr;
	retro_serialize_size_ = nullptr;
	retro_serialize_ = nullptr;
	retro_unserialize_ = nullptr;
	retro_set_controller_port_device_ = nullptr;
	loaded_ = false;
	contentLoaded_ = false;
	for (auto &state : controllerState_)
		state.store(0, std::memory_order_relaxed);
	loadedCorePath_.clear();
	loadedContentPath_.clear();
}

bool RetroHost::GetFrame(std::vector<uint32_t> *pixels, unsigned *width, unsigned *height) {
	std::lock_guard<std::mutex> lock(mutex_);
	if (pixels)
		*pixels = frame_;
	if (width)
		*width = frameWidth_;
	if (height)
		*height = frameHeight_;
	return !frame_.empty();
}
