// Dynamic Wards - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.

#pragma once

#include "PCH.h"
#include "SettingsText.h"
#include "WardMath.h"

namespace Plugin
{
	template <class T>
	T* NewForm()
	{
		auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<T>();
		return factory ? factory->Create() : nullptr;
	}

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

	// Wards.cpp: which effects are wards, and what each wears
	void                                             MakeArt();                       // data load: each row's neutral art, in memory
	void                                             FindWards();                     // data load: every ward, by the 1.0 table and by what it is
	void                                             ApplyAll(const char* a_why);     // every dressed ward to what the settings ask for (main thread)
	void                                             CheckUnlock(const char* a_why);  // re-reads the player's skill or perk; re-applies when it changed
	bool                                             Has360Ward();                    // the 360 dome can be worn: 360 Ward.esp loaded, 360 Ward Universal Patch SKSE present AND our 360 domes installed
	bool                                             Missing360Patch();               // 360 Ward.esp loaded, its Universal Patch not
	bool                                             Missing360Meshes();              // 360 Ward and its patch loaded, but our 360 domes not installed (the installer puts them in only with 360 Ward.esp in the plugin list)
	bool                                             LightPlacerLoaded();
	bool                                             Unlocked360();
	bool                                             CrusaderAvailable();
	bool                                             ShieldFound(std::size_t a_row);  // a shield in this load order raises that row's ward
	std::size_t                                      DressedCount();
	std::size_t                                      FoundCount();
	std::string                                      WardsReport();
	RE::BGSReferenceEffect*                          FlashFor(RE::EffectSetting* a_effect);
	bool                                             PreviewRow(std::size_t a_row, float a_seconds);  // devbench: the dome this row wears now, on the player (the normal dome of a rank row with worn copies: its looping copy - the cast dome idles as plain art; the 360 dome loops already)
	std::size_t                                      RowOfModel(std::string_view a_model);            // kRows when it is not one of ours
	std::size_t                                      AlwaysOnWardRow(RE::Actor* a_actor);             // the row of an always-on ward of ours active on the actor (a held staff's), else kRows
	std::size_t                                      WardRowOf(const RE::EffectSetting* a_effect);    // the row a ward of ours wears, kRows when it is not one
	void                                             WatchBlocking();                                 // main thread, at a load and every 2 s: the shield's ward at once when the player blocks
	std::vector<std::pair<std::string, std::string>> WardPerks();
	std::vector<std::pair<std::string, std::size_t>> FoundMods();

