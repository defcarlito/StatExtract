#include "pch.h"
#include "StatExtract.h"
#include "bakkesmod/wrappers/GameObject/Stats/StatEventWrapper.h"

#include <string>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <chrono>
#include <iostream>



BAKKESMOD_PLUGIN(StatExtract, "Pull stats from your comp games.", plugin_version, PLUGINTYPE_FREEPLAY)

std::shared_ptr<CVarManagerWrapper> _globalCvarManager;

void StatExtract::onLoad()
{
	_globalCvarManager = cvarManager;
	LOG("StatExtract loaded.");

	cvarManager->registerCvar("http_req_enabled", "0", "Enable post-match HTTP request", true, true, 0, true, 1);
	cvarManager->registerCvar("http_req_url", "", "URL to make HTTP request to");
	cvarManager->registerCvar("http_req_auth_key", "", "Auth key for HTTP Request");
	cvarManager->registerCvar("ranked1v1_enabled", "0", "Record ranked 1v1 stats", true, true, 0, true, 1);
	cvarManager->registerCvar("ranked2v2_enabled", "0", "Record ranked 2v2 stats", true, true, 0, true, 1);
	cvarManager->registerCvar("ranked3v3_enabled", "0", "Record ranked 3v3 stats", true, true, 0, true, 1);

	cvarManager->registerNotifier("saved_stats_toast", [this](std::vector<std::string> args) {
		gameWrapper->Toast("StatExtract", "Captured all stats successfully.", "default", 5.0, ToastType_OK);
		}, "", PERMISSION_ALL);
	cvarManager->registerNotifier("made_http_request", [this](std::vector<std::string> args) {
		gameWrapper->Toast("StatExtract", "Made HTTP request to URL successfully.", "default", 5.0, ToastType_OK);
		}, "", PERMISSION_ALL);
	cvarManager->registerNotifier("error_with_http_request", [this](std::vector<std::string> args) {
		std::string code = args.size() > 1 ? args[1] : "-1";
		gameWrapper->Toast("StatExtract", "Error making HTTP request. Status code: " + code, "default", 5.0, ToastType_Error);
		}, "", PERMISSION_ALL);
	cvarManager->registerNotifier("http_req_url_empty", [this](std::vector<std::string> args) {
		gameWrapper->Toast("StatExtract", "Tried sending stats but the request URL is empty.", "default", 5.0, ToastType_Warning);
		}, "", PERMISSION_ALL);
	cvarManager->registerNotifier("http_req_not_enabled", [this](std::vector<std::string> args) {
		gameWrapper->Toast("StatExtract", "Tried sending stats but post-match HTTP request is disabled.", "default", 5.0, ToastType_Warning);
		}, "", PERMISSION_ALL);
	cvarManager->registerNotifier("make_request", [this](std::vector<std::string> args) {
		makePostRequestToURL(data);
		}, "", PERMISSION_ALL);


	// Fires every time count down starts
	gameWrapper->HookEvent("Function GameEvent_TA.Countdown.BeginState", std::bind(&StatExtract::onMatchStart, this));

	// Fires when match ends normally
	gameWrapper->HookEvent("Function TAGame.GameEvent_Soccar_TA.EventMatchEnded", std::bind(&StatExtract::onMatchEnd, this));

	// Fires when local player leaves
	gameWrapper->HookEvent("Function TAGame.GameEvent_Soccar_TA.Destroyed", std::bind(&StatExtract::onMatchEnd, this));

	gameWrapper->HookEvent("Function TAGame.GFxHUD_TA.HandleStatEvent", std::bind(&StatExtract::pullScoreBoardStats, this));
	gameWrapper->HookEventWithCallerPost<ServerWrapper>("Function TAGame.GFxHUD_TA.HandleStatTickerMessage",
		[this](ServerWrapper caller, void* params, std::string eventname) {
			pullScoreBoardStats();
			onStatTickerMessage(params);
		});

	gameWrapper->HookEvent("Function TAGame.GFxHUD_TA.HandlePenaltyChanged", std::bind(&StatExtract::onForfeit, this));
	
}

