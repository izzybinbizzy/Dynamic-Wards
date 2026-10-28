// Dynamic Wards - SKSE plugin
// Copyright (C) 2026 izzydoingit
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see the
// GNU General Public License in LICENSE for details.
//
// Dynamic Wards (since 3.0): one neutral set of ward meshes per rank, colored in memory (any color, any opacity). No plugin file; one Papyrus native
// (DynamicWards.FlashFor, for 360 Ward's rebuilt SphereWard script); settings live in DynamicWards.ini, nothing goes in the save.
//
// THE FILES, AND WHAT EACH ONE IS FOR (laid out as Illuminated's and RELight - Spell Addon's)
//   main.cpp           this file - the order of the data-load work, and the SKSE messages
//   Plugin.h           what the files share; PCH.h what every file includes
//   WardMath.h         game-free: the rows, the ladder, a ward's rank, the palette recolor - tested off the game
//   SettingsText.h     game-free: DynamicWards.ini as one table, its reader and writer - tested off the game
//   Settings.cpp       the live settings table and its accessors, the file read at load and saved after each change
//   Wards.cpp          which effects are wards, what each wears, the shields' switches, the 360 unlock, the hand refresh
//   Colors.cpp         each row's color made in memory: palettes on the graphics card, glow on each ward copy as it appears
//   Lighting.cpp       which lighting runs (found, never picked)                   DomeLights.cpp  the lights we hang
//   Menu.cpp           the SKSE Menu Framework pages              Papyrus.cpp  DynamicWards.FlashFor
//   DevBench.cpp       DevBench's inspect and control (optional)   Text.cpp     form names, form lookups
//   Arcs.cpp           the ward's strength as a Reticle Arcs bar (its ReticleArcsAPI.h, MIT, copied unchanged)
//   SKSEMenuFramework.h, DevBenchAPI.*  those mods' own files; DevBenchGlue.h, MenuStyle.h, Translation.h are the master
//                      copies the other plugins carry word for word
//   tests\testwards.cpp the off-game tests of WardMath.h and SettingsText.h, built and run by every SE build of the plugin

#include "Plugin.h"
#include "RuntimeGuard.h"
#include "Translation.h"

using namespace Plugin;

namespace
{
	// a loading screen re-applies; any other menu closing re-checks the 360 unlock
	struct MenuWatch : RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			Guarded("menu watch", [a_event]() {
				if (!a_event || a_event->opening) {
					return;
				}
				const std::string_view name{ a_event->menuName.c_str() };
				// a job that could not be queued is said once per place (as Guarded does), not at every menu close
				static std::atomic<bool> toldLoad{ false };
				static std::atomic<bool> toldMenu{ false };
				if (name == RE::LoadingMenu::MENU_NAME) {
					if (!Later(kWhyLoadingScreen, []() { ApplyAll(kWhyLoadingScreen); }) && !toldLoad.exchange(true)) {
						SKSE::log::warn("loading screen closed: the wards could not be re-applied (no main-thread task); later ones are not logged");
					}
				} else if (name != RE::HUDMenu::MENU_NAME && name != RE::CursorMenu::MENU_NAME) {
					FlushHeldColor();
					if (!Later("a menu closed", []() { CheckUnlock("a menu closed"); }) && !toldMenu.exchange(true)) {
						SKSE::log::warn("a menu closed: the 360 unlock could not be re-checked (no main-thread task); later ones are not logged");
					}
				}
			});
			return RE::BSEventNotifyControl::kContinue;
		}
		static MenuWatch* Get()
		{
			static MenuWatch w;
			return &w;
		}
	};

	// the order of the data-load work: the settings and the lighting first, the art and the wards, then what runs from then on
	void LoadEverything()
	{
		const auto started = std::chrono::steady_clock::now();
		LoadSettings();
		ReadLighting();
		NoteStrangeWards();
		MakeArt();
		ResolveFactions();
		FindWards();
		ApplyAll(kWhyDataLoaded);
		StartDomeLights();
		WatchArt();
		SKSE::log::info("{}", Translation::Load("Data/SKSE/Plugins/DynamicWards/Translation.json"));
		RegisterMenu();
		if (auto* ui = RE::UI::GetSingleton()) {
			ui->AddEventSink<RE::MenuOpenCloseEvent>(MenuWatch::Get());
		}
		SKSE::log::info("ready in {:.1f} ms", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
	}

	// an exception never leaves into the game: one thrown by the load work (a file it cannot read, memory) is logged, and the
	// steps before it keep what they did (Illuminated's OnDataLoaded)
	void OnDataLoaded()
	{
		try {
			LoadEverything();
		} catch (const std::exception& e) {
			SKSE::log::critical("the data-load work stopped part way: {} - the steps before it keep their work, the rest is not done this session",
				e.what());
		}
	}

	void Handle(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kPostLoad:
			InstallOthers();
			InstallCopyPaint();
			OfferToDevBench();
			RegisterArcBar();
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			OnDataLoaded();
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
			ApplyAll(kWhyGameLoaded);
			WatchBlocking();
			break;
		case SKSE::MessagingInterface::kNewGame:
			ApplyAll(kWhyNewGame);
			WatchBlocking();
			break;
		default:
			break;
		}
	}

	// an exception never leaves into the game from any message either (kDataLoaded's work has its own, more telling, guard)
	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		if (a_msg) {
			Guarded("SKSE message", [a_msg]() { Handle(a_msg); });
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	if (!RuntimeGuard::RightGame("Dynamic Wards", "Dynamic Wards", "Dynamic Wards - VR")) {
		return false;
	}
	// the version the DLL declares (xmake.lua set_version), never a typed number - 3.1's log still said "3.0"
	const auto* decl = SKSE::PluginDeclaration::GetSingleton();
	const auto  ver = decl ? std::format("{}.{}", decl->GetVersion().major(), decl->GetVersion().minor()) : std::string("?");
	SKSE::log::info("Dynamic Wards {} - any color for any ward: one set of meshes, colored in memory; no plugin of its own", ver);
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	if (auto* papyrus = SKSE::GetPapyrusInterface()) {
		papyrus->Register(RegisterPapyrus);
	}
	return true;
}
