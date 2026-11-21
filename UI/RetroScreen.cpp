#include "UI/RetroScreen.h"

#include <algorithm>
#include <cmath>
#include <dirent.h>
#include <cctype>

#include "Common/Data/Format/IniFile.h"
#include "Common/File/FileUtil.h"
#include "Common/System/Request.h"
#include "Common/System/System.h"
#include "Common/TimeUtil.h"
#include "Common/UI/Root.h"
#include "Common/UI/Context.h"
#include "Common/UI/ScrollView.h"
#include "Common/UI/ViewGroup.h"
#include "Common/Input/KeyCodes.h"

#include "Core/Config.h"
#include "Core/System.h"
#include "UI/RetroHost.h"

RetroScreen::RetroScreen() {
	RefreshStatus();
}

static const char *Categories[] = {
	"HOME",
	"PSP",
	"PS2",
	"LIBRARY",
	"SYSTEM",
};

void RetroScreen::RefreshStatus() {
	IniFile ini;
	const Path iniPath = g_Config.IniFilename();
	if (ini.Load(iniPath)) {
		const Section *section = ini.GetSection("PS2");
		if (section) {
			section->Get("BIOSPath", &ps2BiosPath_);
			section->Get("BIOSRegion", &ps2BiosRegion_);
		}
	}
	const std::string systemDir = GetSysDirectory(DIRECTORY_PSP).ToString() + "/SYSTEM";
	std::string description;
	if (RetroHost::ValidatePs2BiosDirectory(systemDir, &description))
		status_ = "PS2 BIOS: " + description;
	else
		status_ = "PS2 BIOS: NOT VALID — " + description;
}

