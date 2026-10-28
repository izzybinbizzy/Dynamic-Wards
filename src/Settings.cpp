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

		std::mutex       gLock;
		SettingsValues   gS;                // under gLock
		std::atomic_bool gLoaded{ false };  // nothing is saved before the file was read: an early save would write the defaults

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
		gLoaded = true;
		SKSE::log::info(
			"settings: ladder {} ({} stages{}), opacity {}%, transparency {}%, brightness {}%, hand brightness {}%, dome color boost {}%, dome {}, ward light {}, colored lights {}, 360 unlock rule {}{}, "
			"Crusader shields {}, every ward {}, shield wards Spellbreaker {} Crusader {} Reman {}, always-on ward bar {} ({} line(s) read{})",
			HexColor(gS.ladder), gS.stages, gS.reversed ? ", reversed" : "", gS.opacity, gS.transparency, gS.brightness, gS.castingGlow, gS.domeBoost, gS.dome,
			gS.light ? "on" : "off", gS.coloredLights ? "on" : "off", static_cast<int>(gS.unlock), gS.perk.empty() ? "" : " " + gS.perk,
			gS.crusader ? "on" : "off", gS.every ? "on" : "off", ShieldDomeName(SettingsText::ShieldPick(gS, 0)),
			ShieldDomeName(SettingsText::ShieldPick(gS, 1)), ShieldDomeName(SettingsText::ShieldPick(gS, 2)), gS.alwaysOnBar ? "on" : "off", read.lines,
			read.old ? "; a 2.x file, carried over" : "");
	}

	// Any thread (the menu queues it onto the main thread; DevBench saves on its own). Written beside the file and moved over
	// it, as RELight - Spell Addon and Illuminated save theirs: a crash or a full disk mid-write leaves the old file, never half
	// of one. One save at a time, and each takes its snapshot inside that turn, so the last file written holds the newest settings
	void SaveSettings()
	{
		if (!gLoaded) {
			SKSE::log::warn("settings: not saved - {} has not been read yet", kPath);
			return;
		}
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

	namespace
	{
		template <class F>
		auto Read(F a_f)
		{
			std::scoped_lock l{ gLock };
			return a_f(std::as_const(gS));
		}

		// every change ends in Normalize: the ranges live in one place, shared with the ini reader
		template <class F>
		void Change(F a_f)
		{
			std::scoped_lock l{ gLock };
			a_f(gS);
			Normalize(gS);
		}
	}

	RowMode RowModeOf(std::size_t a_row)
	{
		return Read([&](const auto& s) { return a_row < kRows ? s.rows[a_row].mode : RowMode::kDefault; });
	}
	Color RowCustom(std::size_t a_row)
	{
		return Read([&](const auto& s) { return a_row < kRows ? s.rows[a_row].color : kWhite; });
	}
	void SetRow(std::size_t a_row, RowMode a_mode, Color a_color)
	{
		Change([&](auto& s) {
			if (a_row < kRows) {
				s.rows[a_row] = { a_mode, a_color };
			}
		});
	}
	LookPick LookOf(std::size_t a_look)
	{
		return Read([&](const auto& s) { return a_look < kLooks ? s.looks[a_look] : LookPick{}; });
	}
	std::array<LookPick, kLooks> Looks()
	{
		return Read([](const auto& s) { return s.looks; });
	}
	void SetLook(std::size_t a_look, LookPick a_pick)
	{
		Change([&](auto& s) {
			if (a_look < kLooks && (a_pick.mode != LookMode::kDefault || a_look >= kGroups)) {
				s.looks[a_look] = a_pick;
			}
		});
	}
	Color LadderColor()
	{
		return Read([](const auto& s) { return s.ladder; });
	}
	void SetLadderColor(Color a_color)
	{
		Change([&](auto& s) { s.ladder = a_color; });
	}
	int LadderStages()
	{
		return Read([](const auto& s) { return s.stages; });
	}
	void SetLadderStages(int a_stages)
	{
		Change([&](auto& s) { s.stages = a_stages; });
	}
	bool LadderReversed()
	{
		return Read([](const auto& s) { return s.reversed; });
	}
	void SetLadderReversed(bool a_on)
	{
		Change([&](auto& s) { s.reversed = a_on; });
	}
	int Opacity()
	{
		return Read([](const auto& s) { return s.opacity; });
	}
	void SetOpacity(int a_percent)
	{
		Change([&](auto& s) { s.opacity = a_percent; });
	}
	int Brightness()
	{
		return Read([](const auto& s) { return s.brightness; });
	}
	void SetBrightness(int a_percent)
	{
		Change([&](auto& s) { s.brightness = a_percent; });
	}
	float LightDim()
	{
		return Read([](const auto& s) { return s.opacity / 100.0f * s.brightness / 100.0f; });
	}
	int CastingGlow()
	{
		return Read([](const auto& s) { return s.castingGlow; });
	}
	void SetCastingGlow(int a_percent)
	{
		Change([&](auto& s) { s.castingGlow = a_percent; });
	}
	int DomeBoost()
	{
		return Read([](const auto& s) { return s.domeBoost; });
	}
	void SetDomeBoost(int a_percent)
	{
		Change([&](auto& s) { s.domeBoost = a_percent; });
	}
	float HandDim()
	{
		return Read([](const auto& s) { return s.castingGlow / 100.0f; });
	}
	int Transparency()
	{
		return Read([](const auto& s) { return s.transparency; });
	}
	void SetTransparency(int a_percent)
	{
		Change([&](auto& s) { s.transparency = a_percent; });
	}
	bool Dome360()
	{
		return Read([](const auto& s) { return s.dome == 0; });
	}
	void SetDome360(bool a_on)
	{
		Change([&](auto& s) { s.dome = a_on ? 0 : 1; });
	}
	bool WardLightOn()
	{
		return Read([](const auto& s) { return s.light; });
	}
	void SetWardLightOn(bool a_on)
	{
		Change([&](auto& s) { s.light = a_on; });
	}
	bool ColoredLightsOn()
	{
		return Read([](const auto& s) { return s.coloredLights; });
	}
	void SetColoredLightsOn(bool a_on)
	{
		Change([&](auto& s) { s.coloredLights = a_on; });
	}
	Unlock UnlockRule()
	{
		return Read([](const auto& s) { return s.unlock; });
	}
	void SetUnlockRule(Unlock a_rule)
	{
		Change([&](auto& s) { s.unlock = a_rule; });
	}
	std::string UnlockPerk()
	{
		return Read([](const auto& s) { return s.perk; });
	}
	void SetUnlockPerk(std::string a_perk)
	{
		Change([&](auto& s) { s.perk = std::move(a_perk); });
	}
	bool CrusaderOn()
	{
		return Read([](const auto& s) { return s.crusader; });
	}
	void SetCrusaderOn(bool a_on)
	{
		Change([&](auto& s) { s.crusader = a_on; });
	}
	bool EveryWard()
	{
		return Read([](const auto& s) { return s.every; });
	}
	void SetEveryWard(bool a_on)
	{
		Change([&](auto& s) {
			s.every = a_on;
			for (auto& [plugin, on] : s.mods) {
				on = a_on;
			}
		});
	}
	ShieldDome ShieldDomeOf(std::size_t a_row)
	{
		return Read([&](const auto& s) {
			const auto i = SettingsText::ShieldIndex(a_row);
			return i < s.shieldDome.size() ? static_cast<ShieldDome>(SettingsText::ShieldPick(s, i)) : ShieldDome::kNormal;  // not a shield row: normal
		});
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
		Change([&](auto& s) {
			if (const auto i = SettingsText::ShieldIndex(a_row); i < s.shieldDome.size()) {
				s.shieldDome[i] = static_cast<int>(a_dome);
			}
		});
	}
	bool KeepStrangeRunes()
	{
		return Read([](const auto& s) { return s.keepRunes; });
	}
	void SetKeepStrangeRunes(bool a_on)
	{
		Change([&](auto& s) { s.keepRunes = a_on; });
	}
	bool AlwaysOnBar()
	{
		return Read([](const auto& s) { return s.alwaysOnBar; });
	}
	void SetAlwaysOnBar(bool a_on)
	{
		Change([&](auto& s) { s.alwaysOnBar = a_on; });
	}
	bool ModOn(std::string_view a_plugin)
	{
		return Read([k = Lower(a_plugin)](const auto& s) {
			const auto it = s.mods.find(k);
			return it == s.mods.end() ? s.every : it->second;
		});
	}
	void SetModOn(std::string_view a_plugin, bool a_on)
	{
		Change([k = Lower(a_plugin), a_on](auto& s) { s.mods[k] = a_on; });
	}

	void ApplyScheme(const Scheme& a_scheme)
	{
		std::scoped_lock l{ gLock };
		gS.ladder = a_scheme.ladder & 0xFFFFFF;  // colors only - stages and Reverse are the player's (his rule 2026-10-05)
		for (std::size_t i = 0; i < kRows; ++i) {
			const Color c = a_scheme.rows[i];
			if (gS.rows[i].mode == RowMode::kVanilla) {
				continue;  // a preset changes colors only; a row the player set to the game's own art stays so
			}
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

	std::optional<Color> SlotColor(std::size_t a_slot)
	{
		if (a_slot < kRows) {
			return RowColor(a_slot);
		}
		std::scoped_lock l{ gLock };
		if (a_slot < kSlots && gS.looks[a_slot - kRows].mode == LookMode::kCustom) {
			return gS.looks[a_slot - kRows].color;
		}
		return std::nullopt;
	}
}
