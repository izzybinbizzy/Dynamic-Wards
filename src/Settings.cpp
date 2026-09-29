// Dynamic Wards - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.

#include "Plugin.h"

namespace Plugin
{
	namespace
	{
		constexpr const char* kPath = "Data/SKSE/Plugins/DynamicWards.ini";

		std::mutex             gLock;
		std::array<int, kRows> gColor{};  // Pick: 0 ladder or the row's default, 1-5 a color, 6 vanilla, 7 purple
		int                    gLadder = 0;
		int                    gStages = kMinStages;
		bool                   gReversed = false;
		int                    gDome = 0;
		bool                   gLight = true;
		bool                   gColoredLights = true;
		Unlock                 gUnlock = Unlock::kSkill75;
		std::string            gPerk;
		bool                   gCrusader = true;
		bool                   gEvery = true;
		std::map<std::string, bool, std::less<>> gMods;  // [Compatibility] plugin=0/1, lowercased (Plugin::Lower); missing = gEvery

		std::string Trim(std::string s)
		{
			while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
				s.pop_back();
			}
			std::size_t i = 0;
			while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
				++i;
			}
			return s.substr(i);
		}

		std::size_t RowIndex(std::string_view a_token)
		{
			for (std::size_t i = 0; i < kRows; ++i) {
				if (kTokens[i] == a_token) {
					return i;
				}
			}
			return kRows;
		}
	}

	void LoadSettings()
	{
		std::ifstream    in(kPath);
		std::string      line, section;
		std::size_t      read = 0;
		std::scoped_lock l{ gLock };
		while (in && std::getline(in, line)) {
			line = Trim(line);
			if (line.empty() || line[0] == ';' || line[0] == '#') {
				continue;
			}
			if (line.front() == '[' && line.back() == ']') {
				section = line.substr(1, line.size() - 2);
				continue;
			}
			const auto eq = line.find('=');
			if (eq == std::string::npos) {
				continue;
			}
			const auto key = Trim(line.substr(0, eq));
			const auto val = Trim(line.substr(eq + 1));
			int        v = 0;
			std::from_chars(val.data(), val.data() + val.size(), v);
			++read;
			if (section == "Settings") {
				if (key == "WardLight") {
					gLight = v != 0;
				} else if (key == "ColoredLights") {
					gColoredLights = v != 0;
				} else if (key == "Ladder") {
					gLadder = std::clamp(v, 0, static_cast<int>(kLadders) - 1);
				} else if (key == "LadderStages") {
					gStages = std::clamp(v, kMinStages, kMaxStages);
				} else if (key == "LadderReversed") {
					gReversed = v != 0;
				} else if (key == "Dome") {
					gDome = std::clamp(v, 0, 1);
				} else if (key == "Unlock360") {
					gUnlock = static_cast<Unlock>(std::clamp(v, 0, 4));
				} else if (key == "Unlock360Perk") {
					gPerk = val;
				} else if (key == "CrusaderShields") {
					gCrusader = v != 0;
				} else if (key == "EveryWard") {
					gEvery = v != 0;
				}
			} else if (section == "Compatibility") {
				gMods[Lower(key)] = v != 0;
			} else if (const auto row = RowIndex(key); row < kRows) {
				if (section == "Colors") {
					gColor[row] = std::clamp(v, 0, static_cast<int>(Pick::kPurple));
				} else if (section == "Ranks" && v != 0) {
					gColor[row] = static_cast<int>(Pick::kVanilla);  // the first 2.0 test builds kept a Vanilla switch per row
				}
			}
		}
		SKSE::log::info("settings: ladder {} ({} stages{}), dome {}, ward light {}, colored lights {}, 360 unlock rule {}{}, "
						"Crusader shields {}, every ward {} ({} line(s) read)",
			gLadder, gStages, gReversed ? ", reversed" : "", gDome, gLight ? "on" : "off", gColoredLights ? "on" : "off", static_cast<int>(gUnlock), gPerk.empty() ? "" : " " + gPerk,
			gCrusader ? "on" : "off", gEvery ? "on" : "off", read);
	}

	void SaveSettings()
	{
		std::scoped_lock l{ gLock };
		std::ofstream    out(kPath, std::ios::trunc);
		if (!out) {
			SKSE::log::warn("settings: {} could not be written", kPath);
			return;
		}
		out << "; Dynamic Wards - written by its menu (SKSE Menu Framework). Change these in game, not here.\n"
			<< "[Settings]\nLadder=" << gLadder << "\nLadderStages=" << gStages << "\nLadderReversed=" << (gReversed ? 1 : 0)
			<< "\nDome=" << gDome << "\nWardLight=" << (gLight ? 1 : 0)
			<< "\nColoredLights=" << (gColoredLights ? 1 : 0) << "\nUnlock360=" << static_cast<int>(gUnlock) << "\nUnlock360Perk=" << gPerk
			<< "\nCrusaderShields=" << (gCrusader ? 1 : 0) << "\nEveryWard=" << (gEvery ? 1 : 0) << "\n[Colors]\n";
		for (std::size_t i = 0; i < kRows; ++i) {
			out << kTokens[i] << "=" << gColor[i] << "\n";
		}
		out << "[Compatibility]\n";
		for (const auto& [plugin, on] : gMods) {
			out << plugin << "=" << (on ? 1 : 0) << "\n";
		}
	}

	Pick RowPick(std::size_t a_row)
	{
		std::scoped_lock l{ gLock };
		return a_row < kRows ? static_cast<Pick>(gColor[a_row]) : Pick::kDefault;
	}
	void SetRowPick(std::size_t a_row, Pick a_pick)
	{
		std::scoped_lock l{ gLock };
		if (a_row < kRows) {
			gColor[a_row] = static_cast<int>(a_pick);
		}
	}
	int Ladder()
	{
		std::scoped_lock l{ gLock };
		return gLadder;
	}
	void SetLadder(int a_ladder)
	{
		std::scoped_lock l{ gLock };
		gLadder = std::clamp(a_ladder, 0, static_cast<int>(kLadders) - 1);
	}
	int LadderStages()
	{
		std::scoped_lock l{ gLock };
		return gStages;
	}
	void SetLadderStages(int a_stages)
	{
		std::scoped_lock l{ gLock };
		gStages = std::clamp(a_stages, kMinStages, kMaxStages);
	}
	bool LadderReversed()
	{
		std::scoped_lock l{ gLock };
		return gReversed;
	}
	void SetLadderReversed(bool a_on)
	{
		std::scoped_lock l{ gLock };
		gReversed = a_on;
	}
	bool Dome360()
	{
		std::scoped_lock l{ gLock };
		return gDome == 0;
	}
	void SetDome360(bool a_on)
	{
		std::scoped_lock l{ gLock };
		gDome = a_on ? 0 : 1;
	}
	bool WardLightOn()
	{
		std::scoped_lock l{ gLock };
		return gLight;
	}
	void SetWardLightOn(bool a_on)
	{
		std::scoped_lock l{ gLock };
		gLight = a_on;
	}
	bool ColoredLightsOn()
	{
		std::scoped_lock l{ gLock };
		return gColoredLights;
	}
	void SetColoredLightsOn(bool a_on)
	{
		std::scoped_lock l{ gLock };
		gColoredLights = a_on;
	}
	Unlock UnlockRule()
	{
		std::scoped_lock l{ gLock };
		return gUnlock;
	}
	void SetUnlockRule(Unlock a_rule)
	{
		std::scoped_lock l{ gLock };
		gUnlock = a_rule;
	}
	std::string UnlockPerk()
	{
		std::scoped_lock l{ gLock };
		return gPerk;
	}
	void SetUnlockPerk(std::string a_perk)
	{
		std::scoped_lock l{ gLock };
		gPerk = std::move(a_perk);
	}
	bool CrusaderOn()
	{
		std::scoped_lock l{ gLock };
		return gCrusader;
	}
	void SetCrusaderOn(bool a_on)
	{
		std::scoped_lock l{ gLock };
		gCrusader = a_on;
	}
	bool EveryWard()
	{
		std::scoped_lock l{ gLock };
		return gEvery;
	}
	void SetEveryWard(bool a_on)
	{
		std::scoped_lock l{ gLock };
		gEvery = a_on;
		for (auto& [plugin, on] : gMods) {
			on = a_on;
		}
	}
	bool ModOn(std::string_view a_plugin)
	{
		const auto       k = Lower(a_plugin);
		std::scoped_lock l{ gLock };
		const auto       it = gMods.find(k);
		return it == gMods.end() ? gEvery : it->second;
	}
	void SetModOn(std::string_view a_plugin, bool a_on)
	{
		const auto       k = Lower(a_plugin);
		std::scoped_lock l{ gLock };
		gMods[k] = a_on;
	}
}