void RetroScreen::CreateViews() {
	using namespace UI;

	categoryOffset_ = entrance_ = 0.0f;
	root_ = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));

	LinearLayout *header = root_->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(18, 12, 18, 8))));
	header->SetSpacing(10.0f);
	header->Add(new TextView("PSP5", ALIGN_LEFT | FLAG_WRAP_TEXT, true, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT)));
	TextView *edition = header->Add(new TextView("  // CONSOLE HUB", ALIGN_LEFT, false, new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT)));
	edition->SetTextColor(0xFF78AFFF);
	header->Add(new TextView("ARM64  |  ONLINE", ALIGN_LEFT, true, new LinearLayoutParams(1.0f, Gravity::G_VCENTER)));

	LinearLayout *top = root_->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(18, 0, 18, 10))));
	top->SetSpacing(8.0f);

	for (int i = 0; i < 5; ++i) {
		Choice *choice = top->Add(new Choice(Categories[i], new LinearLayoutParams(i == 0 ? 1.2f : 1.0f, WRAP_CONTENT)));
		choice->SetTag(std::to_string(i));
		choice->OnClick.Add([this, i](UI::EventParams &) {
			category_ = i;
			item_ = 0;
			UI::PlayUISound(UI::UISound::SELECT);
			RecreateViews();
		});
	}

	ScrollView *scroll = root_->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, Margins(18, 0, 18, 18))));
	LinearLayout *body = scroll->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	body->SetSpacing(10.0f);

	if (category_ == 0) {
		LinearLayout *hero = body->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(0))));
		hero->SetSpacing(4.0f);
		hero->SetBG(UI::Drawable(0xE01A2035));
		hero->Add(new TextView("PORTABLE PLAY", ALIGN_LEFT, true, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(18, 16, 18, 0))));
		TextView *pspTitle = hero->Add(new TextView("PSP", ALIGN_LEFT, true, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(18, 0, 18, 0))));
		pspTitle->SetTextColor(0xFF59A6FF);
		hero->Add(new TextView("Native PPSSPP core  |  ARM64 optimized", ALIGN_LEFT, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(18, 0, 18, 14))));
		Choice *browse = hero->Add(new Choice("OPEN PSP GAMES", new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT, Margins(18, 0, 18, 18))));
		browse->OnClick.Add([this](UI::EventParams &) {
			System_BrowseForFile(GetRequesterToken(), "Load PSP game", BrowseFileType::BOOTABLE,
				[](std::string_view value, int) { System_PostUIMessage(UIMessage::REQUEST_GAME_BOOT, value); });
		});

		LinearLayout *cards = body->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
		cards->SetSpacing(8.0f);

		LinearLayout *coreCard = cards->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(1.0f, WRAP_CONTENT)));
		coreCard->SetSpacing(3.0f);
		coreCard->SetBG(UI::Drawable(0xDE11172A));
		coreCard->Add(new TextView("CORE", ALIGN_LEFT, true, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(12, 10, 12, 0))));
		coreCard->Add(new TextView("Native PPSSPP", ALIGN_LEFT, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(12, 0, 12, 10))));

		LinearLayout *speedCard = cards->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(1.0f, WRAP_CONTENT)));
		speedCard->SetSpacing(3.0f);
		speedCard->SetBG(UI::Drawable(0xDE11172A));
		speedCard->Add(new TextView("PLATFORM", ALIGN_LEFT, true, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(12, 10, 12, 0))));
		speedCard->Add(new TextView("Android arm64-v8a", ALIGN_LEFT, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(12, 0, 12, 10))));

		LinearLayout *modeCard = cards->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(1.0f, WRAP_CONTENT)));
		modeCard->SetSpacing(3.0f);
		modeCard->SetBG(UI::Drawable(0xDE11172A));
		modeCard->Add(new TextView("MODE", ALIGN_LEFT, true, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(12, 10, 12, 0))));
		modeCard->Add(new TextView("Console-first UI", ALIGN_LEFT, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(12, 0, 12, 10))));

		body->Add(new Choice("CUSTOM PSP LIBRARY FOLDER"))->OnClick.Add([this](UI::EventParams &) {
			BrowsePspFolder();
		});
	} else if (category_ == 1) {
		LinearLayout *psp = body->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
		psp->SetSpacing(4.0f);
		psp->SetBG(UI::Drawable(0xE0141D38));
		psp->Add(new TextView("PSP  //  NATIVE", ALIGN_LEFT, true, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(16, 14, 16, 2))));
		psp->Add(new TextView("One-click browser, native Vulkan/OpenGL rendering and ARM64 JIT.", ALIGN_LEFT | FLAG_WRAP_TEXT, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(16, 0, 16, 14))));
		psp->Add(new Choice("BROWSE & LOAD PSP GAME", new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT, Margins(16, 0, 16, 16))))->OnClick.Add([this](UI::EventParams &) {
			System_BrowseForFile(GetRequesterToken(), "Load PSP game", BrowseFileType::BOOTABLE,
				[](std::string_view value, int) { System_PostUIMessage(UIMessage::REQUEST_GAME_BOOT, value); });
		});
		body->Add(new Choice("SET CUSTOM PSP LIBRARY FOLDER"))->OnClick.Add([this](UI::EventParams &) { BrowsePspFolder(); });
	} else if (category_ == 2) {
		body->Add(new TextView(status_, ALIGN_LEFT | FLAG_WRAP_TEXT, false));
		body->Add(new TextView("ARMSX2 BIOS: system/pcsx2/bios", ALIGN_LEFT, false));
		body->Add(new TextView("ARMSX2 requires Vulkan >= 1.1 and a valid PS2 BIOS.", ALIGN_LEFT | FLAG_WRAP_TEXT, false));
		body->Add(new Choice("CHOOSE PS2 BIOS"))->OnClick.Add([this](UI::EventParams &) { BrowseBios(); });
		body->Add(new Choice("CHOOSE ARMSX2 CORE"))->OnClick.Add([this](UI::EventParams &) { BrowseCustomCore(); });
		body->Add(new Choice("LOAD PS2 GAME"))->OnClick.Add([this](UI::EventParams &) { BrowsePs2Game(); });
	} else if (category_ == 3) {
		body->Add(new TextView("NINTENDO ARM64 CORES", ALIGN_LEFT, true));
		body->Add(new Choice("SCAN NINTENDO ROM FOLDER"))->OnClick.Add([this](UI::EventParams &) { BrowseNintendoFolder(); });
		body->Add(new Choice("QUICK LOAD GB/GBC/GBA/NDS ROM"))->OnClick.Add([this](UI::EventParams &) { BrowseNintendoGame(); });
		body->Add(new TextView("PS3", ALIGN_LEFT, true));
		body->Add(new TextView("No stable open ARM64 libretro core is currently available.", ALIGN_LEFT | FLAG_WRAP_TEXT, false));
		body->Add(new Choice("SCAN PS3 IMAGE"))->OnClick.Add([this](UI::EventParams &) { BrowsePs3Image(); });
	} else {
		body->Add(new TextView(status_, ALIGN_LEFT, false));
		body->Add(new TextView("Build: ARM64 Normal Debug", ALIGN_LEFT, false));
		body->Add(new TextView("PSP: Native PPSSPP  |  PS2: ARMSX2", ALIGN_LEFT, false));
		body->Add(new TextView("Nintendo: Gambatte, SameBoy, mGBA, melonDS", ALIGN_LEFT, false));
		body->Add(new Choice("STOP ACTIVE EMULATION"))->OnClick.Add([this](UI::EventParams &) {
			RetroHost::Instance().Shutdown();
			status_ = "Emulation stopped.";
			RecreateViews();
		});
		body->Add(new Choice("RE-SCAN SYSTEM STATUS"))->OnClick.Add([this](UI::EventParams &) {
			RefreshStatus();
			RecreateViews();
		});
	}
}

