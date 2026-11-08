// Dynamic Wards - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// The SKSE Menu Framework pages (3.0): the ladder and every ward in any color - a drawn spectrum bar, color pickers,
// quick picks, live swatches of each rank - other casters' looks, the shields, the opacity slider, the dome and the lights.
// A color drag shows on the wards live (at most ~15 times a second) and is saved when the pick is let go.
// His layout: two pages - Colors (presets, the ladder, every ward with the rankless ones' mod named, other casters' looks, the
// shields, the factions) and Settings (Lights, Dome, Look - Transparency right below Opacity, greyed out without 360 Ward); a
// legend above every list of dropdowns; Strange Wards and Strange Runes as choices of the rank wards. Every change is saved
// the moment it is made (a slider or a color when it is let go), so closing the menu loses nothing - on the game's main
// thread, in one task with the re-dress (Changed).

#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX  // the build defines it too (xmake.lua)
#endif
#include "Plugin.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <span>

// SKSE Menu Framework's own header (theirs, MIT): its warnings are not ours, and ours are errors (xmake.lua) - under
// MSVC and under clang-cl, which checks every file too
#pragma warning(push, 0)
#if defined(__clang__)
#	pragma clang diagnostic push
#	pragma clang diagnostic ignored "-Weverything"
#endif
#include "SKSEMenuFramework.h"
#if defined(__clang__)
#	pragma clang diagnostic pop
#endif
#pragma warning(pop)
#include "Translation.h"
#include "MenuStyle.h"

namespace Plugin
{
	namespace
	{
		using namespace ImGuiMCP;
		using Translation::Fill;
		using Translation::T;
#define TR_MARK(x) x  // a line Translation.json carries, though it is not written inside T( ) here

		using MenuStyle::Header;
		using MenuStyle::kMuted;
		namespace Icon = MenuStyle::Icon;

		constexpr const char* kRowLabel[] = { TR_MARK("Lesser ward"), TR_MARK("Steadfast ward"), TR_MARK("Greater ward"),
			TR_MARK("Expert ward"), TR_MARK("Master ward"), TR_MARK("Spellbreaker"), TR_MARK("Shield of the Crusader"),
			TR_MARK("Visage of Reman"), TR_MARK("Vampire ward") };
		static_assert(std::size(kRowLabel) == kRows);

		// the other casters' looks on their pages, kLookTokens' order; a faction's hover says who is in it
		constexpr const char* kLookLabel[] = { TR_MARK("Followers"), TR_MARK("Enemies"), TR_MARK("Neutral"), TR_MARK("College of Winterhold"),
			TR_MARK("Thalmor"), TR_MARK("Penitus Oculatus"), TR_MARK("Vigilants of Stendarr"), TR_MARK("Imperial Legion"), TR_MARK("Stormcloaks"),
			TR_MARK("Miraak's Cultists"), TR_MARK("Dragon Priests and Liches"), TR_MARK("The Afflicted"), TR_MARK("Vampire Thralls"),
			TR_MARK("Forsworn"), TR_MARK("Hagravens and Witches"), TR_MARK("Necromancers"), TR_MARK("Dremora"), TR_MARK("Falmer"),
			TR_MARK("Draugr"), TR_MARK("Bandits"), TR_MARK("Hostile Mages") };
		static_assert(std::size(kLookLabel) == kLooks);
		constexpr const char* kLookTip[] = { TR_MARK("Anyone following you, always - whatever group they belong to."),
			TR_MARK("Casters fighting you, unless the Factions page picks a look for their group."),
			TR_MARK("Casters who neither follow nor fight you, unless the Factions page picks a look for their group."),
			TR_MARK("The College's mages: Colette, Drevis, Arniel, Brelyna and the rest."),
			TR_MARK("Thalmor Justiciars, the embassy's agents and the Thalmor wizards on the roads."),
			TR_MARK("The Emperor's agents and their battlemages."), TR_MARK("The Vigilants and their commanders."),
			TR_MARK("The Legion's battlemages and wizards."), TR_MARK("The Stormcloak mages."), TR_MARK("The masked cultists of Solstheim."),
			TR_MARK("Dragon priests and liches, Miraak's three acolytes too."), TR_MARK("Peryite's Afflicted and Orchendor."),
			TR_MARK("The mages who serve in vampire lairs."), TR_MARK("Forsworn shamans and Briarhearts."), TR_MARK("Hagravens, witches and hags."),
			TR_MARK("Necromancers and necromages."), TR_MARK("Dremora warlocks: churls, caitiffs and kynvals."),
			TR_MARK("Falmer shamans and loremasters."), TR_MARK("Draugr who cast, and the old Nords of the tombs."),
			TR_MARK("Bandit and Reaver mages."), TR_MARK("Mages, conjurers and battlemages in dungeons and camps.") };
		static_assert(std::size(kLookTip) == kLooks);

