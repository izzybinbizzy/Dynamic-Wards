// Dynamic Wards - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// The SKSE Menu Framework pages (3.0): the ladder and every ward in any color - a drawn spectrum bar, color pickers,
// quick picks, live swatches of each rank - the opacity slider, the dome, the lights and the compatibility page. A color
// is applied when the pick is let go (a drag does not rebuild palettes every frame). The HUD swatch is gone (his call 2026-10-03).
// His layout 2026-10-05: a Colors page first (presets, then the ladder and every ward, the rankless ones with their mod
// named), then Settings (Lights, Dome, Opacity - Transparency right below Opacity, greyed out without 360 Ward), then
// Compatibility. Every change is saved the moment it is made (a slider when it is let go), so closing the menu loses nothing.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "Plugin.h"

#include "SKSEMenuFramework.h"
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

		void Changed()
		{
			SaveSettings();
			Later([]() { ApplyAll("menu"); });
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

		// the five ranks as they will look, white end to color end
		void LadderSwatches()
		{
			for (std::size_t i = 0; i < kRanks; ++i) {
				if (i) {
					SameLine();
				}
				const auto c = RowColor(i);
				ColorButton(std::format("##rank{}", i).c_str(), c ? Vec(*c) : Vec(0x404040), ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoTooltip,
					ImVec2(48.0f, 22.0f));
				SetItemTooltip("%s", c ? Fill(T("{}: #{}"), std::string(T(kRowLabel[i])), HexColor(*c)).c_str() : T(kRowLabel[i]));
			}
		}

		static_assert(std::size(kRowFrom) == kRows);

		// The Colors page (his ask 2026-10-05: its own page above Settings): the presets first, then the ladder and every ward
		void __stdcall RenderColors()
		{
			MenuStyle::Page page;

			static Color ladder = LadderColor();
			static bool  editingLadder = false;
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
					editingLadder = false;  // the picker shows the preset's ladder, not the one before it
				}
				SetItemTooltip("%s", T(sc.tip));
				PopID();
			}

			Header(Icon::kPalette, T("Colors"));
			if (!editingLadder) {
				ladder = LadderColor();
			}
			TextColored(kMuted, "%s", T("The ladder runs from white at the lowest rank to this color at the highest."));
			editingLadder = true;
			if (ColorChooser("ladder", ladder)) {
				SetLadderColor(ladder);
				Changed();
				editingLadder = false;
			} else if (ladder != LadderColor()) {
				// dragged (the color bar, the picker's hue): the wards follow live - his ask 2026-10-06, "slide the mouse
				// across the slider ... to make the ward just skim through the color spectrum". Saved when let go.
				SetLadderColor(ladder);
				LiveRecolor();
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
			bool reversed = LadderReversed();
			if (Checkbox(T("Reverse the ladder"), &reversed)) {
				SetLadderReversed(reversed);
				Changed();
			}
			SetItemTooltip("%s", T("Off: Lesser wards are white and the strongest take the color. On: the other way round."));
			LadderSwatches();

			Spacing();
			if (CollapsingHeader(T("Each ward"), ImGuiTreeNodeFlags_DefaultOpen)) {
				for (std::size_t i = 0; i < kRows; ++i) {
					PushID(static_cast<int>(i));
					int        mode = static_cast<int>(RowModeOf(i));
					const auto shown = RowColor(i);
					ColorButton("##now", shown ? Vec(*shown) : Vec(0x303030), ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoTooltip, ImVec2(18.0f, 18.0f));
					SameLine();
					const std::string first = i < kRanks ? std::string(T("Ladder")) : std::string(T("Default"));
					const char*       modes[] = { first.c_str(), T("Own color"), T("Vanilla") };
					SetNextItemWidth(150.0f);
					if (Combo(T(kRowLabel[i]), &mode, modes, 3)) {
						SetRow(i, static_cast<RowMode>(mode), mode == 1 && RowModeOf(i) != RowMode::kCustom ? shown.value_or(kWhite) : RowCustom(i));
						Changed();
					}
					SetItemTooltip("%s", T("Vanilla leaves that ward exactly as the game or your other mods have it."));
					if (*kRowFrom[i]) {
						SameLine();
						TextColored(kMuted, "(%s)", kRowFrom[i]);
					}
					if (RowModeOf(i) == RowMode::kCustom) {
						Indent(26.0f);
						Color c = RowCustom(i);
						if (ColorChooser("own", c)) {
							SetRow(i, RowMode::kCustom, c);
							Changed();
						} else if (c != RowCustom(i)) {
							SetRow(i, RowMode::kCustom, c);  // the ward follows the drag live; saved when let go
							LiveRecolor();
						}
						Unindent(26.0f);
					}
					PopID();
				}
			}
		}

		// The Settings page (his order 2026-10-05): Lights first, then the dome, Opacity last
		void __stdcall RenderSettings()
		{
			MenuStyle::Page page;

			Header(Icon::kBulb, T("Lights"));
			const char* lighting[] = { T("Community Shaders"), T("ENB"), T("Vanilla") };
			TextColored(kMuted, T("Lighting picked in the installer: %s"), lighting[static_cast<int>(LightingPick())]);
			TextColored(kMuted, "%s", T("In first person your ward also lights your hand."));
			{  // every pick since 2026-10-06: ENB gets the plugin's lights too (Lighting.cpp OwnLights), so its switches show
				bool light = WardLightOn();
				if (Checkbox(T("Ward casting light"), &light)) {
					SetWardLightOn(light);
					Changed();
				}
				SetItemTooltip("%s", T("The light on your hand while you raise a ward, in the ward's color."));
				bool colored = ColoredLightsOn();
				if (Checkbox(T("Colored ward lights"), &colored)) {
					SetColoredLightsOn(colored);
					Changed();
				}
				SetItemTooltip("%s", T("Each ward's dome lights the area around you in its own color."));
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
				if (Dome360()) {
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
			}

			Header(Icon::kEye, T("Opacity"));
			int opacity = Opacity();
			SetNextItemWidth(260.0f);
			SliderInt(T("Opacity"), &opacity, kOpacityMin, 100, "%d%%");
			if (opacity != Opacity()) {
				SetOpacity(opacity);
			}
			if (IsItemDeactivatedAfterEdit()) {
				Changed();
			}
			SetItemTooltip("%s", T("How strong the ward is: its color, glow and light fade together. The ward in your hands follows Hand Brightness instead."));
			// right below Opacity (his order 2026-10-05); greyed out without 360 Ward, whose dome it thins
			const bool has360 = Has360Ward();
			BeginDisabled(!has360);
			int transparency = Transparency();
			SetNextItemWidth(260.0f);
			SliderInt(T("Transparency"), &transparency, 0, kTransparencyMax, "%d%%");
			if (has360 && transparency != Transparency()) {
				SetTransparency(transparency);
			}
			if (has360 && IsItemDeactivatedAfterEdit()) {
				Changed();
			}
			SetItemTooltip("%s", T("How clearly you see through the dome: the cloudy fill facing you thins out, the colored rim stays."));
			EndDisabled();
			if (!has360) {
				TextColored(kMuted, "%s", T("Needs 360 Ward."));
			}

			// his order 2026-10-06: Ward brightness moves down under the old "Casting art" heading, now "Brightness", and
			// Casting glow is "Hand Brightness" (the ini key stays CastingGlow so saved settings carry over)
			Header(Icon::kStar, T("Brightness"));
			int brightness = Brightness();
			SetNextItemWidth(260.0f);
			SliderInt(T("Ward brightness"), &brightness, kBrightnessMin, kBrightnessMax, "%d%%");
			if (brightness != Brightness()) {
				SetBrightness(brightness);
			}
			if (IsItemDeactivatedAfterEdit()) {
				Changed();
			}
			SetItemTooltip("%s", T("How bright the wards are: their glow and their light together. Turn it up if your lighting makes wards look dim. The ward in your hands follows Hand Brightness instead."));

			// the ward in your hands has its own slider, and it alone sets that art and its light (his rule 2026-10-05)
			int castingGlow = CastingGlow();
			SetNextItemWidth(260.0f);
			SliderInt(T("Hand Brightness"), &castingGlow, kBrightnessMin, kBrightnessMax, "%d%%");
			if (castingGlow != CastingGlow()) {
				SetCastingGlow(castingGlow);
			}
			if (IsItemDeactivatedAfterEdit()) {
				Changed();
			}
			SetItemTooltip("%s", T("How bright the ward in your hands is: its glow and its light together. Opacity and Ward brightness leave it alone."));
		}

		// The Compatibility page: one tick per plugin that adds wards Dynamic Wards found by what they are (not by name);
		// unticked, that mod's wards keep their own art. The Legacy shields live here too.
		void __stdcall RenderCompatibility()
		{
			MenuStyle::Page page;
			Header(Icon::kPuzzle, T("Wards from other mods"));
			const auto mods = FoundMods();
			if (mods.empty()) {
				TextDisabled("%s", T("No mod in your load order adds wards of its own."));
			}
			for (const auto& [plugin, count] : mods) {
				PushID(plugin.c_str());
				bool              on = ModOn(plugin);
				const std::string name = plugin.empty() ? std::string(T("Made in memory")) : plugin;
				std::string       label = count == 1 ? Fill(T("{} (1 ward)"), name) : Fill(T("{} ({} wards)"), name, std::to_string(count));
				if (Checkbox(label.c_str(), &on)) {
					SetModOn(plugin, on);
					Changed();
				}
				SetItemTooltip("%s", T("On: this mod's wards take the color of their rank. Off: they keep the mod's own look."));
				PopID();
			}
			TextColored(kMuted, T("%zu ward effect(s) found, %zu colored"), FoundCount(), DressedCount());
			if (StrangeRunesLoaded()) {
				Separator();
				bool keep = KeepStrangeRunes();
				if (Checkbox(T("Keep Strange Runes' ward look"), &keep)) {
					SetKeepStrangeRunes(keep);
					Changed();
				}
				SetItemTooltip("%s",
					T("Strange Runes gives the wards its own look from its menu. Off: Dynamic Wards puts its colors back whenever "
					  "Strange Runes swaps them. On: the wards keep Strange Runes' look (reload the save to see it)."));
			}
			if (CrusaderAvailable()) {
				Separator();
				bool cru = CrusaderOn();
				if (Checkbox(T("Legacy Crusader shields raise the Crusader ward"), &cru)) {
					SetCrusaderOn(cru);
					Changed();
				}
				SetItemTooltip("%s",
					T("The two Divine Crusader shields raise the Shield of the Crusader ward, so they take its color. They "
					  "also take that ward's strength instead of Spellbreaker's."));
			}
		}
	}

	void RegisterMenu()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			SKSE::log::warn("SKSE Menu Framework is not installed, so there is no settings page; the settings file still applies");
			return;
		}
		SKSEMenuFramework::SetSection(T("Dynamic Wards"));
		SKSEMenuFramework::AddSectionItem(T("Colors"), RenderColors);  // first (his ask 2026-10-05)
		SKSEMenuFramework::AddSectionItem(T("Settings"), RenderSettings);
		SKSEMenuFramework::AddSectionItem(T("Compatibility"), RenderCompatibility);
		SKSE::log::info("settings page added to SKSE Menu Framework {}", SKSEMenuFramework::GetMenuFrameworkVersion());
	}
}
