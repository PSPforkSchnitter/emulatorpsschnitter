#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

enum RetroCoreCategory : int {
	RETRO_PSP = 0,
	RETRO_PS2 = 1,
	RETRO_NINTENDO = 2,
	RETRO_PS3 = 3,
};
class RetroHost {
public:
	static RetroHost &Instance();

	void SetNativeLibraryDirectory(std::string_view directory);
	void SetExternalCoreDirectory(std::string_view directory);
	bool LoadCore(RetroCoreCategory category, const std::string &corePath);
	bool LoadContent(const std::string &corePath, const std::string &contentPath);
	bool LoadContent(RetroCoreCategory category, const std::string &corePath, const std::string &contentPath);
	void RunFrame();
	void Shutdown();
	void SetControllerState(unsigned id, int16_t value);
	int16_t ControllerState(unsigned id) const;
	bool SetControllerKeyMapping(unsigned id, int keyCode);
	int ControllerKeyMapping(unsigned id) const;
	bool SetControllerKeyState(int keyCode, bool pressed);
	bool CanSaveState() const;
	bool SaveStateToSlot(unsigned slot);
	bool LoadStateFromSlot(unsigned slot);

	bool IsLoaded() const;
	std::string LastError() const;
	bool GetFrame(std::vector<uint32_t> *pixels, unsigned *width, unsigned *height);
	static int32_t ConvertAudioSample(int16_t sample);
	static std::string StateFilePath(const std::string &saveDirectory, const std::string &corePath, const std::string &contentPath, unsigned slot);

	struct CoreCandidate {
		std::string name;
		std::string file;
		std::string description;
	};
	static std::vector<CoreCandidate> CoreCandidates(RetroCoreCategory category);
	static std::string CoreForCategory(RetroCoreCategory category);
	std::string ResolveCorePath(RetroCoreCategory category) const;
	static std::string ResolveCoreForContent(RetroCoreCategory category, const std::string &contentPath, const std::string &preferredCore = {});
	bool IsCoreAvailable(RetroCoreCategory category, const std::string &coreFile) const;
	static bool ValidatePs2BiosDirectory(const std::string &systemDirectory, std::string *description = nullptr);

private:
	RetroHost() = default;

	void *handle_ = nullptr;
	void (*retro_init_)(void) = nullptr;
	void (*retro_deinit_)(void) = nullptr;
	bool (*retro_load_game_)(const void *) = nullptr;
	void (*retro_unload_game_)(void) = nullptr;
	void (*retro_run_)(void) = nullptr;
	size_t (*retro_serialize_size_)(void) = nullptr;
	bool (*retro_serialize_)(void *, size_t) = nullptr;
	bool (*retro_unserialize_)(const void *, size_t) = nullptr;
	void (*retro_set_controller_port_device_)(unsigned, unsigned) = nullptr;
	bool loaded_ = false;
	bool contentLoaded_ = false;

	std::string nativeLibDir_;
	std::string externalCoreDir_;
	std::string loadedCorePath_;
	std::string loadedContentPath_;
	std::string lastError_;
	mutable std::mutex mutex_;
	mutable std::recursive_mutex coreMutex_;
	mutable std::mutex inputMutex_;
	std::array<std::atomic<int16_t>, 16> controllerState_{};
	std::array<int, 16> controllerKeyMapping_ = {
		97, 100, 109, 108, 19, 20, 21, 22, 96, 99, 102, 103,
		104, 105, 106, 107,
	};
	std::vector<uint32_t> frame_;
	unsigned frameWidth_ = 0;
	unsigned frameHeight_ = 0;
};