	// Settings.cpp: DynamicWards.ini, written by the menu
	void    LoadSettings();
	void    SaveSettings();
	RowMode RowModeOf(std::size_t a_row);
	Color   RowCustom(std::size_t a_row);
	void    SetRow(std::size_t a_row, RowMode a_mode, Color a_color);
	Color   LadderColor();
	void    SetLadderColor(Color a_color);
	int     LadderStages();
	void    SetLadderStages(int a_stages);
	bool    LadderReversed();
	void    SetLadderReversed(bool a_on);
	int     Opacity();  // kOpacityMin-100, percent: the ward art AND its lights (not the casting art - CastingGlow)
	void    SetOpacity(int a_percent);
	int     Transparency();  // 0-kTransparencyMax, percent: how much of the domes' facing fill is taken away (0 = as built)
	void    SetTransparency(int a_percent);
	int     Brightness();  // kBrightnessMin-kBrightnessMax, percent: the wards' glow AND their lights (100 = as built)
	void    SetBrightness(int a_percent);
	float   LightDim();  // what a dome / hit light's strength is multiplied by: opacity x brightness
	// the casting art (the ward in the hands) and its light have their own slider and nothing else touches them - his rule
	// 2026-10-05: "opacity should not be affecting casting art brightness, neither should ward brightness ... make it the sole
	// light handler for casting art, opacity and ward brightness should only be affecting the wards themselves"
	int                  CastingGlow();  // the menu's Hand Brightness (ini key CastingGlow), kBrightnessMin-kBrightnessMax, percent (100 = as built)
	void                 SetCastingGlow(int a_percent);
	float                HandDim();  // what a hand light's strength (and the hand art's glow) is multiplied by: Hand Brightness alone
	bool                 Dome360();
	void                 SetDome360(bool a_on);
	bool                 WardLightOn();
	void                 SetWardLightOn(bool a_on);
	bool                 ColoredLightsOn();
	void                 SetColoredLightsOn(bool a_on);
	Unlock               UnlockRule();
	void                 SetUnlockRule(Unlock a_rule);
	std::string          UnlockPerk();
	void                 SetUnlockPerk(std::string a_perk);
	bool                 CrusaderOn();
	void                 SetCrusaderOn(bool a_on);
	bool                 ShieldWardOn(std::size_t a_row);  // a kShieldRows row: off = the shield raises no ward at all
	ShieldDome           ShieldDomeOf(std::size_t a_row);  // a kShieldRows row: off, the normal dome or the 360 dome
	bool                 Any360Wanted();                   // the Dome pick or any shield's pick asks for the 360 dome (the unlock matters)
	void                 SetShieldDome(std::size_t a_row, ShieldDome a_dome);
	bool                 KeepStrangeRunes();  // Strange Runes loaded: its ward look stays (off: ours goes back when it swaps the art)
	void                 SetKeepStrangeRunes(bool a_on);
	bool                 StrangeRunesLoaded();
	void                 WatchArt();  // every 2 s: a ward whose art another mod swapped is dressed again (main.cpp starts it)
	bool                 EveryWard();
	void                 SetEveryWard(bool a_on);
	bool                 ModOn(std::string_view a_plugin);
	void                 SetModOn(std::string_view a_plugin, bool a_on);
	std::optional<Color> RowColor(std::size_t a_row);  // what the row wears now; nothing = Vanilla
	// Strange Wards (a vanilla ward replacer, Nexus 139921) installed: the five rank rows act as Vanilla, so its dome and the
	// game's hand art (which reads the gradient its pick ships) show, and the game's ward hand light takes its pick's color
	void NoteStrangeWards();  // data load, main thread, after MakeHandLights
	bool StrangeWardsInstalled();
	int  StopPercent(std::size_t a_rank);  // where a rank sits on the ladder, 0 white - 100 the color

	// Colors.cpp: the neutral art takes each row's color in memory - its palettes on the graphics card, its glow on the
	// cached model every copy is cloned from
	void RegisterRowModels(std::size_t a_row, std::vector<std::string> a_models);  // data load, from MakeArt
	bool ApplyColors(bool a_quiet = false);                                        // main thread; true when a row changed
	void LiveRecolor();                                                            // the menu, while a color is dragged: the wards (and a dome on screen) follow it, throttled
	// the rows whose ward-in-the-hand look (its color or Hand Brightness) changed since the last call: a ward already in a hand
	// is re-equipped only for those (ApplyAll's hand refresh); every row's look is marked as seen by the call
	std::array<bool, kRows> TakeHandLookChanges();
	std::string             ColorsReport();