void StatExtract::onMatchStart() {

	if (isMatchStarted) return;

	resetMatchVariables();

	ServerWrapper game = gameWrapper->GetOnlineGame();
	if (!game) return;

	ArrayWrapper<PriWrapper> PRIs = game.GetPRIs();
	UniqueIDWrapper uid = gameWrapper->GetUniqueID();
	MMRWrapper mmrw = gameWrapper->GetMMRWrapper();

	playlist = game.GetPlaylist().GetPlaylistId();
	mmrBefore = mmrw.GetPlayerMMR(uid, playlist);

	auto now = std::chrono::system_clock::now();
	auto epoch = now.time_since_epoch(); 
	startEpoch = static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(epoch).count());

	for (PriWrapper pri : PRIs) {
		if (!pri) continue;

		UniqueIDWrapper uid = pri.GetUniqueIdWrapper();

		json player = {
			{ "name", pri.GetPlayerName().ToString() },
			{ "team", pri.GetTeamNum() },
			{ "platform", pri.GetPlatform() },
			{ "isLocal", pri.IsLocalPlayerPRI() },
			{ "uid", uid.str() }
		};

		players.push_back(player);
	}

	isMatchStarted = true;
}

void StatExtract::onForfeit() {
	ServerWrapper game = gameWrapper->GetOnlineGame();
	if (!game) return;
	isForfeit = game.GetbForfeit();
}

void StatExtract::onMatchEnd() {

	if (playlist == -1) return;

	// temp comment for testing
	/*int RANKED_ONES = 10;
	int RANKED_TWOS = 11;
	int RANKED_THREES = 13;

	if (playlist != RANKED_ONES && playlist != RANKED_TWOS && playlist != RANKED_THREES) return;

	CVarWrapper enableRanked1v1 = cvarManager->getCvar("ranked1v1_enabled");
	CVarWrapper enableRanked2v2 = cvarManager->getCvar("ranked2v2_enabled");
	CVarWrapper enableRanked3v3 = cvarManager->getCvar("ranked3v3_enabled");

	if (!enableRanked1v1) return;
	if (!enableRanked2v2) return;
	if (!enableRanked3v3) return;

	if (playlist == RANKED_ONES && !enableRanked1v1.getBoolValue()) return;
	if (playlist == RANKED_TWOS && !enableRanked2v2.getBoolValue()) return;
	if (playlist == RANKED_THREES && !enableRanked3v3.getBoolValue()) return;*/


	// Try to pull final scoreboard stats
	pullScoreBoardStats();
	
	// Delay for MMR capture
	gameWrapper->SetTimeout([this](GameWrapper*) {

		// Capture local player MMR
		UniqueIDWrapper uid = gameWrapper->GetUniqueID();
		MMRWrapper mmrw = gameWrapper->GetMMRWrapper();
		mmrAfter = mmrw.GetPlayerMMR(uid, playlist);

		LOG("MATCH OVER");

		// Build data json
		data["playlist"] = playlist;
		data["mmrBefore"] = mmrBefore;
		data["mmrAfter"] = mmrAfter;
		data["startEpoch"] = startEpoch;
		data["players"] = players;
		data["goals"] = goals;
		data["endedOnForfeit"] = isForfeit;

		cvarManager->log(data.dump(2));

		LOG("DATA DUMPED");

		cvarManager->executeCommand("saved_stats_toast");

		// Make HTTP request
		makePostRequestToURL(data);

		// Reset variables
		resetMatchVariables();

		}, 0.2f);

}

void StatExtract::onStatTickerMessage(void* params) {

	struct StatTickerParams {
		uintptr_t Receiver;
		uintptr_t Victim;
		uintptr_t StatEvent;
	};

	StatTickerParams* pStruct = (StatTickerParams*)params;
	PriWrapper receiver = PriWrapper(pStruct->Receiver);
	PriWrapper victim = PriWrapper(pStruct->Victim);
	StatEventWrapper statEvent = StatEventWrapper(pStruct->StatEvent);

	ServerWrapper game = gameWrapper->GetOnlineGame();
	if (!game) return;

	if (statEvent.GetEventName() == "Goal") {

		if (!receiver) return;

		UniqueIDWrapper uid = receiver.GetUniqueIdWrapper();

		json goal = {
			{ "uid", uid.str() },
			{ "time",  game.GetSecondsRemaining() }
		};
		
		cvarManager->log(goal.dump(2));

		goals.push_back(goal);

	}

}

void StatExtract::pullScoreBoardStats() {
	ServerWrapper game = gameWrapper->GetOnlineGame();
	if (!game) return;

	ArrayWrapper<PriWrapper> PRIs = game.GetPRIs();
	for (PriWrapper pri : PRIs) {
		if (!pri) continue;

		UniqueIDWrapper uid = pri.GetUniqueIdWrapper();

		for (auto& player : players) {

			if (player["uid"] == uid.str() && player["platform"] == uid.GetPlatform()) {
				
				player["score"] = pri.GetMatchScore();
				player["goals"] = pri.GetMatchGoals();
				player["assists"] = pri.GetMatchAssists();
				player["saves"] = pri.GetMatchSaves();
				player["shots"] = pri.GetMatchShots();

				break;
			}
		}
		
	}

}

