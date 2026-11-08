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
//   SKSEMenuFramework.h, DevBenchAPI.*  those mods' own files; DevBenchGlue.h, MenuStyle.h, Translation.h, RuntimeGuard.h and
//                      FrameCost.h (what each pass costs) are copies of Tools\Shared Plugin Code, word for word
//   tests\testwards.cpp the off-game tests of WardMath.h and SettingsText.h, built and run by every SE build of the plugin

#include "Plugin.h"
#include "RuntimeGuard.h"
#include "Translation.h"

using namespace Plugin;

namespace
{
	// a loading screen re-applies; any other menu closing looks for art another mod swapped (an MCM) and re-checks the 360 unlock
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
					if (!Later(kWhyLoadingScreen, []() {
							ApplyAll(kWhyLoadingScreen);
							HangLightsAgain();
						}) &&
						!toldLoad.exchange(true)) {
						SKSE::log::warn("loading screen closed: the wards could not be re-applied (no main-thread task); later ones are not logged");
					}
				} else if (name != RE::HUDMenu::MENU_NAME && name != RE::CursorMenu::MENU_NAME) {
					FlushHeldColor();
					if (!QueueArtCheck("a menu closed") && !toldMenu.exchange(true)) {
						SKSE::log::warn("a menu closed: the art and the 360 unlock could not be re-checked (no main-thread task); later ones are not logged");
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

	// The game's own moments, instead of a watch on a timer (rule file 2 rule 19c). Anyone's SKSE actions (a draw, a sheathe, a spell cast or
	// fired), equip, 3D load, 3D rebuilt, death, combat or magic effect names him for the ward lights - a drawn staff's always-on ward has no art
	// of ours to wake them, and one equipped while the hands are out sends no draw - and her draw or cast looks for art another mod swapped. Her
	// Restoration rising re-checks the 360 unlock. A sink only notes: the main thread reads the actor
	struct GameEvents :
		RE::BSTEventSink<SKSE::ActionEvent>,
		RE::BSTEventSink<RE::SkillIncrease::Event>,
		RE::BSTEventSink<RE::TESObjectLoadedEvent>,
		RE::BSTEventSink<RE::TESEquipEvent>,
		RE::BSTEventSink<RE::TESDeathEvent>,
		RE::BSTEventSink<RE::TESCombatEvent>,
		RE::BSTEventSink<RE::TESMagicEffectApplyEvent>,
		RE::BSTEventSink<SKSE::NiNodeUpdateEvent>,
		RE::BSTEventSink<RE::TESSwitchRaceCompleteEvent>
	{
		RE::BSEventNotifyControl ProcessEvent(const SKSE::ActionEvent* a_event, RE::BSTEventSource<SKSE::ActionEvent>*) override
		{
			// the action's type is a plain value, not a set of bits: compared, never tested with any()
			using T = SKSE::ActionEvent::Type;
			const auto type = a_event ? a_event->type.get() : T::kWeaponSwing;
			const bool hands = type == T::kBeginDraw || type == T::kEndDraw || type == T::kBeginSheathe || type == T::kEndSheathe;
			if ((hands || type == T::kSpellCast || type == T::kSpellFire) && a_event->actor) {
				NoteHands(a_event->actor->GetFormID());
				if ((type == T::kBeginDraw || type == T::kSpellCast) && a_event->actor->IsPlayerRef()) {
					Guarded("her action", []() { (void)QueueArtCheck("as she drew or cast"); });
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::SkillIncrease::Event* a_event, RE::BSTEventSource<RE::SkillIncrease::Event>*) override
		{
			if (a_event && a_event->actorValue == RE::ActorValue::kRestoration) {
				Guarded("her skill rose", []() { (void)Later("her Restoration rose", []() { CheckUnlock("her Restoration rose"); }); });
			}
			return RE::BSEventNotifyControl::kContinue;
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::TESObjectLoadedEvent* a_event, RE::BSTEventSource<RE::TESObjectLoadedEvent>*) override
		{
			// an actor with a script or in an alias that arrives with a ward readied (the game sends this for no other);
			// only actors, so a cell's load names no door or plant. Anyone's leaving: DomeLights.cpp BodySet
			if (a_event && RE::TESForm::LookupByID<RE::Actor>(a_event->formID)) {
				NoteHands(a_event->formID);
			}
			return RE::BSEventNotifyControl::kContinue;
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* a_event, RE::BSTEventSource<RE::TESEquipEvent>*) override
		{
			if (a_event && a_event->actor) {
				NoteHands(a_event->actor->GetFormID());
				// a spell or staff equipped with her hands out copies its art at once: art a script swapped since is dressed first
				if (a_event->equipped && a_event->actor->IsPlayerRef()) {
					const auto* form = RE::TESForm::LookupByID(a_event->baseObject);
					const auto* weapon = form ? form->As<RE::TESObjectWEAP>() : nullptr;
					if (form && (form->Is(RE::FormType::Spell) || (weapon && weapon->IsStaff()))) {
						Guarded("her equip", []() { (void)QueueArtCheck("as she equipped"); });
					}
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::TESDeathEvent* a_event, RE::BSTEventSource<RE::TESDeathEvent>*) override
		{
			if (a_event && a_event->actorDying) {
				NoteHands(a_event->actorDying->GetFormID());
			}
			return RE::BSEventNotifyControl::kContinue;
		}

		// his look may follow who he fights (Others.cpp TheirSlot)
		RE::BSEventNotifyControl ProcessEvent(const RE::TESCombatEvent* a_event, RE::BSTEventSource<RE::TESCombatEvent>*) override
		{
			if (a_event && a_event->actor) {
				NoteHands(a_event->actor->GetFormID());
			}
			return RE::BSEventNotifyControl::kContinue;
		}

		// an always-on ward given while a staff is drawn. A held ward's own effect is applied again and again: only a constant effect names him
		RE::BSEventNotifyControl ProcessEvent(const RE::TESMagicEffectApplyEvent* a_event, RE::BSTEventSource<RE::TESMagicEffectApplyEvent>*) override
		{
			const auto* effect = a_event && a_event->target ? RE::TESForm::LookupByID<RE::EffectSetting>(a_event->magicEffect) : nullptr;
			if (effect && effect->data.castingType == RE::MagicSystem::CastingType::kConstantEffect) {
				NoteHands(a_event->target->GetFormID());
			}
			return RE::BSEventNotifyControl::kContinue;
		}

		// a 3D rebuilt: the lights follow the new body
		RE::BSEventNotifyControl ProcessEvent(const SKSE::NiNodeUpdateEvent* a_event, RE::BSTEventSource<SKSE::NiNodeUpdateEvent>*) override
		{
			if (a_event && a_event->reference) {
				NoteHands(a_event->reference->GetFormID());
			}
			return RE::BSEventNotifyControl::kContinue;
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::TESSwitchRaceCompleteEvent* a_event, RE::BSTEventSource<RE::TESSwitchRaceCompleteEvent>*) override
		{
			if (a_event && a_event->subject) {
				NoteHands(a_event->subject->GetFormID());
			}
			return RE::BSEventNotifyControl::kContinue;
		}

		static GameEvents* Get()
		{
			static GameEvents e;
			return &e;
		}

		static void Register()
		{
			auto* events = Get();
			if (auto* actions = SKSE::GetActionEventSource()) {
				actions->AddEventSink<SKSE::ActionEvent>(events);
			}
			if (auto* nodes = SKSE::GetNiNodeUpdateEventSource()) {
				nodes->AddEventSink<SKSE::NiNodeUpdateEvent>(events);
			}
			if (auto* skills = RE::SkillIncrease::GetEventSource()) {
				skills->AddEventSink<RE::SkillIncrease::Event>(events);
			}
			if (auto* scripts = RE::ScriptEventSourceHolder::GetSingleton()) {
				scripts->AddEventSink<RE::TESObjectLoadedEvent>(events);
				scripts->AddEventSink<RE::TESEquipEvent>(events);
				scripts->AddEventSink<RE::TESDeathEvent>(events);
				scripts->AddEventSink<RE::TESCombatEvent>(events);
				scripts->AddEventSink<RE::TESMagicEffectApplyEvent>(events);
				scripts->AddEventSink<RE::TESSwitchRaceCompleteEvent>(events);
			}
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
		gTaskThread = std::this_thread::get_id();  // the game's main thread, which runs the data-load message and SKSE's tasks
		ApplyAll(kWhyDataLoaded);
		StartDomeLights();
		GameEvents::Register();
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
			InstallAnimationHook();
			InstallBodyWatch();
			InstallCopyPaint();
			OfferToDevBench();
			RegisterArcBar();
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			OnDataLoaded();
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
			FrameCost::Reset();  // the worst times start again with each game
			ApplyAll(kWhyGameLoaded);
			WakeLights();
			break;
		case SKSE::MessagingInterface::kNewGame:
			FrameCost::Reset();
			ApplyAll(kWhyNewGame);
			WakeLights();
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
	SKSE::InitInfo info{};
	info.trampoline = true;  // the hand art hook (Others.cpp)
	info.trampolineSize = 14;
	SKSE::Init(a_skse, info);
	if (!RuntimeGuard::RightGame("Dynamic Wards", "Dynamic Wards", "Dynamic Wards - VR")) {
		return true;
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