		// The menu draws on the RENDER thread: the file and the wards are the main thread's, so they follow in ONE task there -
		// changes made before it runs ride the same task (Illuminated's SetSetting); the setters above already hold the value
		void Changed()
		{
			static std::atomic_bool queued{ false };
			if (!LaterOnce(queued, "menu change", []() {
					SaveSettings();
					ApplyAll("menu");
				})) {
				SKSE::log::warn("menu: the change could not reach the main thread - it is kept for this session but not saved or applied");
			}
		}

		ImVec4 Vec(Color a_c, float a_alpha = 1.0f)
		{
			return { ((a_c >> 16) & 0xFF) / 255.0f, ((a_c >> 8) & 0xFF) / 255.0f, (a_c & 0xFF) / 255.0f, a_alpha };
		}

		Color FromFloats(const float a_rgb[3])
		{
			auto ch = [](float v) { return static_cast<Color>(std::clamp(std::lround(v * 255.0f), 0L, 255L)); };
			return ch(a_rgb[0]) << 16 | ch(a_rgb[1]) << 8 | ch(a_rgb[2]);
		}

		// one row of color (MenuStyle::ColorRow with the presets as quick picks); true when an edit is finished
		bool ColorChooser(const char* a_id, Color& a_color)
		{
			static const auto quick = [] {
				std::vector<MenuStyle::Swatch> q;
				for (const auto& p : kPresets) {
					const auto v = Vec(p.color);
					q.push_back({ p.name, { v.x, v.y, v.z } });
				}
				return q;
			}();
			std::vector<MenuStyle::Swatch> named(quick);
			for (auto& q : named) {
				q.name = T(q.name);
			}
			const auto v = Vec(a_color);
			float      rgb[3] = { v.x, v.y, v.z };
			const bool done = MenuStyle::ColorRow(a_id, rgb, named);
			a_color = FromFloats(rgb);
			return done;
		}

		// One color being edited (Illuminated's rule for its sliders: the control shows the stored value, and its own only while it
		// is being changed). While it is held the wards follow the drag live, in memory only (his ask 2026-10-06 - LiveRecolor; no
		// save, no re-equip); the edit is saved and the wards re-dressed ONCE when it ends: the picker's own "done", or the first
		// frame nothing on the page is held - ImGui never reports the end of a drag inside the color picker's pop-up, so such a
		// drag was never saved before 3.5. Until 3.5 the ladder's picker also stopped re-reading the stored color after its first
		// frame and put its own back over a color set elsewhere (DevBench's setter) - the outside review's "the ladder picker
		// re-reads / saves mid-drag". True when an edit ended (the caller saves and applies it: Changed).
		std::atomic<bool> gHeldEdit{ false };  // a color is held (changed live, not saved yet) - FlushHeldColor saves it

		// devbench only, never saved: a page drawn in a window of its own for a test's picture (DevBench.cpp's menuPage):
		// -1 closed, 0 Colors, 1 Colors scrolled to its end, 2 Settings, 3 Colors at its end with Factions open
		std::atomic_int gTestPage{ -1 };

		template <class Set>
		bool EditColor(const char* a_id, std::optional<Color>& a_held, Color a_stored, const Set& a_set)
		{
			Color      c = a_held.value_or(a_stored);
			const bool done = ColorChooser(a_id, c);
			if (c != a_held.value_or(a_stored)) {
				a_held = c;
				a_set(c);
				LiveRecolor();
				gHeldEdit = true;
			} else if (a_held && LivePending()) {
				LiveRecolor();  // the drag stopped inside the throttle: its last color still reaches the wards
			}
			if (a_held && (done || !IsAnyItemActive())) {
				a_held.reset();
				gHeldEdit = false;
				return true;
			}
			return false;
		}

