// Dynamic Wards - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "Plugin.h"

#include "SKSEMenuFramework.h"
#include "Translation.h"

namespace Plugin
{
	namespace
	{
		const ImGuiMCP::ImVec4 kNote{ 1.0f, 0.85f, 0.4f, 1.0f };
		using Translation::T;
#define TR_MARK(x) x  // a line Translation.json carries, though it is not written inside T( ) here

		using Translation::Fill;

		constexpr const char* kRowLabel[] = { TR_MARK("Lesser ward"), TR_MARK("Steadfast ward"), TR_MARK("Greater ward"),
			TR_MARK("Expert ward"), TR_MARK("Master ward"), TR_MARK("Spellbreaker"), TR_MARK("Shield of the Crusader"),
			TR_MARK("Visage of Reman"), TR_MARK("Vampire ward") };
		static_assert(std::size(kRowLabel) == kRows);

		void Changed()
		{
			SaveSettings();
			Later([]() { ApplyAll("menu"); });
		}

		void __stdcall RenderSettings()
		{
			ImGuiMCP::TextColored(kNote, "%s", T("Colors"));
			int         ladder = Ladder();
			const char* ladders[] = { T("White and blue"), T("White and red"), T("White and gold"), T("White and green"), T("White, pink and purple") };
			static_assert(std::size(ladders) == kLadders);
			if (ImGuiMCP::Combo(T("Ladder"), &ladder, ladders, static_cast<int>(kLadders))) {
				SetLadder(ladder);
				Changed();
			}
			int         stages = LadderStages() - kMinStages;
			const char* stageNames[] = { T("3 stages"), T("4 stages"), T("5 stages") };
			if (ImGuiMCP::Combo(T("Ladder stages"), &stages, stageNames, kMaxStages - kMinStages + 1)) {
				SetLadderStages(stages + kMinStages);
				Changed();
			}
			ImGuiMCP::SetItemTooltip("%s",
				T("3: Lesser, Steadfast and Greater each a step. 4 or 5: Expert and Master wards (magic overhauls) get steps of "
				"their own, so a Greater ward stops short of the full color."));
			bool reversed = LadderReversed();
			if (ImGuiMCP::Checkbox(T("Reverse the ladder"), &reversed)) {
				SetLadderReversed(reversed);
				Changed();
			}
			ImGuiMCP::SetItemTooltip("%s", T("Off: Lesser wards are white and the strongest take the color. On: the other way round."));
			for (std::size_t i = 0; i < kRows; ++i) {
				ImGuiMCP::PushID(static_cast<int>(i));
				int         pick = static_cast<int>(RowPick(i));
				const std::string colorName = T(std::string(kRowDefault[i]).c_str());
				std::string       first = i < kRanks ? std::string(T("Ladder")) : Fill(T("Default ({})"), colorName);
				// shown in color order; Purple is saved as 7 so an older settings file's 6 still means Vanilla
				const char*    items[] = { first.c_str(), T("Blue"), T("Red"), T("Gold"), T("Green"), T("White"), T("Purple"), T("Vanilla") };
				constexpr Pick kShown[] = { Pick::kDefault, Pick::kBlue, Pick::kRed, Pick::kGold, Pick::kGreen, Pick::kWhite, Pick::kPurple,
					Pick::kVanilla };
				int            shown = static_cast<int>(std::ranges::find(kShown, static_cast<Pick>(pick)) - std::begin(kShown));
				if (ImGuiMCP::Combo(T(kRowLabel[i]), &shown, items, static_cast<int>(std::size(items)))) {
					SetRowPick(i, kShown[shown]);
					Changed();
				}
				// his call, 2026-09-26: on every row's list (after the loop it reached only the last one)
				ImGuiMCP::SetItemTooltip("%s", T("Vanilla leaves that ward exactly as the game or your other mods have it."));
				ImGuiMCP::PopID();
			}

			if (Has360Ward()) {
				ImGuiMCP::Separator();
				ImGuiMCP::TextColored(kNote, "%s", T("Dome"));
				int         dome = Dome360() ? 0 : 1;
				const char* domes[] = { T("360 dome"), T("Normal dome") };
				if (ImGuiMCP::Combo(T("Dome"), &dome, domes, 2)) {
					SetDome360(dome == 0);
					Changed();
				}
				if (Dome360()) {
					int         rule = static_cast<int>(UnlockRule());
					const char* rules[] = { T("Always"), T("Restoration 50"), T("Restoration 75"), T("Restoration 100"), T("A perk") };
					if (ImGuiMCP::Combo(T("360 dome unlocks at"), &rule, rules, 5)) {
						SetUnlockRule(static_cast<Unlock>(rule));
						Changed();
					}
					ImGuiMCP::SetItemTooltip("%s",
						T("Until then your wards use the normal dome in their own color. The ward's blocking is set by Perfectly "
						"Valid Wards, not here."));
					if (UnlockRule() == Unlock::kPerk) {
						static std::vector<std::pair<std::string, std::string>> perks = WardPerks();
						const auto  cur = UnlockPerk();
						std::string preview = cur.empty() ? std::string(T("(pick a perk)")) : cur;  // a perk set in the file, not in this list
						for (const auto& [id, name] : perks) {
							if (id == cur) {
								preview = name;
							}
						}
						if (ImGuiMCP::BeginCombo(T("Perk"), preview.c_str())) {
							for (const auto& [id, name] : perks) {
								if (ImGuiMCP::Selectable(name.c_str(), id == cur)) {
									SetUnlockPerk(id);
									Changed();
								}
							}
							ImGuiMCP::EndCombo();
						}
						if (perks.empty()) {
							ImGuiMCP::TextDisabled("%s", T("No perk in your load order has ward in its name."));
						}
					}
					ImGuiMCP::TextDisabled("%s", Unlocked360() ? T("Unlocked") : T("Locked"));
				}
			}

			ImGuiMCP::Separator();
			ImGuiMCP::TextColored(kNote, "%s", T("Lights"));
			bool light = WardLightOn();
			if (ImGuiMCP::Checkbox(T("Ward casting light"), &light)) {
				SetWardLightOn(light);
				Changed();
			}
			ImGuiMCP::SetItemTooltip("%s", T("The light on your hand while you raise a ward, in the ward's color."));
			bool colored = ColoredLightsOn();
			if (ImGuiMCP::Checkbox(T("Colored ward lights"), &colored)) {
				SetColoredLightsOn(colored);
				Changed();
			}
			ImGuiMCP::SetItemTooltip("%s", T("Each ward's dome lights the area around you in its own color (Light Placer or RE::Light)."));
		}