void StatExtract::RenderSettings() {

	ImGui::Text("Post-match HTTP request options:");

	CVarWrapper enableHttpReq = cvarManager->getCvar("http_req_enabled");
	if (!enableHttpReq) return;
	bool httpEnabled = enableHttpReq.getBoolValue();
	if (ImGui::Checkbox("Make HTTP POST request", &httpEnabled)) {
		enableHttpReq.setValue(httpEnabled);
	}

	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Toggle to make a POST request with match stats after every match. ");
	}

	CVarWrapper reqUrl = cvarManager->getCvar("http_req_url");
	if (!reqUrl) return;
	std::string httpReqUrlString = reqUrl.getStringValue();
	if (ImGui::InputText("Request URL", &httpReqUrlString)) {
		reqUrl.setValue(httpReqUrlString);
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("URL to make the POST request to. Can't be empty if box is ticked.");
	}

	CVarWrapper authKey = cvarManager->getCvar("http_req_auth_key");
	if (!authKey) return;
	std::string authKeyString = authKey.getStringValue();
	if (ImGui::InputText("Request Auth Key (optional)", &authKeyString)) {
		authKey.setValue(authKeyString);
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Auth key used to validate request. Optional but recomended.");
	}

	if (ImGui::Button("Test connection")) {
		json testData;
		testData["data"] = "test";
		makePostRequestToURL(testData);
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Send a POST request to URL with auth key and dummy data. Will return success if status code is 200.");
	}

	ImGui::Separator();

	ImGui::Text("Record stats for playlists:");

	CVarWrapper enableRanked1v1 = cvarManager->getCvar("ranked1v1_enabled");
	if (!enableRanked1v1) return;
	bool isRanked1v1Enabled = enableRanked1v1.getBoolValue();
	if (ImGui::Checkbox("Ranked 1v1", &isRanked1v1Enabled)) {
		enableRanked1v1.setValue(isRanked1v1Enabled);
	}

	CVarWrapper enableRanked2v2 = cvarManager->getCvar("ranked2v2_enabled");
	if (!enableRanked2v2) return;
	bool isRanked2v2Enabled = enableRanked2v2.getBoolValue();
	if (ImGui::Checkbox("Ranked 2v2", &isRanked2v2Enabled)) {
		enableRanked2v2.setValue(isRanked2v2Enabled);
	}

	CVarWrapper enableRanked3v3 = cvarManager->getCvar("ranked3v3_enabled");
	if (!enableRanked3v3) return;
	bool isRanked3v3Enabled = enableRanked3v3.getBoolValue();
	if (ImGui::Checkbox("Ranked 3v3", &isRanked3v3Enabled)) {
		enableRanked3v3.setValue(isRanked3v3Enabled);
	}

	ImGui::Separator();
}

void StatExtract::resetMatchVariables() {
	isMatchStarted = false;
	playlist = -1;
	mmrBefore = -1;
	mmrAfter = -1;
	startEpoch = -1;
	isForfeit = false;
	data.clear();
	players.clear();
	goals.clear();
}

void StatExtract::makePostRequestToURL(json body) {
	CVarWrapper enableHttpReq = cvarManager->getCvar("http_req_enabled");
	CVarWrapper reqUrl = cvarManager->getCvar("http_req_url");
	CVarWrapper authKey = cvarManager->getCvar("http_req_auth_key");

	if (!enableHttpReq.getBoolValue()) {
		cvarManager->executeCommand("http_req_not_enabled");
		return;
	}

	if (reqUrl.getStringValue() == "") {
		cvarManager->executeCommand("http_req_url_empty");
		return;
	}

	if (!reqUrl) return;
	if (!authKey) return;
	if (!enableHttpReq) return;


	CurlRequest req;
	req.url = reqUrl.getStringValue();
	req.body = body.dump();
	req.headers = {
        {"x-auth-key", authKey.getStringValue()}
	};

	HttpWrapper::SendCurlJsonRequest(req, [this](int code, std::string result)
		{
			if (code == 200) {
				cvarManager->executeCommand("made_http_request");
			}
			else {
				cvarManager->executeCommand("error_with_http_request " + std::to_string(code));
			}
		});
}