		// a slider changes its setting live while held: its release saves it, and a menu shut mid-drag is saved by FlushHeldColor
		void SliderDone()
		{
			if (IsItemActive()) {
				gHeldEdit = true;
			}
			if (IsItemDeactivatedAfterEdit()) {
				gHeldEdit = false;
				Changed();
			} else if (IsItemDeactivated()) {
				gHeldEdit = false;  // let go without a change: nothing to save
			}
		}

		// one setting's slider: changed live while held, saved when let go (SliderDone)
		void SettingSlider(const char* a_label, int (*a_get)(), void (*a_set)(int), int a_min, int a_max, const char* a_tip)
		{
			int value = a_get();
			SetNextItemWidth(260.0f);
			SliderInt(a_label, &value, a_min, a_max, "%d%%");
			if (value != a_get()) {
				a_set(value);
			}
			SliderDone();
			SetItemTooltip("%s", a_tip);
		}

		// one setting's switch: saved and applied when clicked
		void SettingToggle(const char* a_label, bool (*a_get)(), void (*a_set)(bool), const char* a_tip)
		{
			bool on = a_get();
			if (Checkbox(a_label, &on)) {
				a_set(on);
				Changed();
			}
			SetItemTooltip("%s", a_tip);
		}

		// a setting's own control beside it (his rule), or on the next line when the panel is too narrow for its a_width
		void BesideIfRoom(float a_width)
		{
			SameLine(0.0f, 24.0f);
			if (GetContentRegionAvail().x < a_width) {
				NewLine();
			}
		}

		// a color row's first line: the 220 picker and a 20 swatch per preset (MenuStyle::ColorRow)
		float ColorRowWidth()
		{
			return 220.0f + static_cast<float>(std::size(kPresets)) * (20.0f + GetStyle()->ItemSpacing.x);
		}

		// the next dropdown as wide as the longest of these names, so no choice is cut short
		void FitCombo(std::span<const char* const> a_names)
		{
			float widest = 0.0f;
			for (const char* name : a_names) {
				widest = std::max(widest, CalcTextSize(name).x);
			}
			SetNextItemWidth(widest + GetFrameHeight() + GetStyle()->FramePadding.x * 2.0f);
		}

		// the mod a rank is left to now (its pick, with that mod there), else nullptr
		const char* LeftTo(std::size_t a_row)
		{
			return StrangeWardsRow(a_row) ? "Strange Wards" : StrangeRunesRow(a_row) ? "Strange Runes" :
			                                                                           nullptr;
		}

		// what a row's swatch shows: its color, or none (Vanilla, or a rank left to another mod's look)
		std::optional<Color> ShownColor(std::size_t a_row)
		{
			return LeftTo(a_row) ? std::nullopt : RowColor(a_row);
		}

		// the five ranks as they will look, white end to color end
		void LadderSwatches()
		{
			for (std::size_t i = 0; i < kRanks; ++i) {
				if (i) {
					SameLine();
				}
				const auto c = ShownColor(i);
				ColorButton(std::format("##rank{}", i).c_str(), c ? Vec(*c) : Vec(0x404040), ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoTooltip,
					ImVec2(48.0f, 22.0f));
				SetItemTooltip("%s", c         ? Fill(T("{}: #{}"), std::string(T(kRowLabel[i])), HexColor(*c)).c_str() :
									 LeftTo(i) ? Fill(T("{}: {}'s look"), std::string(T(kRowLabel[i])), std::string(LeftTo(i))).c_str() :
												 T(kRowLabel[i]));
			}
		}

		// one line per choice of the dropdowns below it, each its name in the accent and what it does (his ask 2026-11-02);
		// a line without a name is left out (a choice that is not offered here)
		void Legend(std::initializer_list<std::pair<const char*, const char*>> a_lines)
		{
			PushTextWrapPos(0.0f);
			for (const auto& [name, what] : a_lines) {
				if (name) {
					TextColored(MenuStyle::gTheme.accent, "%s", name);
					SameLine();
					TextColored(kMuted, "%s", what);
				}
			}
			PopTextWrapPos();
		}

		// a row's dropdown: Ladder (a rank) or Default (a rankless ward), Own color, then Vanilla - which a rank shows as
		// Strange Wards while that is installed, its art being the game's - and Strange Runes for a rank while that is loaded
		struct RowChoices
		{
			std::vector<const char*> names;
			std::vector<RowMode>     modes;

