#pragma once

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

	bool IsLoaded() const { return loaded_; }
	std::string LastError() const;
	bool GetFrame(std::vector<uint32_t> *pixels, unsigned *width, unsigned *height);

	struct CoreCandidate {
		std::string name;
		std::string file;
		std::string description;
	};
	static std::vector<CoreCandidate> CoreCandidates(RetroCoreCategory category);
	static std::string CoreForCategory(RetroCoreCategory category);
	std::string ResolveCorePath(RetroCoreCategory category) const;
	static std::string ResolveCoreForContent(RetroCoreCategory category, const std::string &contentPath);
	static bool ValidatePs2BiosDirectory(const std::string &systemDirectory, std::string *description = nullptr);

private:
	RetroHost() = default;

	void *handle_ = nullptr;
	void (*retro_init_)(void) = nullptr;
	void (*retro_deinit_)(void) = nullptr;
	bool (*retro_load_game_)(const void *) = nullptr;
	void (*retro_unload_game_)(void) = nullptr;
	void (*retro_run_)(void) = nullptr;
	void (*retro_set_controller_port_device_)(unsigned, unsigned) = nullptr;
	bool loaded_ = false;

	std::string nativeLibDir_;
	std::string externalCoreDir_;
	std::string lastError_;
	mutable std::mutex mutex_;
	std::vector<uint32_t> frame_;
	unsigned frameWidth_ = 0;
	unsigned frameHeight_ = 0;
	unsigned pixelFormat_ = 1;
};
