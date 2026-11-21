#include "UI/RetroScreen.h"

#include <algorithm>
#include <cmath>

#include "Common/Data/Format/IniFile.h"
#include "Common/File/FileUtil.h"
#include "Common/System/Request.h"
#include "Common/System/System.h"
#include "Common/TimeUtil.h"
#include "Common/UI/Root.h"
#include "Common/UI/Context.h"
#include "Common/UI/ViewGroup.h"
#include "Common/Input/KeyCodes.h"

#include "Core/Config.h"
#include "Core/System.h"
#include "UI/RetroHost.h"

RetroScreen::RetroScreen() {
	RefreshStatus();
}

static const char *Categories[] = {
	"PSP",
	"PS2",
	"NINTENDO",
	"PS3",
	"SETTINGS",
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
	status_ = ps2BiosPath_.empty() ? "PS2 BIOS: NOT SELECTED" : "PS2 BIOS: " + ps2BiosPath_;
}

void RetroScreen::CreateViews() {
	using namespace UI;

	categoryOffset_ = entrance_ = 0.0f;
	root_ = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	LinearLayout *top = root_->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	top->SetSpacing(10.0f);

	for (int i = 0; i < 5; ++i) {
		Choice *choice = top->Add(new Choice(Categories[i], new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT)));
		choice->SetTag(std::to_string(i));
		choice->OnClick.Add([this, i](UI::EventParams &) {
			category_ = i;
			item_ = 0;
			UI::PlayUISound(UI::UISound::SELECT);
			RecreateViews();
		});
	}

	LinearLayout *body = root_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT)));
	body->SetSpacing(8.0f);
	body->Add(new TextView(Categories[category_], ALIGN_LEFT, false));

	if (category_ == 0) {
		body->Add(new TextView("Native PPSSPP PSP core", ALIGN_LEFT, false));
		body->Add(new Choice("Browser / Load PSP games"))->OnClick.Add([this](UI::EventParams &) {
			System_BrowseForFile(GetRequesterToken(), "Load PSP game", BrowseFileType::BOOTABLE,
				[](std::string_view value, int) { System_PostUIMessage(UIMessage::REQUEST_GAME_BOOT, value); });
		});
	} else if (category_ == 1) {
		body->Add(new TextView(status_, ALIGN_LEFT, false));
		body->Add(new Choice("Choose PS2 BIOS (.bin / .rom)"))->OnClick.Add([this](UI::EventParams &) { BrowseBios(); });
		body->Add(new Choice("Choose Play! or compatible ARM64 core"))->OnClick.Add([this](UI::EventParams &) { BrowseCustomCore(); });
		body->Add(new Choice("Load PS2 game"))->OnClick.Add([this](UI::EventParams &) { BrowsePs2Game(); });
	} else if (category_ == 2) {
		body->Add(new TextView("ARM64: Gambatte / SameBoy / mGBA / melonDS", ALIGN_LEFT, false));
		body->Add(new Choice("Scan Nintendo ROM folder"))->OnClick.Add([this](UI::EventParams &) { BrowseNintendoFolder(); });
		body->Add(new Choice("Load native Nintendo core"))->OnClick.Add([this](UI::EventParams &) { LoadCoreCandidate(); });
	} else if (category_ == 3) {
		body->Add(new TextView("PS3: no stable open ARM64 libretro core is available.", ALIGN_LEFT, false));
		body->Add(new Choice("Scan PS3 images (EBOOT.BIN / ISO)"))->OnClick.Add([this](UI::EventParams &) { BrowsePs3Image(); });
		body->Add(new Choice("Attach future ARM64 external core"))->OnClick.Add([this](UI::EventParams &) { BrowseCustomCore(); });
	} else {
		body->Add(new TextView(status_, ALIGN_LEFT, false));
		body->Add(new Choice("Choose PS2 BIOS"))->OnClick.Add([this](UI::EventParams &) { BrowseBios(); });
		body->Add(new Choice("Choose PS2 / PS3 custom external core"))->OnClick.Add([this](UI::EventParams &) { BrowseCustomCore(); });
	}
}

void RetroScreen::BrowseBios() {
	System_BrowseForFile(GetRequesterToken(), "Select PS2 BIOS", BrowseFileType::ANY,
		[this](std::string_view value, int) {
			std::string source(value);
			Path dest = GetSysDirectory(DIRECTORY_PSP) / "SYSTEM" / "bios" / "ps2";
			File::CreateFullPath(dest);
			Path sourcePath(source);
			Path target = dest / sourcePath.GetFilename();
			if (!source.empty() && File::Copy(sourcePath, target)) {
				ps2BiosPath_ = target.ToString();
				ps2BiosRegion_ = ps2BiosPath_.find("USA") != std::string::npos ? "USA" :
					ps2BiosPath_.find("EUR") != std::string::npos ? "Europe" :
					ps2BiosPath_.find("JAP") != std::string::npos ? "Japan" : "Unknown";

				IniFile ini;
				ini.Load(g_Config.IniFilename());
				Section *section = ini.GetOrCreateSection("PS2");
				section->Set("BIOSPath", ps2BiosPath_);
				section->Set("BIOSRegion", ps2BiosRegion_);
				ini.Save(g_Config.IniFilename());
				status_ = "PS2 BIOS: " + ps2BiosPath_ + " (" + ps2BiosRegion_ + ")";
				UI::PlayUISound(UI::UISound::CONFIRM);
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
			if (!customCorePath_.empty())
				RetroHost::Instance().LoadContent(customCorePath_, ps2GamePath_);
			status_ = customCorePath_.empty()
				? "PS2 core not found. Attach play_libretro_android.so."
				: "Loading PS2: " + ps2GamePath_;
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
	dc.FillRect(UI::Drawable(0xA0001748), Bounds(b.x, b.y, b.w, b.h * 0.55f));

	double t = time_now_d();
	for (int i = 0; i < 5; ++i) {
		float phase = float(t * (0.12f + i * 0.015f) + i);
		float x = b.x + b.w * (0.68f + 0.12f * std::sin(phase));
		float y = b.y + b.h * (0.22f + 0.10f * std::cos(phase * 1.2f));
		float r = 18.0f + i * 7.0f;
		dc.FillRect(UI::Drawable(0x30149AE8), Bounds(x - r, y - r, r * 2.0f, r * 2.0f));
	}

	float alpha = std::clamp(entrance_, 0.0f, 1.0f);
	dc.FillRect(UI::Drawable(0x802D75A0), Bounds(b.x + 24.0f, b.y + b.h * 0.18f, b.w - 48.0f, 2.0f * alpha));
}

bool RetroScreen::key(const KeyInput &key) {
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