	// every effect-shader block under a_root that has a material, in scene-graph order - one walk for every reader, so a
	// master's blocks and a live copy's pair up by index
	template <class F>
	void EachEffectBlock(RE::NiAVObject* a_root, F&& a_fn)
	{
		RE::BSVisit::TraverseScenegraphGeometries(a_root, [&](RE::BSGeometry* a_geometry) {
			auto* raw = a_geometry ? a_geometry->GetGeometryRuntimeData().shaderProperty.get() : nullptr;
			auto* prop = raw ? netimmerse_cast<RE::BSEffectShaderProperty*>(raw) : nullptr;
			if (auto* mat = prop ? static_cast<RE::BSEffectShaderMaterial*>(prop->GetMaterial()) : nullptr) {
				a_fn(a_geometry, prop, mat);
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});
	}

	// Lighting.cpp: the lighting picked in the installer, and the lights this plugin makes
	enum class Lighting : int
	{
		kShaders = 0,  // Community Shaders (with or without Effects 11) or none: this plugin makes the lights
		kEnb = 1,      // an ENB light inside each ward mesh, and (since 2026-10-06) this plugin's lights as on Vanilla
		kVanilla = 2,  // this plugin makes the lights, in the game's own lighting
	};
	void               ReadLighting();  // data load, before MakeArt
	Lighting           LightingPick();
	void               SetLightingPick(Lighting a_pick);  // devbench only, never saved
	bool               MeshLights();                      // ENB: the meshes carry an ENB light sprite as well
	bool               OwnLights();                       // this plugin makes the hand and dome lights (every pick since 2026-10-06)
	bool               Effects11();                       // Community Shaders' Effects 11 is installed
	bool               HandLight1st();                    // the first-person hand light: every pick (his order 2026-10-05)
	bool               InverseSquare();                   // Community Shaders' inverse square lighting is installed, or CommunityShaders.dll is loaded (3.4)
	void               MakeHandLights();                  // data load: one copy of the game's ward light per row
	void               ColorHandLights();                 // after a color change: each row's light in its color
	RE::TESObjectLIGH* HandLightFor(std::size_t a_row);
	bool               IsOurHandLight(const RE::TESObjectLIGH* a_light);
	std::string        LightingReport();
	std::string        ModelKey(std::string_view a_model);        // lower case, back slashes, under the meshes folder
	RE::NiColor        LightColor(Color a_color, bool a_linear);  // 0-1, sRGB or linear

	// DomeLights.cpp: the colored light on each dome, and the hand lights (every pick)
	enum class DomeMode : int
	{
		kAuto = 0,
		kOn = 1,  // devbench only
		kOff = 2,
	};
	void        StartDomeLights();  // data load, after MakeArt
	void        SetDomeMode(DomeMode a_mode);
	std::string DomeLightsReport();

	void RegisterArcBar();  // Arcs.cpp, SKSE's post-load (Reticle Arcs takes add-on bars only then): the ward's strength as its bar

	void RegisterMenu();
	void FlushHeldColor();  // Menu.cpp: a menu closed - a color still held (changed live) is saved and applied
	bool RegisterPapyrus(RE::BSScript::IVirtualMachine* a_vm);
	void OfferToDevBench();
	void WardsApplied(const char* a_why, std::size_t a_found, std::size_t a_dressed);  // DevBench.cpp: the dynamicwards.applied event

	// Text.cpp (the game-free text helpers - Lower, SameText, HexColor, ParseColor, Mix ... - are in WardMath.h)
	std::string  Where(const RE::TESForm* a_form);
	std::string  JsonEscape(std::string_view a_text);
	RE::TESForm* ResolveForm(std::string_view a_text);  // "0x1540E~Dawnguard.esm"
	std::string  FormText(const RE::TESForm* a_form);

	// Runs a_work and lets no exception out: inside a game callback, a task or one of our threads one would end the game (his
	// C++ rule; Illuminated's LightKit::Guarded, the same code). The first failure at each place is logged, later ones there
	// are skipped quietly. One flag per call site - each a_work is a lambda of its own type, so each call site has its own
	// instance of this function.
	template <class F>
	inline void Guarded(const char* a_where, F&& a_work) noexcept
	{
		static std::atomic<bool> told{ false };
		try {
			std::forward<F>(a_work)();
		} catch (const std::exception& e) {
			if (!told.exchange(true)) {
				SKSE::log::error("{}: stopped by an error ({}); later ones here are skipped quietly", a_where, e.what());
			}
		} catch (...) {
			if (!told.exchange(true)) {
				SKSE::log::error("{}: stopped by an unknown error; later ones here are skipped quietly", a_where);
			}
		}
	}

	// a_job onto the game's main thread (the menu draws off it; DevBench and our two threads call from their own), run there
	// inside Guarded under a_where - one flag per caller, as each caller's job is its own type. False when nothing was queued
	// (no task interface, or no memory for the task): a caller that raised a "queued" flag lowers it again, or it would stay up,
	// and a caller that reports to someone (DevBench) says it did not happen - so the result is never dropped
	template <class F>
	[[nodiscard]] bool Later(const char* a_where, F&& a_job) noexcept
	{
		try {
			if (auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddTask([a_where, job = std::forward<F>(a_job)]() { Guarded(a_where, job); });
				return true;
			}
		} catch (...) {
		}
		return false;
	}
}