			explicit RowChoices(std::size_t a_row)
			{
				const bool rank = a_row < kRanks;
				Add(rank ? T("Ladder") : T("Default"), RowMode::kDefault);
				Add(T("Own color"), RowMode::kCustom);
				const bool strange = rank && StrangeWardsInstalled();
				Add(strange ? "Strange Wards" : T("Vanilla"), strange ? RowMode::kStrangeWards : RowMode::kVanilla);
				if (rank && StrangeRunesLoaded()) {
					Add("Strange Runes", RowMode::kStrangeRunes);
				}
			}
			void Add(const char* a_name, RowMode a_mode)
			{
				names.push_back(a_name);
				modes.push_back(a_mode);
			}
			// the shown choice: Vanilla on a Strange Wards rank is that, a mod's look without the mod is the ladder
			[[nodiscard]] int Shown(std::size_t a_row) const
			{
				const auto mode = RowModeOf(a_row);
				const auto want = StrangeWardsRow(a_row) ? RowMode::kStrangeWards : mode;
				const auto it = std::ranges::find(modes, want);
				return it == modes.end() ? 0 : static_cast<int>(it - modes.begin());
			}
		};

		static_assert(std::size(kRowFrom) == kRows);

		void DrawOthers();
		bool HasShields();
		void DrawShields();
		void DrawFactions();

		// The Colors page (his ask 2026-10-05: its own page above Settings): the presets first, then the ladder and every ward
		void DrawColors()
		{
			MenuStyle::Page page;

			static std::optional<Color>                    heldLadder;  // the ladder's picker while it is dragged (EditColor)
			static std::array<std::optional<Color>, kRows> heldRow;     // each ward's own color picker while it is dragged
			Header(Icon::kStar, T("Presets"));
			TextColored(kMuted, "%s", T("Sets the colors only - the stages and Reverse stay as you set them."));
			for (std::size_t s = 0; s < std::size(kSchemes); ++s) {
				const auto& sc = kSchemes[s];
				if (s % 4) {
					SameLine();
				}
				PushID(static_cast<int>(s));
				if (Button(T(sc.name))) {
					ApplyScheme(sc);
					Changed();
					heldLadder.reset();  // the pickers show the preset's colors, not a drag before it
					heldRow = {};
				}
				SetItemTooltip("%s", T(sc.tip));
				PopID();
			}

			Header(Icon::kPalette, T("Colors"));
			TextColored(kMuted, "%s", T("The ladder runs from white at the lowest rank to this color at the highest."));
			// dragged (the color bar, the picker's hue): the wards follow live - his ask 2026-10-06, "slide the mouse across the
			// slider ... to make the ward just skim through the color spectrum". Saved when let go.
			if (EditColor("ladder", heldLadder, LadderColor(), [](Color a_c) { SetLadderColor(a_c); })) {
				Changed();
			}
			int         stages = LadderStages() - kMinStages;
			const char* stageNames[] = { T("3 stages"), T("4 stages"), T("5 stages") };
			SetNextItemWidth(160.0f);
			if (Combo(T("Ladder stages"), &stages, stageNames, kMaxStages - kMinStages + 1)) {
				SetLadderStages(stages + kMinStages);
				Changed();
			}
			SetItemTooltip("%s",
				T("3: Lesser, Steadfast and Greater each a step. 4 or 5: Expert and Master wards (magic overhauls) get steps of "
				  "their own, so a Greater ward stops short of the full color."));
			SameLine();
			SettingToggle(T("Reverse the ladder"), LadderReversed, SetLadderReversed,
				T("Off: Lesser wards are white and the strongest take the color. On: the other way round."));
			LadderSwatches();

			Spacing();
			if (CollapsingHeader(T("Each ward"), ImGuiTreeNodeFlags_DefaultOpen)) {
				const bool strange = StrangeWardsInstalled();
				Legend({ { T("Ladder:"), T("a rank ward takes its step on the ladder above.") },
					{ T("Default:"), T("Dynamic Wards' own color for that ward - Spellbreaker gold, Shield of the Crusader and Visage of Reman white, Vampire ward red.") },
					{ T("Own color:"), T("a color you pick, for that ward alone.") },
					{ T("Vanilla:"), strange ? T("Dynamic Wards leaves the ward alone, as the game or another mod has it (on the rank wards that is Strange Wards).") :
											   T("Dynamic Wards leaves the ward alone, as the game or another mod has it.") },
					{ strange ? "Strange Wards:" : nullptr, T("a rank ward wears Strange Wards' own dome and hand art.") },
					{ StrangeRunesLoaded() ? "Strange Runes:" : nullptr, T("a rank ward keeps the look you pick in Strange Runes' own menu (reload the save to see it).") } });
				const char* const anyChoice[] = { T("Ladder"), T("Default"), T("Own color"), T("Vanilla"), "Strange Wards", "Strange Runes" };
				for (std::size_t i = 0; i < kRows; ++i) {
					PushID(static_cast<int>(i));
					const RowChoices choices(i);
					int              pick = choices.Shown(i);
					const auto       shown = ShownColor(i);
					ColorButton("##now", shown ? Vec(*shown) : Vec(0x303030), ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoTooltip, ImVec2(18.0f, 18.0f));
					SameLine();
					FitCombo(anyChoice);
					if (Combo(T(kRowLabel[i]), &pick, choices.names.data(), static_cast<int>(choices.names.size()))) {
						const auto mode = choices.modes[static_cast<std::size_t>(pick)];
						SetRow(i, mode, mode == RowMode::kCustom && RowModeOf(i) != RowMode::kCustom ? RowColor(i).value_or(kWhite) : RowCustom(i));
						heldRow[i].reset();
						Changed();
					}
					if (*kRowFrom[i]) {
						SameLine();
						TextColored(kMuted, "(%s)", kRowFrom[i]);
					}
					// a setting's own control sits beside it, never under it (his rule); the ward follows the drag live, saved when let go
					if (RowModeOf(i) == RowMode::kCustom) {
						BesideIfRoom(ColorRowWidth());
						if (EditColor("own", heldRow[i], RowCustom(i), [i](Color a_c) { SetRow(i, RowMode::kCustom, a_c); })) {
							Changed();
						}
					}
					PopID();
				}
			}
			if (CollapsingHeader(T("Other casters"), ImGuiTreeNodeFlags_DefaultOpen)) {
				DrawOthers();
			}
			if (HasShields() && CollapsingHeader(T("Shields"), ImGuiTreeNodeFlags_DefaultOpen)) {
				DrawShields();
			}
			if (gTestPage.load() == 3) {
				SetNextItemOpen(true, ImGuiCond_Always);
			}
			if (CollapsingHeader(T("Factions"))) {  // closed until opened: the longest list
				DrawFactions();
			}
		}

