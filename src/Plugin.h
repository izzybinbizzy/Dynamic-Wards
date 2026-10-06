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

	// a row: five ranks, three worn shield wards, the vampire ward - one set of neutral meshes each (3.0)
	inline constexpr std::string_view kTokens[] = { "Novice", "Apprentice", "Adept", "Expert", "Master", "Spellbreaker",
		"Crusader", "Reman", "Vampire" };
	inline constexpr std::size_t kRows = std::size(kTokens);
	inline constexpr std::size_t kRanks = 5;

	// colors are 0xRRGGBB; the ladder runs from white to one color
	using Color = std::uint32_t;
	inline constexpr Color kWhite = 0xFFFFFF;
	struct Preset
	{
		const char* name;
		Color       color;
	};
	// the five colors of 2.x, as quick picks (and how a 2.x settings file's numbers read)
	inline constexpr Preset kPresets[] = { { "Blue", 0x2468FF }, { "Red", 0xFF3648 }, { "Gold", 0xFFBE5A }, { "Green", 0x2DC846 },
		{ "Purple", 0x9646FF }, { "White", kWhite } };
	// what a rankless row wears on its default
	inline constexpr Color kRowDefault[] = { 0, 0, 0, 0, 0, 0xFFBE5A, kWhite, kWhite, 0xFF3648 };
	// the mod each rankless row comes from, named beside it in the menu (his ask 2026-10-05); a rank has none
	inline constexpr const char* kRowFrom[] = { "", "", "", "", "", "Skyrim", "Knights of the Nine", "Legacy of the Dragonborn", "Better Vampires" };

	// a whole look at once, picked on the Colors page above the colors (his ask 2026-10-05). Each sets the ladder color and
	// every row's color - colors only: the stages and Reverse stay as the player set them (his rule 2026-10-05: "preset only
	// changes colors nothing else").
	struct Scheme
	{
		const char* name;
		const char* tip;
		Color       ladder;
		Color       rows[9];  // 0 = the row's default (a rank follows the ladder)
	};
	// his ask 2026-10-05: "as diverse as possible to avoid intersecting colors". The ladder hues sit about 30 degrees apart round
	// the wheel (crimson 350, ember 25, gold 45, green 135, teal 170, frost 190, the game's blue 220, violet 265, magenta 315);
	// Vanilla is the game's own ward blue (wardgen WARD_BLUE) on every ward, flat, the way the base game shows them.
	inline constexpr Scheme kSchemes[] = {
		{ "Vanilla", "Every ward in the game's own ward blue.", 0x2468FF,
			{ 0x2468FF, 0x2468FF, 0x2468FF, 0x2468FF, 0x2468FF, 0x2468FF, 0x2468FF, 0x2468FF, 0x2468FF } },
		{ "Daedric", "White to blood crimson.", 0xD0102E, {} },
		{ "Ember", "White to burning orange.", 0xFF6A10, {} },
		{ "Aedric", "White to sun gold.", 0xFFC420, {} },
		{ "Verdant", "White to leaf green.", 0x2ED452, {} },
		{ "Psijic", "White to deep teal.", 0x00D2B0, {} },
		{ "Frost", "White to glacier cyan.", 0x40DCFF, {} },
		{ "Nightingale", "White to shadow violet.", 0x7A3CFF, {} },
		{ "Sanguine", "White to wild magenta.", 0xFF2EC4, {} },
		{ "Schools", "Every rank its own color: blue, gold, white, violet, crimson.", 0x2468FF,
			{ 0x2468FF, 0xFFC420, 0xFFFFFF, 0x7A3CFF, 0xD0102E, 0, 0, 0, 0 } },
		{ "Rainbow", "Every rank a step round the rainbow: crimson, gold, green, blue, violet.", 0x2468FF,
			{ 0xD0102E, 0xFFC420, 0x2ED452, 0x2468FF, 0x7A3CFF, 0, 0, 0, 0 } },
	};
	void ApplyScheme(const Scheme& a_scheme);  // Settings.cpp; the caller saves and applies

	// a ladder's stops, in percent of the way from white to its color
	inline constexpr std::array<int, 3> kStages3{ 0, 55, 100 };
	inline constexpr std::array<int, 4> kStages4{ 0, 33, 67, 100 };
	inline constexpr std::array<int, 5> kStages5{ 0, 25, 50, 75, 100 };
	inline constexpr int                kMinStages = 3;
	inline constexpr int                kMaxStages = 5;

	enum class RowMode : int
	{
		kDefault = 0,  // a rank follows the ladder; a rankless row wears its own default
		kCustom = 1,   // a color of its own
		kVanilla = 2,  // the game's own (or another mod's) art
	};

	enum class Unlock : int
	{
		kAlways = 0,
		kSkill50 = 1,
		kSkill75 = 2,
		kSkill100 = 3,
		kPerk = 4,
	};

	// Wards.cpp: which effects are wards, and what each wears
	void        MakeArt();                       // data load: each row's neutral art, in memory
	void        FindWards();                     // data load: every ward, by the 1.0 table and by what it is
	void        ApplyAll(const char* a_why);     // every dressed ward to what the settings ask for (main thread)
	void        CheckUnlock(const char* a_why);  // re-reads the player's skill or perk; re-applies when it changed
	bool        Has360Ward();       // 360 Ward.esp loaded and 360 Ward Universal Patch SKSE present
	bool        Missing360Patch();  // 360 Ward.esp loaded, its Universal Patch not
	bool        LightPlacerLoaded();
	bool        Unlocked360();
	bool        CrusaderAvailable();
	std::size_t DressedCount();
	std::size_t FoundCount();
	std::string WardsReport();
	RE::BGSReferenceEffect* FlashFor(RE::EffectSetting* a_effect);
	bool        PreviewRow(std::size_t a_row, float a_seconds);  // devbench: the dome this row wears now, on the player
	std::size_t RowOfModel(std::string_view a_model);            // kRows when it is not one of ours
	std::vector<std::pair<std::string, std::string>> WardPerks();
	std::vector<std::pair<std::string, std::size_t>> FoundMods();

	// Settings.cpp: DynamicWards.ini, written by the menu
	void        LoadSettings();
	void        SaveSettings();
	RowMode     RowModeOf(std::size_t a_row);
	Color       RowCustom(std::size_t a_row);
	void        SetRow(std::size_t a_row, RowMode a_mode, Color a_color);
	Color       LadderColor();
	void        SetLadderColor(Color a_color);
	int         LadderStages();
	void        SetLadderStages(int a_stages);
	bool        LadderReversed();
	void        SetLadderReversed(bool a_on);
	inline constexpr int kOpacityMin = 25;  // lower reads as nearly invisible
	int         Opacity();  // kOpacityMin-100, percent: the ward art AND its lights (not the casting art - CastingGlow)
	void        SetOpacity(int a_percent);
	inline constexpr int kTransparencyDefault = 25;  // his default, 2026-10-03
	inline constexpr int kTransparencyMax = 90;  // the dome's fill facing you keeps at least a tenth: the rim alone reads as no ward
	int         Transparency();  // 0-kTransparencyMax, percent: how much of the domes' facing fill is taken away (0 = as built)
	void        SetTransparency(int a_percent);
	inline constexpr int kBrightnessMin = 25;   // the range is centred on 100 (as built): the slider starts in the middle (his call)
	inline constexpr int kBrightnessMax = 175;
	int         Brightness();  // kBrightnessMin-kBrightnessMax, percent: the wards' glow AND their lights (100 = as built)
	void        SetBrightness(int a_percent);
	float       LightDim();    // what a dome / hit light's strength is multiplied by: opacity x brightness
	// the casting art (the ward in the hands) and its light have their own slider and nothing else touches them - his rule
	// 2026-10-05: "opacity should not be affecting casting art brightness, neither should ward brightness ... make it the sole
	// light handler for casting art, opacity and ward brightness should only be affecting the wards themselves"
	int         CastingGlow();  // kBrightnessMin-kBrightnessMax, percent (100 = as built)
	void        SetCastingGlow(int a_percent);
	float       HandDim();      // what a hand light's strength (and the hand art's glow) is multiplied by: casting glow alone
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
	bool        KeepStrangeRunes();  // Strange Runes loaded: its ward look stays (off: ours goes back when it swaps the art)
	void        SetKeepStrangeRunes(bool a_on);
	bool        StrangeRunesLoaded();
	void        WatchArt();  // every 2 s: a ward whose art another mod swapped is dressed again (main.cpp starts it)
	bool        EveryWard();
	void        SetEveryWard(bool a_on);
	bool        ModOn(std::string_view a_plugin);
	void        SetModOn(std::string_view a_plugin, bool a_on);
	std::optional<Color> RowColor(std::size_t a_row);  // what the row wears now; nothing = Vanilla
	int         StopPercent(std::size_t a_rank);       // where a rank sits on the ladder, 0 white - 100 the color

	// Colors.cpp: the neutral art takes each row's color in memory - its palettes on the graphics card, its glow on the
	// cached model every copy is cloned from
	void        RegisterRowModels(std::size_t a_row, std::vector<std::string> a_models);  // data load, from MakeArt
	bool        ApplyColors();                                                            // main thread; true when a row changed
	std::string ColorsReport();

	// Lighting.cpp: the lighting picked in the installer, and the lights this plugin makes
	enum class Lighting : int
	{
		kShaders = 0,  // Community Shaders (with or without Effects 11) or none: this plugin makes the lights
		kEnb = 1,      // an ENB light inside each ward mesh
		kVanilla = 2,  // this plugin makes the lights, in the game's own lighting
	};
	void               ReadLighting();  // data load, before MakeArt
	Lighting           LightingPick();
	void               SetLightingPick(Lighting a_pick);  // devbench only, never saved
	bool               MeshLights();                      // ENB: the light is in the mesh, the game's ward light goes
	bool               OwnLights();                       // this plugin makes the hand and dome lights
	bool               Effects11();                       // Community Shaders' Effects 11 is installed
	bool               HandLight1st();                    // the first-person hand light: every pick (his order 2026-10-05)
	bool               InverseSquare();                   // Community Shaders' inverse square lighting is installed
	void               MakeHandLights();                  // data load: one copy of the game's ward light per row
	void               ColorHandLights();                 // after a color change: each row's light in its color
	RE::TESObjectLIGH* HandLightFor(std::size_t a_row);
	bool               IsOurHandLight(const RE::TESObjectLIGH* a_light);
	std::string        LightingReport();
	std::string        ModelKey(std::string_view a_model);  // lower case, back slashes, under the meshes folder
	RE::NiColor        LightColor(Color a_color, bool a_linear);  // 0-1, sRGB or linear

	// DomeLights.cpp: the colored light on each dome, and (ENB, or Effects 11) the first-person hand light
	enum class DomeMode : int
	{
		kAuto = 0,
		kOn = 1,  // devbench only
		kOff = 2,
	};
	void        StartDomeLights();  // data load, after MakeArt
	void        SetDomeMode(DomeMode a_mode);
	std::string DomeLightsReport();

	void RegisterMenu();
	bool RegisterPapyrus(RE::BSScript::IVirtualMachine* a_vm);
	void OfferToDevBench();
	void WardsApplied(const char* a_why, std::size_t a_found, std::size_t a_dressed);  // DevBench.cpp: the dynamicwards.applied event

	std::string  Lower(std::string_view a_text);
	bool         Contains(std::string_view a_haystack, std::string_view a_needle);
	std::string  Where(const RE::TESForm* a_form);
	std::string  JsonEscape(std::string_view a_text);
	RE::TESForm* ResolveForm(std::string_view a_text);  // "0x1540E~Dawnguard.esm"
	std::string  FormText(const RE::TESForm* a_form);
	std::string  HexColor(Color a_color);               // "2468FF"
	std::optional<Color> ParseColor(std::string_view a_text);
	Color        Mix(Color a_from, Color a_to, int a_percent);
	void         Later(std::function<void()> a_job);  // onto the game's main thread (the menu draws off it)
}
