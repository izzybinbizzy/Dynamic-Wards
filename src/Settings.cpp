// Dynamic Wards - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// DynamicWards.ini, written by the menu: the live settings table (SettingsText.h's SettingsValues - one table, read and
// written by the game-free reader and writer the off-game tests run) under one lock, and its accessors.

#include "Plugin.h"

namespace Plugin
{
	namespace
	{
		constexpr const char* kPath = "Data/SKSE/Plugins/DynamicWards.ini";

		std::mutex     gLock;
		SettingsValues gS;  // under gLock

		const char* ShieldDomeName(int a_dome)
		{
			return a_dome == static_cast<int>(ShieldDome::kOff) ? "off" : a_dome == static_cast<int>(ShieldDome::k360) ? "360 dome" :
			                                                                                                             "normal dome";
		}
	}

	void LoadSettings()
	{
		std::ifstream    in(kPath);
		std::scoped_lock l{ gLock };
		const auto       read = ReadSettingsText(in, gS);
		SKSE::log::info(
			"settings: ladder {} ({} stages{}), opacity {}%, transparency {}%, brightness {}%, hand brightness {}%, dome color boost {}%, dome {}, ward light {}, colored lights {}, 360 unlock rule {}{}, "
			"Crusader shields {}, every ward {}, shield wards Spellbreaker {} Crusader {} Reman {} ({} line(s) read{})",
			HexColor(gS.ladder), gS.stages, gS.reversed ? ", reversed" : "", gS.opacity, gS.transparency, gS.brightness, gS.castingGlow, gS.domeBoost, gS.dome,
			gS.light ? "on" : "off", gS.coloredLights ? "on" : "off", static_cast<int>(gS.unlock), gS.perk.empty() ? "" : " " + gS.perk,
			gS.crusader ? "on" : "off", gS.every ? "on" : "off", ShieldDomeName(SettingsText::ShieldPick(gS, 0)),
			ShieldDomeName(SettingsText::ShieldPick(gS, 1)), ShieldDomeName(SettingsText::ShieldPick(gS, 2)), read.lines,
			read.old ? "; a 2.x file, carried over" : "");
	}

	// Any thread (the menu queues it onto the main thread; DevBench saves on its own). Written beside the file and moved over
	// it, as RELight - Spell Addon and Illuminated save theirs: a crash or a full disk mid-write leaves the old file, never half
	// of one. One save at a time, and each takes its snapshot inside that turn, so the last file written holds the newest settings
	void SaveSettings()
	{
		static std::mutex  writing;
		std::scoped_lock   w{ writing };
		std::ostringstream out;
		{
			std::scoped_lock l{ gLock };
			WriteSettingsText(gS, out);
		}
		const std::string tmp = std::string(kPath) + ".tmp";
		std::error_code   ec;
		{
			std::ofstream file(tmp, std::ios::trunc);
			file << out.str();
			file.close();
			if (!file) {
				SKSE::log::warn("settings: {} could not be written", kPath);
				std::filesystem::remove(tmp, ec);
				return;
			}
		}
		if (std::filesystem::rename(tmp, kPath, ec); ec) {
			SKSE::log::warn("settings: {} could not be replaced ({})", kPath, ec.message());
			std::filesystem::remove(tmp, ec);  // nothing left behind when the move failed
		}
	}