void RetroScreen::BrowseBios() {
	System_BrowseForFile(GetRequesterToken(), "Select PS2 BIOS", BrowseFileType::ANY,
		[this](std::string_view value, int) {
			std::string source(value);
			Path dest = GetSysDirectory(DIRECTORY_PSP) / "SYSTEM" / "pcsx2" / "bios";
			File::CreateFullPath(dest);
			Path sourcePath(source);
			Path target = dest / sourcePath.GetFilename();
			if (!source.empty() && File::Copy(sourcePath, target)) {
				const std::string sourceBase = source;
				const std::string targetBase = target.ToString();
				const std::string suffixes[] = { ".rom1", ".ROM1", ".rom2", ".ROM2" };
				for (const std::string &suffix : suffixes) {
					Path sibling(sourceBase + suffix);
					if (File::Exists(sibling))
						File::Copy(sibling, Path(targetBase + suffix));
				}
				const std::string targetNoBin = target.ToString();
				std::string stem = targetNoBin;
				if (stem.size() >= 4 && stem.substr(stem.size() - 4) == ".bin")
					stem.resize(stem.size() - 4);
				for (const char *extension : { "rom1", "ROM1", "rom2", "ROM2" }) {
					Path sibling(stem + "." + extension);
					if (File::Exists(sibling))
						continue;
					Path originalSource(sourceBase.size() >= 4 && sourceBase.substr(sourceBase.size() - 4) == ".bin"
						? sourceBase.substr(0, sourceBase.size() - 4) + "." + extension
						: sourceBase + "." + extension);
					if (File::Exists(originalSource))
						File::Copy(originalSource, sibling);
				}
				ps2BiosPath_ = target.ToString();
				const std::string systemDir = GetSysDirectory(DIRECTORY_PSP).ToString() + "/SYSTEM";
				std::string description;
				if (RetroHost::ValidatePs2BiosDirectory(systemDir, &description)) {
					ps2BiosRegion_ = description;
					IniFile ini;
					ini.Load(g_Config.IniFilename());
					Section *section = ini.GetOrCreateSection("PS2");
					section->Set("BIOSPath", ps2BiosPath_);
					section->Set("BIOSRegion", ps2BiosRegion_);
					ini.Save(g_Config.IniFilename());
					status_ = "PS2 BIOS: " + ps2BiosPath_ + " (" + ps2BiosRegion_ + ")";
					UI::PlayUISound(UI::UISound::CONFIRM);
				} else {
					status_ = description;
					ps2BiosPath_.clear();
					ps2BiosRegion_.clear();
				}
				RecreateViews();
			}
		});
}

