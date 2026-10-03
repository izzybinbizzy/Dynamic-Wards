// Dynamic Wards - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// The SKSE Menu Framework pages (3.0): the ladder and every ward in any color - a drawn spectrum bar, color pickers,
// quick picks, live swatches of each rank - the opacity slider, the dome, the lights, the compatibility page, and an
// optional small swatch on the HUD showing the ward in your hands. A color is applied when the pick is let go (a drag
// does not rebuild palettes every frame).

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "Plugin.h"

#include "SKSEMenuFramework.h"
#include "Translation.h"

namespace Plugin
{
	namespace
	{
		using namespace ImGuiMCP;
		using Translation::T;
		using Translation::Fill;
#define TR_MARK(x) x  // a line Translation.json carries, though it is not written inside T( ) here

		// the house look: a deep violet accent on the menu's own dark
		const ImVec4 kAccent{ 0.62f, 0.45f, 1.0f, 1.0f };
		const ImVec4 kAccentDim{ 0.36f, 0.24f, 0.62f, 1.0f };
		const ImVec4 kNote{ 1.0f, 0.85f, 0.4f, 1.0f };
		const ImVec4 kMuted{ 0.62f, 0.62f, 0.68f, 1.0f };

		constexpr const char* kRowLabel[] = { TR_MARK("Lesser ward"), TR_MARK("Steadfast ward"), TR_MARK("Greater ward"),
			TR_MARK("Expert ward"), TR_MARK("Master ward"), TR_MARK("Spellbreaker"), TR_MARK("Shield of the Crusader"),
			TR_MARK("Visage of Reman"), TR_MARK("Vampire ward") };
		static_assert(std::size(kRowLabel) == kRows);

		// Font Awesome 6 (solid)
		constexpr unsigned kIconPalette = 0xF53F, kIconShield = 0xF3ED, kIconBulb = 0xF0EB, kIconPuzzle = 0xF12E, kIconEye = 0xF06E;

		SKSEMenuFramework::Model::HudElement* gHud = nullptr;

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

		ImU32 U32(Color a_c, int a_alpha = 255)
		{
			return IM_COL32((a_c >> 16) & 0xFF, (a_c >> 8) & 0xFF, a_c & 0xFF, a_alpha);
		}

		void Header(unsigned a_icon, const char* a_text)
		{
			FontAwesome::PushSolid();
			TextColored(kAccent, "%s", FontAwesome::UnicodeToUtf8(a_icon).c_str());
			FontAwesome::Pop();
			SameLine();
			TextColored(kAccent, "%s", a_text);
			Separator();
		}

		// the full spectrum as a bar: click or drag picks the hue at full color; the edit lands when the mouse is let go
		bool SpectrumBar(const char* a_id, Color& a_color)
		{
			const auto pos = GetCursorScreenPos();
			const auto avail = GetContentRegionAvail();
			const float w = (std::max)(120.0f, avail.x), h = 18.0f;
			auto*       dl = GetWindowDrawList();
			constexpr Color kStops[] = { 0xFF0000, 0xFFFF00, 0x00FF00, 0x00FFFF, 0x0000FF, 0xFF00FF, 0xFF0000 };
			for (int i = 0; i < 6; ++i) {
				const float x0 = pos.x + w * i / 6.0f, x1 = pos.x + w * (i + 1) / 6.0f;
				ImDrawListManager::AddRectFilledMultiColor(dl, ImVec2(x0, pos.y), ImVec2(x1, pos.y + h), U32(kStops[i]), U32(kStops[i + 1]),
					U32(kStops[i + 1]), U32(kStops[i]));
			}
			ImDrawListManager::AddRect(dl, pos, ImVec2(pos.x + w, pos.y + h), U32(0x000000, 160), 3.0f, 0, 1.0f);
			InvisibleButton(a_id, ImVec2(w, h));
			if (IsItemActive()) {
				const float t = std::clamp((GetMousePos().x - pos.x) / w, 0.0f, 0.9999f);
				float       r = 0, g = 0, b = 0;
				ColorConvertHSVtoRGB(t, 1.0f, 1.0f, &r, &g, &b);
				const float rgb[3] = { r, g, b };
				a_color = FromFloats(rgb);
			}
			return IsItemDeactivated();
		}

		// one row of color: a picker (applies when let go) and the quick picks
		bool ColorChooser(const char* a_id, Color& a_color)
		{
			PushID(a_id);
			bool  done = false;
			float rgb[3] = { ((a_color >> 16) & 0xFF) / 255.0f, ((a_color >> 8) & 0xFF) / 255.0f, (a_color & 0xFF) / 255.0f };
			SetNextItemWidth(220.0f);
			if (ColorEdit3("##pick", rgb, ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_NoAlpha)) {
				a_color = FromFloats(rgb);
			}
			done |= IsItemDeactivatedAfterEdit();
			for (const auto& p : kPresets) {
				SameLine();
				if (ColorButton(T(p.name), Vec(p.color), ImGuiColorEditFlags_NoAlpha, ImVec2(20.0f, 20.0f))) {
					a_color = p.color;
					done = true;
				}
				SetItemTooltip("%s", T(p.name));
			}
			done |= SpectrumBar("##spectrum", a_color);
			PopID();
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

		void __stdcall RenderSettings()
		{
			PushStyleColor(ImGuiCol_CheckMark, kAccent);
			PushStyleColor(ImGuiCol_SliderGrab, kAccent);
			PushStyleColor(ImGuiCol_SliderGrabActive, kAccentDim);
			PushStyleColor(ImGuiCol_Header, kAccentDim);
			PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

			Header(kIconPalette, T("Colors"));
			static Color ladder = LadderColor();
			static bool  editingLadder = false;
			if (!editingLadder) {
				ladder = LadderColor();
			}
			TextColored(kMuted, "%s", T("The ladder runs from white at the lowest rank to this color at the highest."));
			editingLadder = true;
			if (ColorChooser("ladder", ladder)) {
				SetLadderColor(ladder);
				Changed();
				editingLadder = false;
			}
			int stages = LadderStages() - kMinStages;
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
					int         mode = static_cast<int>(RowModeOf(i));
					const auto  shown = RowColor(i);
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
					if (RowModeOf(i) == RowMode::kCustom) {
						Indent(26.0f);
						Color c = RowCustom(i);
						if (ColorChooser("own", c)) {
							SetRow(i, RowMode::kCustom, c);
							Changed();
						} else if (c != RowCustom(i)) {
							SetRow(i, RowMode::kCustom, c);  // shown while dragging, applied when let go
						}
						Unindent(26.0f);
					}
					PopID();
				}
			}