		// one look's pick: the player's look, its own color, the game's art (a faction also Default: its casters go on to the
		// Enemies or Neutral look). The ward follows a color drag live; saved when let go
		void LookRow(std::size_t a_look, std::optional<Color>& a_held)
		{
			PushID(static_cast<int>(a_look));
			const auto  pick = LookOf(a_look);
			const char* modes[] = { T("Your look"), T("Own color"), StrangeWardsInstalled() ? T("Vanilla (Strange Wards)") : T("Vanilla"), T("Default") };
			int         mode = static_cast<int>(pick.mode);
			ColorButton("##now", pick.mode == LookMode::kCustom ? Vec(pick.color) : Vec(0x303030), ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoTooltip, ImVec2(18.0f, 18.0f));
			SameLine();
			FitCombo(modes);
			if (Combo(T(kLookLabel[a_look]), &mode, modes, a_look >= kGroups ? 4 : 3)) {
				SetLook(a_look, { static_cast<LookMode>(mode), pick.color });
				a_held.reset();
				Changed();
			}
			SetItemTooltip("%s", T(kLookTip[a_look]));
			// beside its pick, never under it (his rule)
			if (LookOf(a_look).mode == LookMode::kCustom) {
				const char* missing = LookArtInstalled(a_look) ? nullptr : T("This look's meshes are not installed - install Dynamic Wards again.");
				BesideIfRoom(missing ? CalcTextSize(missing).x : ColorRowWidth());
				if (missing) {
					TextColored(kMuted, "%s", missing);
				} else if (EditColor("own", a_held, LookOf(a_look).color, [a_look](Color a_c) { SetLook(a_look, { LookMode::kCustom, a_c }); })) {
					Changed();
				}
			}
			PopID();
		}

