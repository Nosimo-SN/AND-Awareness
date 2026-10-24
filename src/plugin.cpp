// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Nosimo-SN. Part of A.N.D. Awareness: https://github.com/Nosimo-SN/AND-Awareness

// A.N.D. Awareness: watches A.N.D.'s AND_Flashing* factions on the player and nearby (high-process) NPCs.
//  - Every change raises the SKSE mod event "SN_ANDFlash" (strArg = flash type, numArg = 1 start / 0 end,
//    sender = the actor) for any mod or trigger that wants it.
//  - Flash starts that pass the user's settings (SkyrimNet Settings > Plugins > AND_Awareness) are posted to
//    SkyrimNet as one narration line through its C++ API, with the actor as originator.
// No Papyrus. A worker thread paces the poll, posts one scan task at a time to the game thread, reloads the
// settings and makes the SkyrimNet calls, so the game thread never waits on SkyrimNet.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "SkyrimNet/PublicAPI.h"

namespace
{
	constexpr auto kANDPlugin = "Advanced Nudity Detection.esp"sv;
	constexpr auto kConfigName = "AND_Awareness";
	constexpr auto kEventName = "SN_ANDFlash";
	constexpr auto kPollInterval = 2s;
	constexpr int  kReloadEveryPolls = 5;  // settings reload every ~10 s

	struct FlashType
	{
		RE::FormID  localID;
		const char* name;         // strArg of the mod event
		const char* settingKey;   // types.<key> / text.<key>
		const char* defaultText;
	};

	constexpr std::array kFlashTypes{
		FlashType{ 0x875, "ChestCurtain", "chest_curtain", "The cloth draped over {name}'s chest swings aside, briefly baring {their} {chest}." },
		FlashType{ 0x89A, "PelvicCurtain", "pelvic_curtain", "The front of {name}'s skirt sways aside, briefly exposing what lies beneath it." },
		FlashType{ 0x89B, "AssCurtain", "ass_curtain", "The back of {name}'s skirt flips up, briefly exposing what lies beneath it." },
		FlashType{ 0x89C, "Top", "top", "Light catches {name}'s sheer top, and for a moment it hides little of what lies beneath." },
		FlashType{ 0x89D, "Bra", "bra", "For a moment, {name}'s sheer {band} turns see-through in the light." },
		FlashType{ 0x89E, "Bottom", "bottom", "Light passes through {name}'s sheer lower garment, briefly revealing what lies beneath." },
		FlashType{ 0x89F, "Underwear", "underwear", "For a moment, {name}'s sheer smallclothes hide little." },
		FlashType{ 0x8A0, "Hotpants", "hotpants", "For a moment, {name}'s sheer shorts turn see-through in the light." },
		FlashType{ 0x8A1, "Skirt", "skirt", "For a moment, the sheer skirt over {name}'s rear turns see-through in the light." },
		FlashType{ 0x8A2, "CString", "cstring", "For a moment, the tiny strip {name} wears is too slight to notice, and {their} groin looks bare." },
	};
	constexpr std::size_t kTypeCount = kFlashTypes.size();

	// Defaults mirror settings/AND_Awareness.yaml; used until (or if never) SkyrimNet answers.
	struct Settings
	{
		bool                                 enabled{ true };
		bool                                 player{ true };
		bool                                 npcs{ true };
		bool                                 followersOnly{ false };
		int                                  maxDistance{ 1500 };
		int                                  chance{ 25 };
		int                                  globalCooldown{ 180 };
		int                                  actorCooldown{ 600 };
		bool                                 forceReaction{ true };
		std::array<bool, kTypeCount>         types{};
		std::array<std::string, kTypeCount>  texts{};
		bool                                 logAll{ false };

		Settings()
		{
			types.fill(true);
			for (std::size_t i = 0; i < kTypeCount; ++i) {
				texts[i] = kFlashTypes[i].defaultText;
			}
		}
	};

	struct Announcement
	{
		RE::FormID  formID;
		std::string text;
		bool        forceReaction;
	};

	std::array<RE::TESFaction*, kTypeCount> g_factions{};
	std::atomic_bool                         g_dataReady{ false };
	std::atomic_bool                         g_inGame{ false };
	std::atomic_bool                         g_scanQueued{ false };
	bool                                     g_haveSkyrimNet{ false };

	std::mutex g_settingsLock;
	Settings   g_settings;