			Spacing();
			Header(kIconEye, T("Opacity"));
			int opacity = Opacity();
			SetNextItemWidth(260.0f);
			SliderInt(T("Ward opacity"), &opacity, 10, 100, "%d%%");
			if (opacity != Opacity()) {
				SetOpacity(opacity);
			}
			if (IsItemDeactivatedAfterEdit()) {
				Changed();
			}
			SetItemTooltip("%s", T("How see-through the ward is. Its light dims with it."));

			if (Has360Ward()) {
				Spacing();
				Header(kIconShield, T("Dome"));
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
						const auto  cur = UnlockPerk();
						std::string preview = cur.empty() ? std::string(T("(pick a perk)")) : cur;
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
					TextColored(Unlocked360() ? kAccent : kMuted, "%s", Unlocked360() ? T("Unlocked") : T("Locked"));
				}
			}

			Spacing();
			Header(kIconBulb, T("Lights"));
			const char* lighting[] = { T("Community Shaders"), T("ENB"), T("Vanilla") };
			TextColored(kMuted, T("Lighting picked in the installer: %s"), lighting[static_cast<int>(LightingPick())]);
			if (LightingPick() == Lighting::kShaders && Effects11()) {
				TextColored(kMuted, "%s", T("Effects 11 found: your ward also lights your hand in first person."));
			}
			if (MeshLights()) {
				TextColored(kMuted, "%s", T("ENB lights are part of the ward meshes; they take the ward's color and dim with its opacity."));
			} else {
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
			bool hud = HudSwatch();
			if (Checkbox(T("Show the ward color on the HUD"), &hud)) {
				SetHudSwatch(hud);
				SaveSettings();
			}
			SetItemTooltip("%s", T("A small swatch in the corner of the screen with the color of the ward in your hands."));

			PopStyleVar();
			PopStyleColor(4);
		}

		// The Compatibility page: one tick per plugin that adds wards Dynamic Wards found by what they are (not by name);
		// unticked, that mod's wards keep their own art. The Legacy shields live here too.
		void __stdcall RenderCompatibility()
		{
			PushStyleColor(ImGuiCol_CheckMark, kAccent);
			Header(kIconPuzzle, T("Wards from other mods"));
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
			PopStyleColor();
		}

		// the HUD swatch: the color of the ward in either hand, bottom left, while one is equipped
		void __stdcall RenderHud()
		{
			if (!HudSwatch()) {
				return;
			}
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return;
			}
			std::optional<Color> color;
			std::size_t          row = kRows;
			for (const bool left : { false, true }) {
				auto* spell = skyrim_cast<RE::SpellItem*>(player->GetEquippedObject(left));
				if (!spell) {
					continue;
				}
				for (auto* e : spell->effects) {
					const auto* art = e && e->baseEffect ? e->baseEffect->data.castingArt : nullptr;
					if (const auto r = art && art->GetModel() ? RowOfModel(art->GetModel()) : kRows; r < kRows) {
						row = r;
						color = RowColor(r);
					}
				}
			}
			if (!color) {
				return;
			}
			auto*       io = GetIO();
			auto*       dl = GetForegroundDrawList();
			const float x = 24.0f, y = io ? io->DisplaySize.y - 215.0f : 600.0f;  // above the vanilla bars and the active-effect icons
			ImDrawListManager::AddRectFilled(dl, ImVec2(x - 4, y - 4), ImVec2(x + 196, y + 30), IM_COL32(10, 8, 18, 150), 6.0f, 0);
			ImDrawListManager::AddRectFilled(dl, ImVec2(x, y), ImVec2(x + 26, y + 26), U32(*color, static_cast<int>(255 * Opacity() / 100)), 5.0f, 0);
			ImDrawListManager::AddRect(dl, ImVec2(x, y), ImVec2(x + 26, y + 26), IM_COL32(255, 255, 255, 120), 5.0f, 0, 1.0f);
			ImDrawListManager::AddText(dl, ImVec2(x + 34, y + 5), IM_COL32(235, 230, 255, 230), T(kRowLabel[row]));
		}
	}

	void RegisterMenu()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			SKSE::log::warn("SKSE Menu Framework is not installed, so there is no settings page; the settings file still applies");
			return;
		}
		SKSEMenuFramework::SetSection(T("Dynamic Wards"));
		SKSEMenuFramework::AddSectionItem(T("Settings"), RenderSettings);
		SKSEMenuFramework::AddSectionItem(T("Compatibility"), RenderCompatibility);
		gHud = SKSEMenuFramework::AddHudElement(RenderHud);
		SKSE::log::info("settings page added to SKSE Menu Framework {}", SKSEMenuFramework::GetMenuFrameworkVersion());
	}
}