		// Followers, Enemies and Neutral: one look each (his answer 2026-10-27)
		void DrawOthers()
		{
			static std::array<std::optional<Color>, kGroups> held;
			MenuStyle::Note(T("The ward look casters who are not you wear."));
			Legend({ { T("Your look:"), T("the same colors as your own wards.") }, { T("Own color:"), T("one color you pick for the whole group.") },
				{ T("Vanilla:"), StrangeWardsInstalled() ? T("the game's own ward - on the rank wards that is Strange Wards' look while it is installed.") :
														   T("the game's own ward, untouched.") } });
			MenuStyle::Note(T("A rank ward you set to Strange Wards or Strange Runes above keeps that look on everyone."));
			for (std::size_t i = 0; i < kGroups; ++i) {
				LookRow(i, held[i]);
			}
		}

		// one look per faction group; a group whose mod is not loaded is left out
		void DrawFactions()
		{
			static std::array<std::optional<Color>, kLooks> held;
			MenuStyle::Note(T("Pick the ward look a group's casters wear. A caster in two groups follows the first one in this list; a follower always wears the Followers look."));
			Legend({ { T("Default:"), T("no pick for this faction: its casters wear the Enemies or Neutral look.") } });
			for (std::size_t i = kGroups; i < kLooks; ++i) {
				if (FactionLoaded(i)) {
					LookRow(i, held[i]);
				}
			}
			Spacing();
			if (Button(T("Reset all factions"))) {
				for (std::size_t i = kGroups; i < kLooks; ++i) {
					SetLook(i, { LookMode::kDefault, LookOf(i).color });
				}
				Changed();
			}
			SetItemTooltip("%s", T("Every group back to Default."));
		}

		// MenuStyle::Header with a hover tip on its title
		void HeaderTip(unsigned a_icon, const char* a_text, const char* a_tip)
		{
			Spacing();
			FontAwesome::PushSolid();
			TextColored(MenuStyle::gTheme.accent, "%s", FontAwesome::UnicodeToUtf8(a_icon).c_str());
			FontAwesome::Pop();
			SameLine();
			TextColored(MenuStyle::gTheme.accent, "%s", a_text);
			SetItemTooltip("%s", a_tip);
			Separator();
		}