		// The Compatibility page: one tick per plugin that adds wards Dynamic Wards found by what they are (not by name);
		// unticked, that mod's wards keep their own art. The Legacy shields live here too.
		void __stdcall RenderCompatibility()
		{
			ImGuiMCP::TextColored(kNote, "%s", T("Wards from other mods"));
			const auto mods = FoundMods();
			if (mods.empty()) {
				ImGuiMCP::TextDisabled("%s", T("No mod in your load order adds wards of its own."));
			}
			for (const auto& [plugin, count] : mods) {
				ImGuiMCP::PushID(plugin.c_str());
				bool        on = ModOn(plugin);
				const std::string name = plugin.empty() ? std::string(T("Made in memory")) : plugin;
				std::string label = count == 1 ? Fill(T("{} (1 ward)"), name) : Fill(T("{} ({} wards)"), name, std::to_string(count));
				if (ImGuiMCP::Checkbox(label.c_str(), &on)) {
					SetModOn(plugin, on);
					Changed();
				}
				ImGuiMCP::SetItemTooltip("%s", T("On: this mod's wards take the color of their rank. Off: they keep the mod's own look."));
				ImGuiMCP::PopID();
			}
			ImGuiMCP::TextDisabled(T("%zu ward effect(s) found, %zu colored"), FoundCount(), DressedCount());

			if (CrusaderAvailable()) {
				ImGuiMCP::Separator();
				bool cru = CrusaderOn();
				if (ImGuiMCP::Checkbox(T("Legacy Crusader shields raise the Crusader ward"), &cru)) {
					SetCrusaderOn(cru);
					Changed();
				}
				ImGuiMCP::SetItemTooltip("%s",
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
		SKSEMenuFramework::AddSectionItem(T("Settings"), RenderSettings);
		SKSEMenuFramework::AddSectionItem(T("Compatibility"), RenderCompatibility);
		SKSE::log::info("settings page added to SKSE Menu Framework {}", SKSEMenuFramework::GetMenuFrameworkVersion());
	}
}