	RowMode RowModeOf(std::size_t a_row)
	{
		std::scoped_lock l{ gLock };
		return a_row < kRows ? gS.rows[a_row].mode : RowMode::kDefault;
	}
	Color RowCustom(std::size_t a_row)
	{
		std::scoped_lock l{ gLock };
		return a_row < kRows ? gS.rows[a_row].color : kWhite;
	}
	void SetRow(std::size_t a_row, RowMode a_mode, Color a_color)
	{
		std::scoped_lock l{ gLock };
		if (a_row < kRows) {
			gS.rows[a_row] = { a_mode, a_color & 0xFFFFFF };
		}
	}
	Color LadderColor()
	{
		std::scoped_lock l{ gLock };
		return gS.ladder;
	}
	void SetLadderColor(Color a_color)
	{
		std::scoped_lock l{ gLock };
		gS.ladder = a_color & 0xFFFFFF;
	}
	int LadderStages()
	{
		std::scoped_lock l{ gLock };
		return gS.stages;
	}
	void SetLadderStages(int a_stages)
	{
		std::scoped_lock l{ gLock };
		gS.stages = std::clamp(a_stages, kMinStages, kMaxStages);
	}
	bool LadderReversed()
	{
		std::scoped_lock l{ gLock };
		return gS.reversed;
	}
	void SetLadderReversed(bool a_on)
	{
		std::scoped_lock l{ gLock };
		gS.reversed = a_on;
	}
	int Opacity()
	{
		std::scoped_lock l{ gLock };
		return gS.opacity;
	}
	void SetOpacity(int a_percent)
	{
		std::scoped_lock l{ gLock };
		gS.opacity = std::clamp(a_percent, kOpacityMin, 100);
	}
	int Brightness()
	{
		std::scoped_lock l{ gLock };
		return gS.brightness;
	}
	void SetBrightness(int a_percent)
	{
		std::scoped_lock l{ gLock };
		gS.brightness = std::clamp(a_percent, kBrightnessMin, kBrightnessMax);
	}
	float LightDim()
	{
		std::scoped_lock l{ gLock };
		return gS.opacity / 100.0f * gS.brightness / 100.0f;
	}
	int CastingGlow()
	{
		std::scoped_lock l{ gLock };
		return gS.castingGlow;
	}
	void SetCastingGlow(int a_percent)
	{
		std::scoped_lock l{ gLock };
		gS.castingGlow = std::clamp(a_percent, kBrightnessMin, kBrightnessMax);
	}
	int DomeBoost()
	{
		std::scoped_lock l{ gLock };
		return gS.domeBoost;
	}
	void SetDomeBoost(int a_percent)
	{
		std::scoped_lock l{ gLock };
		gS.domeBoost = std::clamp(a_percent, 0, 100);
	}
	float HandDim()
	{
		std::scoped_lock l{ gLock };
		return gS.castingGlow / 100.0f;
	}
	int Transparency()
	{
		std::scoped_lock l{ gLock };
		return gS.transparency;
	}
	void SetTransparency(int a_percent)
	{
		std::scoped_lock l{ gLock };
		gS.transparency = std::clamp(a_percent, 0, kTransparencyMax);
	}
	bool Dome360()
	{
		std::scoped_lock l{ gLock };
		return gS.dome == 0;
	}
	void SetDome360(bool a_on)
	{
		std::scoped_lock l{ gLock };
		gS.dome = a_on ? 0 : 1;
	}
	bool WardLightOn()
	{
		std::scoped_lock l{ gLock };
		return gS.light;
	}
	void SetWardLightOn(bool a_on)
	{
		std::scoped_lock l{ gLock };
		gS.light = a_on;
	}
	bool ColoredLightsOn()
	{
		std::scoped_lock l{ gLock };
		return gS.coloredLights;
	}
	void SetColoredLightsOn(bool a_on)
	{
		std::scoped_lock l{ gLock };
		gS.coloredLights = a_on;
	}
	Unlock UnlockRule()
	{
		std::scoped_lock l{ gLock };
		return gS.unlock;
	}
	void SetUnlockRule(Unlock a_rule)
	{
		std::scoped_lock l{ gLock };
		gS.unlock = a_rule;
	}
	std::string UnlockPerk()
	{
		std::scoped_lock l{ gLock };
		return gS.perk;
	}
	void SetUnlockPerk(std::string a_perk)
	{
		std::scoped_lock l{ gLock };
		gS.perk = std::move(a_perk);
	}
	bool CrusaderOn()
	{
		std::scoped_lock l{ gLock };
		return gS.crusader;
	}
	void SetCrusaderOn(bool a_on)
	{
		std::scoped_lock l{ gLock };
		gS.crusader = a_on;
	}
	bool EveryWard()
	{
		std::scoped_lock l{ gLock };
		return gS.every;
	}
	void SetEveryWard(bool a_on)
	{
		std::scoped_lock l{ gLock };
		gS.every = a_on;
		for (auto& [plugin, on] : gS.mods) {
			on = a_on;
		}
	}
	ShieldDome ShieldDomeOf(std::size_t a_row)
	{
		std::scoped_lock l{ gLock };
		const auto       i = SettingsText::ShieldIndex(a_row);
		if (i >= gS.shieldDome.size()) {
			return ShieldDome::kNormal;  // not a shield row
		}
		return static_cast<ShieldDome>(SettingsText::ShieldPick(gS, i));
	}
	bool Any360Wanted()
	{
		return Dome360() || std::ranges::any_of(kShieldRows, [](std::size_t a_row) { return ShieldDomeOf(a_row) == ShieldDome::k360; });
	}
	bool ShieldWardOn(std::size_t a_row)
	{
		return ShieldDomeOf(a_row) != ShieldDome::kOff;
	}
	void SetShieldDome(std::size_t a_row, ShieldDome a_dome)
	{
		std::scoped_lock l{ gLock };
		if (const auto i = SettingsText::ShieldIndex(a_row); i < gS.shieldDome.size()) {
			gS.shieldDome[i] = std::clamp(static_cast<int>(a_dome), 0, 2);
		}
	}
	bool KeepStrangeRunes()
	{
		std::scoped_lock l{ gLock };
		return gS.keepRunes;
	}
	void SetKeepStrangeRunes(bool a_on)
	{
		std::scoped_lock l{ gLock };
		gS.keepRunes = a_on;
	}
	bool ModOn(std::string_view a_plugin)
	{
		const auto       k = Lower(a_plugin);
		std::scoped_lock l{ gLock };
		const auto       it = gS.mods.find(k);
		return it == gS.mods.end() ? gS.every : it->second;
	}
	void SetModOn(std::string_view a_plugin, bool a_on)
	{
		const auto       k = Lower(a_plugin);
		std::scoped_lock l{ gLock };
		gS.mods[k] = a_on;
	}

	void ApplyScheme(const Scheme& a_scheme)
	{
		std::scoped_lock l{ gLock };
		gS.ladder = a_scheme.ladder & 0xFFFFFF;  // colors only - stages and Reverse are the player's (his rule 2026-10-05)
		for (std::size_t i = 0; i < kRows; ++i) {
			const Color c = a_scheme.rows[i];
			gS.rows[i] = c ? RowPick{ RowMode::kCustom, c & 0xFFFFFF } : RowPick{ RowMode::kDefault, gS.rows[i].color };
		}
	}

	int StopPercent(std::size_t a_rank)
	{
		std::scoped_lock l{ gLock };
		return StopPercentOf(gS.stages, gS.reversed, a_rank);
	}

	std::optional<Color> RowColor(std::size_t a_row)
	{
		if (a_row < kRanks && StrangeWardsInstalled()) {
			return std::nullopt;
		}
		std::scoped_lock l{ gLock };
		return RowColorOf(gS, a_row);
	}
}
