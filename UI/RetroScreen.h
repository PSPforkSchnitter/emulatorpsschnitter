#pragma once

#include "Common/UI/UIScreen.h"
#include "Common/UI/ViewGroup.h"

class RetroScreen : public UIScreen {
public:
	RetroScreen();
	const char *tag() const override { return "Retro"; }
	bool isTopLevel() const override { return true; }

protected:
	void CreateViews() override;
	void update() override;
	void DrawBackground(UIContext &dc) override;
	bool key(const KeyInput &key) override;

private:
	int category_ = 0;
	int item_ = 0;
	float categoryOffset_ = 0.0f;
	float itemOffset_ = 0.0f;
	float entrance_ = 0.0f;
	double lastFrameTime_ = 0.0;

	void BrowseBios();
	void BrowsePs2Game();
	void BrowseNintendoFolder();
	void BrowsePs3Image();
	void BrowseCustomCore();
	void LoadCoreCandidate();
	void RefreshStatus();

	std::string ps2BiosPath_;
	std::string ps2BiosRegion_;
	std::string ps2GamePath_;
	std::string customCorePath_;
	std::string status_;
};
