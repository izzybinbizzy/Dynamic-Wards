// Dynamic Wards - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "Plugin.h"

#include "SKSEMenuFramework.h"

namespace Plugin
{
	namespace
	{
		const ImGuiMCP::ImVec4 kNote{ 1.0f, 0.85f, 0.4f, 1.0f };

		constexpr const char* kRowLabel[] = { "Lesser ward", "Steadfast ward", "Greater ward", "Expert ward", "Master ward",
			"Spellbreaker", "Shield of the Crusader", "Visage of Reman", "Vampire ward" };
		static_assert(std::size(kRowLabel) == kRows);

		void Changed()
		{
			SaveSettings();
			Later([]() { ApplyAll("menu"); });
		}

		void __stdcall RenderSettings()
		{
			ImGuiMCP::TextColored(kNote, "%s", "Colors");
			int         ladder = Ladder();
			const char* ladders[] = { "White and blue", "White and red", "White and gold", "White and green", "White, pink and purple" };
			static_assert(std::size(ladders) == kLadders);
			if (ImGuiMCP::Combo("Ladder", &ladder, ladders, static_cast<int>(kLadders))) {
				SetLadder(ladder);
				Changed();
			}
			int         stages = LadderStages() - kMinStages;
			const char* stageNames[] = { "3 stages", "4 stages", "5 stages" };
			if (ImGuiMCP::Combo("Ladder stages", &stages, stageNames, kMaxStages - kMinStages + 1)) {
				SetLadderStages(stages + kMinStages);
				Changed();
			}
			ImGuiMCP::SetItemTooltip("%s",
				"3: Lesser, Steadfast and Greater each a step. 4 or 5: Expert and Master wards (magic overhauls) get steps of "
				"their own, so a Greater ward stops short of the full color.");
			bool reversed = LadderReversed();
			if (ImGuiMCP::Checkbox("Reverse the ladder", &reversed)) {
				SetLadderReversed(reversed);
				Changed();
			}
			ImGuiMCP::SetItemTooltip("%s", "Off: Lesser wards are white and the strongest take the color. On: the other way round.");
			for (std::size_t i = 0; i < kRows; ++i) {
				ImGuiMCP::PushID(static_cast<int>(i));
				int         pick = static_cast<int>(RowPick(i));
				std::string first = i < kRanks ? std::string("Ladder") : std::format("Default ({})", kRowDefault[i]);
				// shown in color order; Purple is saved as 7 so an older settings file's 6 still means Vanilla
				const char*    items[] = { first.c_str(), "Blue", "Red", "Gold", "Green", "White", "Purple", "Vanilla" };
				constexpr Pick kShown[] = { Pick::kDefault, Pick::kBlue, Pick::kRed, Pick::kGold, Pick::kGreen, Pick::kWhite, Pick::kPurple,
					Pick::kVanilla };
				int            shown = static_cast<int>(std::ranges::find(kShown, static_cast<Pick>(pick)) - std::begin(kShown));
				if (ImGuiMCP::Combo(kRowLabel[i], &shown, items, static_cast<int>(std::size(items)))) {
					SetRowPick(i, kShown[shown]);
					Changed();
				}
				// his call, 2026-09-26: on every row's list (after the loop it reached only the last one)
				ImGuiMCP::SetItemTooltip("%s", "Vanilla leaves that ward exactly as the game or your other mods have it.");
				ImGuiMCP::PopID();
			}

			if (Has360Ward()) {
				ImGuiMCP::Separator();
				ImGuiMCP::TextColored(kNote, "%s", "Dome");
				int         dome = Dome360() ? 0 : 1;
				const char* domes[] = { "360 dome", "Normal dome" };
				if (ImGuiMCP::Combo("Dome", &dome, domes, 2)) {
					SetDome360(dome == 0);
					Changed();
				}
				if (Dome360()) {
					int         rule = static_cast<int>(UnlockRule());
					const char* rules[] = { "Always", "Restoration 50", "Restoration 75", "Restoration 100", "A perk" };
					if (ImGuiMCP::Combo("360 dome unlocks at", &rule, rules, 5)) {
						SetUnlockRule(static_cast<Unlock>(rule));
						Changed();
					}
					ImGuiMCP::SetItemTooltip("%s",
						"Until then your wards use the normal dome in their own color. The ward's blocking is set by Perfectly "
						"Valid Wards, not here.");
					if (UnlockRule() == Unlock::kPerk) {
						static std::vector<std::pair<std::string, std::string>> perks = WardPerks();
						const auto  cur = UnlockPerk();
						std::string preview = "(pick a perk)";
						for (const auto& [id, name] : perks) {
							if (id == cur) {
								preview = name;
							}
						}
						if (ImGuiMCP::BeginCombo("Perk", preview.c_str())) {
							for (const auto& [id, name] : perks) {
								if (ImGuiMCP::Selectable(name.c_str(), id == cur)) {
									SetUnlockPerk(id);
									Changed();
								}
							}
							ImGuiMCP::EndCombo();
						}
						if (perks.empty()) {
							ImGuiMCP::TextDisabled("%s", "No perk in your load order has ward in its name.");
						}
					}
					ImGuiMCP::TextDisabled("%s", Unlocked360() ? "Unlocked" : "Locked");
				}
			}

			ImGuiMCP::Separator();
			ImGuiMCP::TextColored(kNote, "%s", "Lights");
			bool light = WardLightOn();
			if (ImGuiMCP::Checkbox("Ward casting light", &light)) {
				SetWardLightOn(light);
				Changed();
			}
			ImGuiMCP::SetItemTooltip("%s", "The light on your hand while you raise a ward, in the ward's color.");
			bool colored = ColoredLightsOn();
			if (ImGuiMCP::Checkbox("Colored ward lights", &colored)) {
				SetColoredLightsOn(colored);
				Changed();
			}
			ImGuiMCP::SetItemTooltip("%s", "Each ward's dome lights the area around you in its own color (Light Placer or RE::Light).");

			if (CrusaderAvailable()) {
				ImGuiMCP::Separator();
				bool cru = CrusaderOn();
				if (ImGuiMCP::Checkbox("Legacy Crusader shields raise the Crusader ward", &cru)) {
					SetCrusaderOn(cru);
					Changed();
				}
				ImGuiMCP::SetItemTooltip("%s",
					"The two Divine Crusader shields raise the Shield of the Crusader ward, so they take its color. They "
					"also take that ward's strength instead of Spellbreaker's.");
			}

			ImGuiMCP::Separator();
			bool every = EveryWard();
			if (ImGuiMCP::Checkbox("Color every ward found", &every)) {
				SetEveryWard(every);
				SaveSettings();
			}
			ImGuiMCP::SetItemTooltip("%s", "Wards from any mod get the color of their rank. Takes effect after a restart.");
			ImGuiMCP::TextDisabled("%zu ward effect(s) found, %zu colored", FoundCount(), DressedCount());
		}
	}

	void RegisterMenu()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			SKSE::log::warn("SKSE Menu Framework is not installed, so there is no settings page; the settings file still applies");
			return;
		}
		SKSEMenuFramework::SetSection("Dynamic Wards");
		SKSEMenuFramework::AddSectionItem("Settings", RenderSettings);
		SKSE::log::info("settings page added to SKSE Menu Framework {}", SKSEMenuFramework::GetMenuFrameworkVersion());
	}
}
