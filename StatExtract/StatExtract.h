#pragma once

#include "GuiBase.h"
#include "bakkesmod/plugin/bakkesmodplugin.h"
#include "bakkesmod/plugin/pluginwindow.h"
#include "bakkesmod/plugin/PluginSettingsWindow.h"

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include "version.h"
constexpr auto plugin_version = stringify(VERSION_MAJOR) "." stringify(VERSION_MINOR) "." stringify(VERSION_PATCH) "." stringify(VERSION_BUILD);


class StatExtract: public BakkesMod::Plugin::BakkesModPlugin
	,public SettingsWindowBase // Uncomment if you wanna render your own tab in the settings menu
	//,public PluginWindowBase // Uncomment if you want to render your own plugin window
{

	//std::shared_ptr<bool> enabled;

	//Boilerplate
	void onLoad() override;
	//void onUnload() override; // Uncomment and implement if you need a unload method

	bool isMatchStarted = false;

	int playlist = -1;
	int mmrBefore = -1;
	int mmrAfter = -1;
	int startEpoch = -1;
	bool isForfeit = false;

	json data;
	json players = json::array();
	json goals = json::array();
	

public:
	void RenderSettings() override; // Uncomment if you wanna render your own tab in the settings menu
	//void RenderWindow() override; // Uncomment if you want to render your own plugin window
	
	void onMatchStart();
	void onMatchEnd();
	void pullScoreBoardStats();
	void resetMatchVariables();
	void onStatTickerMessage(void* params);
	void onForfeit();
	void makePostRequestToURL(json data);
};