void RetroScreen::BrowsePs2Game() {
	System_BrowseForFile(GetRequesterToken(), "Load PS2 game", BrowseFileType::BOOTABLE,
		[this](std::string_view value, int) {
			ps2GamePath_ = value;
			if (customCorePath_.empty()) {
				customCorePath_ = RetroHost::Instance().ResolveCorePath(RETRO_PS2);
			}
			const std::string systemDir = GetSysDirectory(DIRECTORY_PSP).ToString() + "/SYSTEM";
			File::CreateFullPath(Path(systemDir) / "pcsx2" / "bios");
			File::CreateFullPath(Path(systemDir) / "pcsx2" / "resources");
			if (customCorePath_.empty()) {
				status_ = "PS2 core not found. Attach armsx2_libretro.so.";
			} else if (!RetroHost::Instance().LoadContent(customCorePath_, ps2GamePath_)) {
				status_ = "PS2 load blocked: " + RetroHost::Instance().LastError();
			} else {
				status_ = "Loading PS2: " + ps2GamePath_;
			}
			RecreateViews();
		});
}

void RetroScreen::BrowseNintendoFolder() {
	System_BrowseForFolder(GetRequesterToken(), "Nintendo ROM folder", GetSysDirectory(DIRECTORY_PSP),
		[this](std::string_view value, int) {
			status_ = "Nintendo ROM folder: " + std::string(value);
			IniFile ini;
			ini.Load(g_Config.IniFilename());
			ini.GetOrCreateSection("Nintendo")->Set("ROMFolder", value);
			ini.Save(g_Config.IniFilename());
			RecreateViews();
		});
}

void RetroScreen::BrowseNintendoGame() {
	System_BrowseForFile(GetRequesterToken(), "Load Nintendo ROM", BrowseFileType::ANY,
		[this](std::string_view value, int) {
			nintendoRomPath_ = value;
			std::string corePath = RetroHost::ResolveCoreForContent(RETRO_NINTENDO, nintendoRomPath_);
			if (corePath.empty()) {
				status_ = "Unsupported ROM format. Use GB, GBC, GBA, NDS or DSI.";
			} else if (!RetroHost::Instance().LoadContent(RETRO_NINTENDO, corePath, nintendoRomPath_)) {
				status_ = "Nintendo load failed: " + RetroHost::Instance().LastError();
			} else {
				status_ = "Nintendo loading: " + nintendoRomPath_;
				UI::PlayUISound(UI::UISound::CONFIRM);
			}
			RecreateViews();
		});
}

void RetroScreen::BrowsePspFolder() {
	System_BrowseForFolder(GetRequesterToken(), "Choose PSP games folder", GetSysDirectory(DIRECTORY_PSP),
		[this](std::string_view value, int) {
			status_ = "PSP library: " + std::string(value);
			IniFile ini;
			ini.Load(g_Config.IniFilename());
			ini.GetOrCreateSection("PSP")->Set("LibraryFolder", value);
			ini.Save(g_Config.IniFilename());
			RecreateViews();
		});
}

void RetroScreen::BrowsePs3Image() {
	System_BrowseForFile(GetRequesterToken(), "Choose PS3 image", BrowseFileType::BOOTABLE,
		[this](std::string_view value, int) {
			status_ = "PS3 content selected, but ARM64 core support is unavailable.";
			RecreateViews();
		});
}

void RetroScreen::BrowseCustomCore() {
	System_BrowseForFile(GetRequesterToken(), "Select ARM64 libretro .so", BrowseFileType::ANY,
		[this](std::string_view value, int) {
			Path source(value);
			Path cores = GetSysDirectory(DIRECTORY_PSP) / "SYSTEM" / "cores";
			File::CreateFullPath(cores);
			Path target = cores / source.GetFilename();
			if (source.ToString().rfind("://", 0) == 0 || source.ToString().rfind("/", 0) == 0) {
				if (File::Copy(source, target))
					customCorePath_ = target.ToString();
				else
					customCorePath_ = source.ToString();
			} else {
				customCorePath_ = source.ToString();
			}
			status_ = "External core: " + customCorePath_;
			RecreateViews();
		});
}

void RetroScreen::LoadCoreCandidate() {
	customCorePath_.clear();
	status_ = "Attach an ARM64 Nintendo libretro core with the external core picker.";
	RecreateViews();
}

