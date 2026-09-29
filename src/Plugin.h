// Dynamic Wards - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.

#pragma once

#include "PCH.h"

namespace Plugin
{
	template <class T>
	T* NewForm()
	{
		auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<T>();
		return factory ? factory->Create() : nullptr;
	}

	// a row: five ranks, three worn shield wards, the vampire ward
	inline constexpr std::string_view kTokens[] = { "Novice", "Apprentice", "Adept", "Expert", "Master", "Spellbreaker",
		"Crusader", "Reman", "Vampire" };
	inline constexpr std::size_t      kRows = std::size(kTokens);
	inline constexpr std::size_t      kRanks = 5;

	// the ladders run white to this color; the colors a row may pick (wardgen LADDERS2 / PICK_COLORS2)
	inline constexpr std::string_view kLadderNames[] = { "Blue", "Red", "Gold", "Green", "Purple" };
	inline constexpr std::size_t      kLadders = std::size(kLadderNames);
	inline constexpr std::string_view kColorNames[] = { "Blue", "Red", "Gold", "Green", "White" };  // Pick 1-5; Purple is Pick 7

	// a ladder's stops, in percent of the way from white to its color (wardgen STAGES2 - the same numbers)
	inline constexpr std::array<int, 3> kStages3{ 0, 55, 100 };
	inline constexpr std::array<int, 4> kStages4{ 0, 33, 67, 100 };
	inline constexpr std::array<int, 5> kStages5{ 0, 25, 50, 75, 100 };
	inline constexpr int                kMinStages = 3;
	inline constexpr int                kMaxStages = 5;
	// what a rankless row wears on its default (wardgen shield colors)
	inline constexpr std::string_view kRowDefault[] = { "", "", "", "", "", "Gold", "White", "White", "Red" };

	enum class Pick : int
	{
		kDefault = 0,  // a rank follows the ladder; a rankless row wears its own default
		kBlue,
		kRed,
		kGold,
		kGreen,
		kWhite,
		kVanilla,
		kPurple,  // after Vanilla, so a saved 6 still means Vanilla
	};

	enum class Unlock : int
	{
		kAlways = 0,
		kSkill50 = 1,
		kSkill75 = 2,
		kSkill100 = 3,
		kPerk = 4,
	};

	void        MakeArt();                       // data load: every installed look's art, in memory
	void        FindWards();                     // data load: every ward, by the 1.0 table and by what it is
	void        ApplyAll(const char* a_why);     // every dressed ward to what the settings ask for (main thread)
	void        CheckUnlock(const char* a_why);  // re-reads the player's skill or perk; re-applies when it changed
	bool        Has360Ward();                    // 360 Ward.esp is loaded, so the 360 dome and its flash can be used
	bool        LightPlacerLoaded();
	bool        Unlocked360();
	bool        CrusaderAvailable();             // Legacy, its Creation Club hub and Knights of the Nine are all loaded
	std::size_t DressedCount();
	std::size_t FoundCount();
	std::string WardsReport();
	RE::BGSReferenceEffect* FlashFor(RE::EffectSetting* a_effect);
	bool        PreviewRow(std::size_t a_row, float a_seconds);  // devbench: the dome this row wears now, on the player
	std::vector<std::pair<std::string, std::string>> WardPerks();  // (0xID~Plugin, name) - perks whose name says ward

	void        LoadSettings();
	void        SaveSettings();
	Pick        RowPick(std::size_t a_row);
	void        SetRowPick(std::size_t a_row, Pick a_pick);
	int         Ladder();
	void        SetLadder(int a_ladder);
	int         LadderStages();  // 3-5
	void        SetLadderStages(int a_stages);
	bool        LadderReversed();  // the ladder's color at Lesser, white at the top
	void        SetLadderReversed(bool a_on);
	bool        Dome360();
	void        SetDome360(bool a_on);
	bool        WardLightOn();
	void        SetWardLightOn(bool a_on);
	bool        ColoredLightsOn();
	void        SetColoredLightsOn(bool a_on);
	Unlock      UnlockRule();
	void        SetUnlockRule(Unlock a_rule);
	std::string UnlockPerk();
	void        SetUnlockPerk(std::string a_perk);
	bool        CrusaderOn();
	void        SetCrusaderOn(bool a_on);
	bool        EveryWard();                     // the default for a mod with no line of its own (the old one switch)
	void        SetEveryWard(bool a_on);         // devbench `set=every`: every mod on the Compatibility page at once
	// the Compatibility page: one tick per mod
	bool        ModOn(std::string_view a_plugin);            // are the wards this plugin adds colored?
	void        SetModOn(std::string_view a_plugin, bool a_on);
	std::vector<std::pair<std::string, std::size_t>> FoundMods();  // Wards.cpp: every plugin with found wards, and how many

	// DomeLights.cpp: the dome's colored light where Light Placer is not loaded (RE::Light)
	enum class DomeMode : int
	{
		kAuto = 0,  // with RE::Light and no Light Placer
		kOn = 1,    // devbench only: hang them whatever is loaded (a test beside Light Placer)
		kOff = 2,
	};
	void        StartDomeLights();  // data load, after MakeArt
	void        SetDomeMode(DomeMode a_mode);
	std::string DomeLightsReport();

	void RegisterMenu();
	bool RegisterPapyrus(RE::BSScript::IVirtualMachine* a_vm);
	void OfferToDevBench();

	std::string  Lower(std::string_view a_text);
	bool         Contains(std::string_view a_haystack, std::string_view a_needle);
	std::string  Where(const RE::TESForm* a_form);
	std::string  JsonEscape(std::string_view a_text);
	RE::TESForm* ResolveForm(std::string_view a_text);  // "0x1540E~Dawnguard.esm"
	std::string  FormText(const RE::TESForm* a_form);
	void         Later(std::function<void()> a_job);    // onto the game's main thread (the menu draws off it)
}
