#include "Common/File/FileUtil.h"
#include "UI/RetroHost.h"
#include "UnitTest.h"

bool TestRetroHost() {
	RetroHost &host = RetroHost::Instance();
	const auto nintendoCores = RetroHost::CoreCandidates(RETRO_NINTENDO);
	const auto ps2Cores = RetroHost::CoreCandidates(RETRO_PS2);
	EXPECT_EQ_INT(nintendoCores.size(), 4);
	EXPECT_EQ_INT(ps2Cores.size(), 2);
	EXPECT_TRUE(RetroHost::CoreCandidates(RETRO_PSP).empty());

	const Path coreDir = UnitTestTempPath("retrohostcores") / "cores";
	EXPECT_TRUE(File::CreateFullPath(coreDir / "placeholder"));
	for (const auto &core : nintendoCores)
		EXPECT_TRUE(File::CreateEmptyFile(coreDir / core.file));
	for (const auto &core : ps2Cores)
		EXPECT_TRUE(File::CreateEmptyFile(coreDir / core.file));
	host.SetNativeLibraryDirectory(coreDir.ToString());
	EXPECT_EQ_STR(RetroHost::ResolveCoreForContent(RETRO_NINTENDO, "Test.GBC", "sameboy_libretro_android.so"),
		(coreDir / "sameboy_libretro_android.so").ToString());
	EXPECT_EQ_STR(RetroHost::ResolveCoreForContent(RETRO_NINTENDO, "Test.gba", "sameboy_libretro_android.so"),
		(coreDir / "mgba_libretro_android.so").ToString());
	EXPECT_EQ_STR(RetroHost::ResolveCoreForContent(RETRO_NINTENDO, "Test.nds"),
		(coreDir / "melonds_libretro_android.so").ToString());
	EXPECT_TRUE(RetroHost::ResolveCoreForContent(RETRO_NINTENDO, "Test.zip").empty());
	EXPECT_TRUE(host.IsCoreAvailable(RETRO_NINTENDO, "sameboy_libretro_android.so"));
	EXPECT_EQ_STR(RetroHost::ResolveCoreForContent(RETRO_PS2, "Test.iso"), (coreDir / "armsx2_libretro.so").ToString());
	EXPECT_EQ_STR(RetroHost::ResolveCoreForContent(RETRO_PS2, "Test.cue"), (coreDir / "play_libretro_android.so").ToString());
	EXPECT_TRUE(RetroHost::ResolveCoreForContent(RETRO_PS2, "Test.pbp").empty());
	EXPECT_TRUE(RetroHost::ResolveCoreForContent(RETRO_PS2, "Test.7z").empty());
	EXPECT_TRUE(File::CreateEmptyFile(coreDir / "Test.bin"));
	EXPECT_TRUE(File::CreateEmptyFile(coreDir / "Test.cue"));
	EXPECT_EQ_STR(RetroHost::ResolveCoreForContent(RETRO_PS2, (coreDir / "Test.bin").ToString()),
		(coreDir / "play_libretro_android.so").ToString());
	File::Delete(coreDir / "play_libretro_android.so", true);
	EXPECT_FALSE(host.IsCoreAvailable(RETRO_PS2, "play_libretro_android.so"));
	EXPECT_EQ_STR(RetroHost::ResolveCoreForContent(RETRO_PS2, "Test.cue"),
		(coreDir / "armsx2_libretro.so").ToString());
	EXPECT_EQ_STR(RetroHost::ResolveCoreForContent(RETRO_PS2, (coreDir / "Test.bin").ToString()),
		(coreDir / "armsx2_libretro.so").ToString());

	EXPECT_TRUE(host.SetControllerKeyMapping(0, 500));
	EXPECT_TRUE(host.SetControllerKeyState(500, true));
	EXPECT_EQ_INT(host.ControllerState(0), 1);
	EXPECT_TRUE(host.SetControllerKeyState(500, false));
	EXPECT_EQ_INT(host.ControllerState(0), 0);
	EXPECT_FALSE(host.SetControllerKeyMapping(16, 500));
	EXPECT_EQ_INT(host.ControllerKeyMapping(16), -1);
	EXPECT_TRUE(host.SetControllerKeyMapping(0, 97));

	EXPECT_EQ_INT(RetroHost::ConvertAudioSample(-32768), -2147483647 - 1);
	EXPECT_EQ_INT(RetroHost::ConvertAudioSample(-1), -65536);
	EXPECT_EQ_INT(RetroHost::ConvertAudioSample(0), 0);
	EXPECT_EQ_INT(RetroHost::ConvertAudioSample(32767), 2147418112);

	const std::string statePath = RetroHost::StateFilePath("/save", "core.so", "rom.gba", 0);
	EXPECT_TRUE(!statePath.empty());
	EXPECT_TRUE(statePath != RetroHost::StateFilePath("/save", "core.so", "other.gba", 0));
	EXPECT_TRUE(statePath != RetroHost::StateFilePath("/save", "core.so", "rom.gba", 1));
	EXPECT_TRUE(RetroHost::StateFilePath("/save", "core.so", "rom.gba", 10).empty());

	for (const auto &core : nintendoCores)
		File::Delete(coreDir / core.file, true);
	for (const auto &core : ps2Cores)
		File::Delete(coreDir / core.file, true);
	File::Delete(coreDir / "Test.bin", true);
	File::Delete(coreDir / "Test.cue", true);
	File::DeleteDir(coreDir / "placeholder");
	File::DeleteDir(coreDir);
	host.SetNativeLibraryDirectory("");
	return true;
}