	std::mutex                g_queueLock;
	std::vector<Announcement> g_queue;

	// Game thread only.
	using Clock = std::chrono::steady_clock;
	std::unordered_map<RE::FormID, std::uint16_t>     g_lastMask;
	std::unordered_map<RE::FormID, Clock::time_point> g_actorAnnounced;
	std::optional<Clock::time_point>                  g_lastAnnounced;
	bool                                              g_primed{ false };
	std::mt19937                                      g_rng{ std::random_device{}() };

	void InitializeLog()
	{
		auto path = logs::log_directory();
		if (!path) {
			SKSE::stl::report_and_fail("Failed to find standard logging directory"sv);
		}
		*path /= "AND_Awareness.log"sv;
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
		auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S:%e] [%l] %v"s);
	}

	// --- settings ------------------------------------------------------------------------------------

	std::string ConfigValue(const std::string& a_path, const std::string& a_default)
	{
		if (!PublicGetPluginConfigValue) {
			return a_default;
		}
		return PublicGetPluginConfigValue(kConfigName, a_path.c_str(), a_default.c_str());
	}

	bool ConfigBool(const std::string& a_path, bool a_default)
	{
		auto value = ConfigValue(a_path, a_default ? "true" : "false");
		std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return value == "true" || value == "1" || value == "yes" || value == "on";
	}

	int ConfigInt(const std::string& a_path, int a_default, int a_min, int a_max)
	{
		const auto value = ConfigValue(a_path, std::to_string(a_default));
		double     parsed = a_default;
		try {
			parsed = std::stod(value);
		} catch (...) {
			parsed = a_default;
		}
		return std::clamp(static_cast<int>(parsed), a_min, a_max);
	}

	void ReloadSettings()
	{
		Settings s;
		if (PublicGetPluginConfigValue) {
			s.enabled = ConfigBool("general.enabled", s.enabled);
			s.player = ConfigBool("general.player", s.player);
			s.npcs = ConfigBool("general.npcs", s.npcs);
			s.followersOnly = ConfigBool("general.followers_only", s.followersOnly);
			s.maxDistance = ConfigInt("general.max_distance", s.maxDistance, 0, 8192);
			s.chance = ConfigInt("frequency.chance", s.chance, 0, 100);
			s.globalCooldown = ConfigInt("frequency.global_cooldown", s.globalCooldown, 0, 3600);
			s.actorCooldown = ConfigInt("frequency.actor_cooldown", s.actorCooldown, 0, 3600);
			s.forceReaction = ConfigBool("frequency.force_reaction", s.forceReaction);
			for (std::size_t i = 0; i < kTypeCount; ++i) {
				const std::string key = kFlashTypes[i].settingKey;
				s.types[i] = ConfigBool("types." + key, true);
				s.texts[i] = ConfigValue("text." + key, kFlashTypes[i].defaultText);
				if (s.texts[i].empty()) {
					s.texts[i] = kFlashTypes[i].defaultText;
				}
			}
			s.logAll = ConfigBool("debug.log_all", s.logAll);
		}
		std::scoped_lock lock(g_settingsLock);
		g_settings = std::move(s);
	}

	Settings SnapshotSettings()
	{
		std::scoped_lock lock(g_settingsLock);
		return g_settings;
	}

	// --- detection (game thread) ---------------------------------------------------------------------

	std::uint16_t ReadMask(RE::Actor* a_actor, bool a_isPlayer)
	{
		std::uint16_t mask = 0;
		for (std::size_t i = 0; i < kTypeCount; ++i) {
			if (g_factions[i] && a_actor->GetFactionRank(g_factions[i], a_isPlayer) > 0) {
				mask |= static_cast<std::uint16_t>(1u << i);
			}
		}
		return mask;
	}

	void ReplaceAll(std::string& a_text, std::string_view a_token, std::string_view a_value)
	{
		for (auto pos = a_text.find(a_token); pos != std::string::npos; pos = a_text.find(a_token, pos + a_value.size())) {
			a_text.replace(pos, a_token.size(), a_value);
		}
	}

	std::string ComposeText(RE::Actor* a_actor, const std::string& a_template)
	{
		const auto base = a_actor->GetActorBase();
		const bool female = base && base->GetSex() == RE::SEX::kFemale;
		const auto fullName = a_actor->GetDisplayFullName();

		std::string text = a_template;
		ReplaceAll(text, "{name}", fullName && *fullName ? fullName : "someone");
		ReplaceAll(text, "{their}", female ? "her" : "his");
		ReplaceAll(text, "{chest}", female ? "breasts" : "chest");
		ReplaceAll(text, "{band}", female ? "breastband" : "chest wrap");
		return text;
	}

	// Children are never tracked, announced or reported, whatever their clothing is tagged with.
	// The race child flag covers vanilla and most custom child races; the race EditorID check catches
	// custom child races that forgot the flag (EditorIDs are available because A.N.D. requires po3 Tweaks).
	bool IsChildActor(RE::Actor* a_actor)
	{
		if (a_actor->IsChild()) {
			return true;
		}
		const auto race = a_actor->GetRace();
		const auto raceID = race ? race->GetFormEditorID() : nullptr;
		if (!raceID) {
			return false;
		}
		std::string editorID = raceID;
		std::ranges::transform(editorID, editorID.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return editorID.find("child") != std::string::npos;
	}

	// Gate order: master switch, which actors, flash type, cooldowns, then the chance roll.
	void MaybeAnnounce(RE::Actor* a_actor, bool a_isPlayer, std::size_t a_index, const Settings& a_settings, RE::PlayerCharacter* a_player)
	{
		if (!g_haveSkyrimNet || !a_settings.enabled || !a_settings.types[a_index]) {
			return;
		}
		if (a_isPlayer ? !a_settings.player : !a_settings.npcs) {
			return;
		}
		if (!a_isPlayer) {
			if (a_settings.followersOnly && !a_actor->IsPlayerTeammate()) {
				return;
			}
			if (a_settings.maxDistance > 0 && a_actor->GetDistance(a_player) > static_cast<float>(a_settings.maxDistance)) {
				return;
			}
		}

		const auto now = Clock::now();
		const auto id = a_actor->GetFormID();
		if (g_lastAnnounced && now - *g_lastAnnounced < std::chrono::seconds(a_settings.globalCooldown)) {
			return;
		}
		if (const auto it = g_actorAnnounced.find(id); it != g_actorAnnounced.end() && now - it->second < std::chrono::seconds(a_settings.actorCooldown)) {
			return;
		}
		if (std::uniform_int_distribution<int>(1, 100)(g_rng) > a_settings.chance) {
			return;
		}

		g_lastAnnounced = now;
		g_actorAnnounced[id] = now;
		auto text = ComposeText(a_actor, a_settings.texts[a_index]);
		logs::info("announce {:08X} {}: {}", id, kFlashTypes[a_index].name, text);

		std::scoped_lock lock(g_queueLock);
		g_queue.push_back({ id, std::move(text), a_settings.forceReaction });
	}

	void SendModEvent(RE::Actor* a_actor, std::size_t a_index, bool a_started)
	{
		SKSE::ModCallbackEvent evn{
			RE::BSFixedString(kEventName),
			RE::BSFixedString(kFlashTypes[a_index].name),
			a_started ? 1.0f : 0.0f,
			a_actor
		};
		SKSE::GetModCallbackEventSource()->SendEvent(&evn);
	}

	void ScanActor(RE::Actor* a_actor, bool a_isPlayer, const Settings& a_settings, RE::PlayerCharacter* a_player,
		std::unordered_map<RE::FormID, std::uint16_t>& a_seen)
	{
		// Excluded actors never enter a_seen, so they get no mod event and no narration.
		if (!a_actor || a_actor->IsDead() || IsChildActor(a_actor)) {
			return;
		}
		const auto id = a_actor->GetFormID();
		const auto mask = ReadMask(a_actor, a_isPlayer);
		a_seen[id] = mask;

		const auto          it = g_lastMask.find(id);
		const std::uint16_t prev = it != g_lastMask.end() ? it->second : 0;
		const std::uint16_t changed = mask ^ prev;
		for (std::size_t i = 0; i < kTypeCount; ++i) {
			if (!(changed & (1u << i))) {
				continue;
			}
			const bool started = (mask & (1u << i)) != 0;
			SendModEvent(a_actor, i, started);
			if (a_settings.logAll) {
				logs::info("{} {:08X} {} {}", kEventName, id, kFlashTypes[i].name, started ? "start" : "end");
			}
			// The first scan after a load only records state, so flashes already in progress stay quiet.
			if (started && g_primed) {
				MaybeAnnounce(a_actor, a_isPlayer, i, a_settings, a_player);
			}
		}
	}

	void Scan()
	{
		g_scanQueued = false;
		if (!g_dataReady || !g_inGame) {
			return;
		}
		const auto player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return;
		}

		const auto settings = SnapshotSettings();
		std::unordered_map<RE::FormID, std::uint16_t> seen;
		ScanActor(player, true, settings, player, seen);
		if (const auto lists = RE::ProcessLists::GetSingleton()) {
			for (auto& handle : lists->highActorHandles) {
				if (const auto actor = handle.get()) {
					ScanActor(actor.get(), false, settings, player, seen);
				}
			}
		}
		// Actors that left the high process drop out silently; returning later re-announces an active flash.
		g_lastMask = std::move(seen);
		g_primed = true;
	}

	void ResetGameState()
	{
		g_lastMask.clear();
		g_actorAnnounced.clear();
		g_lastAnnounced.reset();
		g_primed = false;
	}

	// --- worker thread -------------------------------------------------------------------------------

	void PostAnnouncements()
	{
		std::vector<Announcement> pending;
		{
			std::scoped_lock lock(g_queueLock);
			pending.swap(g_queue);
		}
		for (const auto& a : pending) {
			const auto type = a.forceReaction ? "direct_narration" : "persistent_generic";
			const int  eventId = PublicRegisterEvent ? PublicRegisterEvent(type, a.text.c_str(), a.formID, 0, nullptr, 0) : 0;
			if (eventId == 0) {
				logs::warn("SkyrimNet did not store the {} for {:08X}", type, a.formID);
			}
		}
	}

	// Pace from our own thread: one queued scan at a time, never a self-requeueing task.
	void WorkerLoop()
	{
		int polls = 0;
		for (;;) {
			std::this_thread::sleep_for(kPollInterval);
			if (++polls >= kReloadEveryPolls) {
				polls = 0;
				ReloadSettings();
			}
			PostAnnouncements();
			if (g_dataReady && g_inGame && !g_scanQueued.exchange(true)) {
				SKSE::GetTaskInterface()->AddTask([] { Scan(); });
			}
		}
	}

	// --- lifecycle -----------------------------------------------------------------------------------

	void ResolveFactions()
	{
		const auto  dataHandler = RE::TESDataHandler::GetSingleton();
		std::size_t found = 0;
		for (std::size_t i = 0; i < kTypeCount; ++i) {
			g_factions[i] = dataHandler ? dataHandler->LookupForm<RE::TESFaction>(kFlashTypes[i].localID, kANDPlugin) : nullptr;
			if (g_factions[i]) {
				++found;
			} else {
				logs::warn("Faction {} ({:03X}) not found in {}", kFlashTypes[i].name, kFlashTypes[i].localID, kANDPlugin);
			}
		}
		logs::info("Resolved {}/{} A.N.D. flashing factions", found, kTypeCount);
		g_dataReady = found > 0;
	}

	void ConnectSkyrimNet()
	{
		if (!FindFunctions() || !PublicGetVersion) {
			logs::warn("SkyrimNet API not found: SN_ANDFlash mod events still fire, but no narration is posted");
			return;
		}
		g_haveSkyrimNet = PublicRegisterEvent != nullptr;
		logs::info("SkyrimNet API v{}; event registration {}; settings {}", PublicGetVersion(),
			g_haveSkyrimNet ? "available" : "MISSING (needs API v11+)",
			PublicGetPluginConfigValue ? "available" : "MISSING (using defaults)");
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			ResolveFactions();
			ConnectSkyrimNet();
			ReloadSettings();
			if (g_dataReady) {
				std::thread(WorkerLoop).detach();
			}
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
			g_inGame = false;
			SKSE::GetTaskInterface()->AddTask([] { ResetGameState(); });
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
			// The payload is the success bool cast into the pointer value.
			g_inGame = a_msg->data != nullptr;
			break;
		case SKSE::MessagingInterface::kNewGame:
			SKSE::GetTaskInterface()->AddTask([] { ResetGameState(); });
			g_inGame = true;
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	InitializeLog();
	SKSE::Init(a_skse, false);
	logs::info("A.N.D. Awareness 1.0.1 loaded");

	if (!SKSE::GetMessagingInterface()->RegisterListener(OnMessage)) {
		logs::critical("Could not register the SKSE message listener");
		return false;
	}
	return true;
}