void RetroScreen::update() {
	UIScreen::update();
	double now = time_now_d();
	float dt = lastFrameTime_ == 0.0 ? 1.0f / 60.0f : std::clamp(float(now - lastFrameTime_), 0.0f, 0.05f);
	lastFrameTime_ = now;
	float ease = 1.0f - expf(-dt * 12.0f);
	categoryOffset_ += (float(category_) - categoryOffset_) * ease;
	itemOffset_ += (float(item_) - itemOffset_) * ease;
	entrance_ += (1.0f - entrance_) * ease;
	if (RetroHost::Instance().IsLoaded())
		RetroHost::Instance().RunFrame();
}

void RetroScreen::DrawBackground(UIContext &dc) {
	const Bounds b = dc.GetBounds();
	dc.FillRect(UI::Drawable(0xFF020718), b);
	dc.FillRect(UI::Drawable(0xFF07152D), Bounds(b.x, b.y, b.w, b.h * 0.58f));
	dc.FillRect(UI::Drawable(0x24185FA8), Bounds(b.x + b.w * 0.48f, b.y - b.h * 0.12f, b.w * 0.58f, b.h * 1.05f));

	double t = time_now_d();
	for (int i = 0; i < 5; ++i) {
		float phase = float(t * (0.12f + i * 0.015f) + i);
		float x = b.x + b.w * (0.72f + 0.14f * std::sin(phase));
		float y = b.y + b.h * (0.22f + 0.11f * std::cos(phase * 1.2f));
		float r = 24.0f + i * 10.0f;
		dc.FillRect(UI::Drawable(i % 2 ? 0x252A74CF : 0x24308ECF), Bounds(x - r, y - r, r * 2.0f, r * 2.0f));
	}

	float alpha = std::clamp(entrance_, 0.0f, 1.0f);
	dc.FillRect(UI::Drawable(0xFF68A8FF), Bounds(b.x + 18.0f, b.y + 58.0f, (b.w - 36.0f) * 0.38f * alpha, 2.0f));
	dc.FillRect(UI::Drawable(0xA02D75A0), Bounds(b.x + 18.0f, b.y + b.h - 38.0f, b.w - 36.0f, 1.0f));
}

bool RetroScreen::key(const KeyInput &key) {
	if (RetroHost::Instance().IsLoaded()) {
		const int16_t pressed = (key.flags & KeyInputFlags::DOWN) ? 1 : 0;
		unsigned retroId = 0xFFFF;
		switch (key.keyCode) {
		case NKCODE_DPAD_UP: retroId = 4; break;
		case NKCODE_DPAD_DOWN: retroId = 5; break;
		case NKCODE_DPAD_LEFT: retroId = 6; break;
		case NKCODE_DPAD_RIGHT: retroId = 7; break;
		case NKCODE_BUTTON_B: retroId = 0; break;
		case NKCODE_BUTTON_Y: retroId = 1; break;
		case NKCODE_BUTTON_SELECT: retroId = 2; break;
		case NKCODE_BUTTON_START: retroId = 3; break;
		case NKCODE_BUTTON_A: retroId = 8; break;
		case NKCODE_BUTTON_X: retroId = 9; break;
		case NKCODE_BUTTON_L1: retroId = 10; break;
		case NKCODE_BUTTON_R1: retroId = 11; break;
		default: break;
		}
		if (retroId != 0xFFFF) {
			RetroHost::Instance().SetControllerState(retroId, pressed);
			return true;
		}
	}
	if (key.flags & KeyInputFlags::DOWN) {
		bool moved = true;
		switch (key.keyCode) {
		case NKCODE_DPAD_LEFT:
			category_ = std::max(0, category_ - 1);
			break;
		case NKCODE_DPAD_RIGHT:
			category_ = std::min(4, category_ + 1);
			break;
		case NKCODE_DPAD_UP:
			item_ = std::max(0, item_ - 1);
			break;
		case NKCODE_DPAD_DOWN:
			item_++;
			break;
		default:
			moved = false;
			break;
		}
		if (moved) {
			UI::PlayUISound(UI::UISound::SELECT);
			RecreateViews();
			return true;
		}
	}
	return UIScreen::key(key);
}