		// The Settings page: Lights, then the dome, then the wards' look
		void DrawSettings()
		{
			MenuStyle::Page page;

			const char* lighting[] = { T("Community Shaders"), T("ENB"), T("Vanilla") };
			HeaderTip(Icon::kBulb, T("Lights"), Fill(T("Lighting: Automatic - {}"), lighting[static_cast<int>(LightingPick())]).c_str());
			{  // every pick: ENB gets the plugin's lights too (Lighting.cpp), so its switches show
				SettingToggle(T("Ward casting light"), WardLightOn, SetWardLightOn,
					T("The light on the hands of anyone raising one of these wards, yours and other casters', in the ward's color. In first person your ward also lights your hand."));
				SettingToggle(T("Colored ward lights"), ColoredLightsOn, SetColoredLightsOn, T("Each ward's dome lights the area around you in its own color."));
			}

			// the ward's strength as Reticle Arcs' bar: shown for a ward she raises, and with this on for an always-on one too
			if (ArcBarRegistered()) {
				SettingToggle(T("Ward strength bar for always-on wards"), AlwaysOnBar, SetAlwaysOnBar,
					T("Reticle Arcs' Ward strength bar also shows for a ward that is always up and wears a dome, like Reliquary of Myth's "
					  "Staff of Magnus while you hold it or its Spellbreaker. Off: only for wards you raise."));
			}

			if (Has360Ward()) {
				Header(Icon::kShield, T("Dome"));
				int         dome = Dome360() ? 0 : 1;
				const char* domes[] = { T("360 dome"), T("Normal dome") };
				SetNextItemWidth(160.0f);
				if (Combo(T("Dome"), &dome, domes, 2)) {
					SetDome360(dome == 0);
					Changed();
				}
				// the unlock also rules a shield's 360 pick, so it shows when any ward wants the 360 dome (CodeRabbit, 2026-10-12)
				if (Any360Wanted()) {
					int         rule = static_cast<int>(UnlockRule());
					const char* rules[] = { T("Always"), T("Restoration 50"), T("Restoration 75"), T("Restoration 100"), T("A perk") };
					SetNextItemWidth(160.0f);
					if (Combo(T("360 dome unlocks at"), &rule, rules, 5)) {
						SetUnlockRule(static_cast<Unlock>(rule));
						Changed();
					}
					SetItemTooltip("%s",
						T("Until then your wards use the normal dome in their own color. The ward's blocking is set by Perfectly "
						  "Valid Wards, not here."));
					if (UnlockRule() == Unlock::kPerk) {
						static std::vector<std::pair<std::string, std::string>> perks = WardPerks();
						const auto                                              cur = UnlockPerk();
						std::string                                             preview = cur.empty() ? std::string(T("(pick a perk)")) : cur;
						for (const auto& [id, name] : perks) {
							if (id == cur) {
								preview = name;
							}
						}
						// the unlock's own pick: the 220 list and its label
						BesideIfRoom(220.0f + GetStyle()->ItemInnerSpacing.x + CalcTextSize(T("Perk")).x);
						SetNextItemWidth(220.0f);
						if (BeginCombo(T("Perk"), preview.c_str())) {
							for (const auto& [id, name] : perks) {
								if (Selectable(name.c_str(), id == cur)) {
									SetUnlockPerk(id);
									Changed();
								}
							}
							EndCombo();
						}
						if (perks.empty()) {
							TextDisabled("%s", T("No perk in your load order has ward in its name."));
						}
					}
					TextColored(Unlocked360() ? MenuStyle::gTheme.accent : kMuted, "%s", Unlocked360() ? T("Unlocked") : T("Locked"));
				}
			} else if (Missing360Patch()) {
				Header(Icon::kShield, T("Dome"));
				TextColored(kMuted, "%s", T("The 360 dome needs 360 Ward Universal Patch SKSE as well as 360 Ward."));
			} else if (Missing360Meshes()) {
				// 360 Ward and its patch are there, but our 360 domes are not: the installer puts them in only when 360 Ward is in
				// the load order, so the pick would change nothing (the outside review) - say how to get them instead
				Header(Icon::kShield, T("Dome"));
				TextColored(kMuted, "%s",
					T("The 360 dome's files are not installed: Dynamic Wards installs them only when 360 Ward is in your load order. "
					  "Install Dynamic Wards again to get them."));
			}

			Header(Icon::kEye, T("Look"));
			SettingSlider(T("Opacity"), Opacity, SetOpacity, kOpacityMin, 100,
				T("How strong the ward is: its color, glow and light fade together. The ward in your hands follows Hand Brightness instead."));
			// right below Opacity (his order 2026-10-05); greyed out without 360 Ward, whose dome it thins
			const bool has360 = Has360Ward();
			BeginDisabled(!has360);  // a disabled slider is never active: nothing set or saved
			SettingSlider(T("Transparency"), Transparency, SetTransparency, 0, kTransparencyMax,
				T("How clearly you see through the dome: the cloudy fill facing you thins out, the colored rim stays."));
			EndDisabled();
			if (!has360) {
				TextColored(kMuted, "%s", Missing360Meshes() ? T("Needs the 360 dome's files (install Dynamic Wards again).") : T("Needs 360 Ward."));
			}

			// Hand Brightness is the old Casting glow: the ini key stays CastingGlow so saved settings carry over
			SettingSlider(T("Ward brightness"), Brightness, SetBrightness, kBrightnessMin, kBrightnessMax,
				T("How bright the wards are: their glow and their light together. Turn it up if your lighting makes wards look dim. The ward in your hands follows Hand Brightness instead."));

			// the ward in your hands has its own slider, and it alone sets that art and its light (his rule 2026-10-05)
			SettingSlider(T("Hand Brightness"), CastingGlow, SetCastingGlow, kBrightnessMin, kBrightnessMax,
				T("How bright the ward in your hands is: its glow and its light together. Opacity and Ward brightness leave it alone."));

			// off unless the player turns it up: in a place with normal color a boosted dome reads too strong
			SettingSlider(T("Dome color boost"), DomeBoost, SetDomeBoost, 0, 100,
				T("Deepens gold, yellow and green domes in places whose own color grading washes colors out, like some cellars and dungeons. A color already at full strength stays as it is. 0 = off. In a place with normal color a boosted dome looks too strong."));
		}

		// a shield in this load order raises one of the shield wards, or Legacy's Crusader shields are there
		bool HasShields()
		{
			return CrusaderAvailable() || std::ranges::any_of(kShieldRows, ShieldFound);
		}

		// one pick per shield - Off (blocking raises no ward at all), Normal dome or 360 dome - and the Legacy Crusader switch
		void DrawShields()
		{
			bool shieldsShown = false;
			for (const auto row : kShieldRows) {
				if (!ShieldFound(row)) {
					continue;
				}
				shieldsShown = true;
				PushID(static_cast<int>(row));
				// all three always shown, so the stored pick is what the menu shows (the code review of 2026-10-12: a 360 pick shown
				// as Normal without 360 Ward could not be changed to Normal); without 360 Ward the 360 pick wears the normal dome
				int         pick = static_cast<int>(ShieldDomeOf(row));
				const char* picks[] = { T("Off"), T("Normal dome"), Has360Ward() ? T("360 dome") : Missing360Meshes() ? T("360 dome (needs its files)") :
																							   Missing360Patch()      ? T("360 dome (needs the Universal Patch)") :
																														T("360 dome (needs 360 Ward)") };
				FitCombo(picks);
				if (Combo(Fill(T("{} ward"), std::string(T(kRowLabel[row]))).c_str(), &pick, picks, 3)) {
					SetShieldDome(row, static_cast<ShieldDome>(pick));
					Changed();
				}
				SetItemTooltip("%s",
					T("Off: blocking with this shield raises no ward at all. Normal dome or 360 dome: the dome this shield's ward "
					  "wears, in its own color, whatever your spell wards wear. The 360 dome needs 360 Ward and follows its unlock."));
				PopID();
			}
			if (CrusaderAvailable()) {
				if (shieldsShown) {
					Separator();
				}
				SettingToggle(T("Legacy Crusader shields raise the Crusader ward"), CrusaderOn, SetCrusaderOn,
					T("The two Divine Crusader shields raise the Shield of the Crusader ward, so they take its color. They "
					  "also take that ward's strength instead of Spellbreaker's."));
			}
		}
	}

	// a menu closed (main.cpp's watch): a color still held then - the menu shut mid-drag - is saved and applied now, not only
	// the next time the Colors page is drawn (the 3.5 code review). The page then finds the stored color equal to its own and
	// lets go of it at once
	void FlushHeldColor()
	{
		if (gHeldEdit.exchange(false)) {
			Changed();
		}
	}

	namespace
	{
		SKSEMenuFramework::Model::WindowInterface* gTestWindow = nullptr;

		void __stdcall DrawTestWindow()
		{
			const int page = gTestPage.load();
			SetNextWindowPos(ImVec2(40.0f, 20.0f), ImGuiCond_Always);
			SetNextWindowSize(ImVec2(820.0f, 1040.0f), ImGuiCond_Always);
			if (Begin("Dynamic Wards (test)", nullptr, ImGuiWindowFlags_NoCollapse) && page >= 0 && page <= 3) {
				Guarded("menu: test window", page == 2 ? DrawSettings : DrawColors);
				if (page == 1 || page == 3) {
					SetScrollY(GetScrollMaxY());
				}
			}
			End();
		}
	}

	void ShowTestPage(int a_page)
	{
		gTestPage = a_page;
		if (gTestWindow) {
			gTestWindow->IsOpen = a_page >= 0;
		}
	}

	void RegisterMenu()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			SKSE::log::warn("SKSE Menu Framework is not installed, so there is no settings page; the settings file still applies");
			return;
		}
		SKSEMenuFramework::SetSection(T("Dynamic Wards"));
		SKSEMenuFramework::AddSectionItem(T("Colors"), []() { Guarded("menu: Colors", DrawColors); });  // first (his ask 2026-10-05)
		SKSEMenuFramework::AddSectionItem(T("Settings"), []() { Guarded("menu: Settings", DrawSettings); });
		gTestWindow = SKSEMenuFramework::AddWindow(DrawTestWindow, false);
		SKSE::log::info("settings page added to SKSE Menu Framework {}", SKSEMenuFramework::GetMenuFrameworkVersion());
	}
}
