// Dynamic Wards - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// DynamicWards.ini, written by the menu. 3.0 stores colors as RRGGBB; a 2.x file (numbered ladders and picks) is read
// and carried over, then written back in the new form.

#include "Plugin.h"

namespace Plugin
{
	namespace
	{
		constexpr const char* kPath = "Data/SKSE/Plugins/DynamicWards.ini";

		struct Row
		{
			RowMode mode = RowMode::kDefault;
			Color   color = kWhite;
		};

		std::mutex             gLock;
		std::array<Row, kRows> gRows{};
		Color                  gLadder = kPresets[0].color;
		int                    gStages = kMinStages;
		bool                   gReversed = false;
		int                    gOpacity = 100;
		int                    gTransparency = 0;
		int                    gDome = 0;
		bool                   gLight = true;
		bool                   gColoredLights = true;
		Unlock                 gUnlock = Unlock::kSkill75;
		std::string            gPerk;
		bool                   gCrusader = true;
		bool                   gEvery = true;
		bool                   gKeepRunes = false;  // Strange Runes loaded: leave the wards it restyles to it
		std::map<std::string, bool, std::less<>> gMods;  // [Compatibility] plugin=0/1, lowercased; missing = gEvery

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

		// 2.x's numbered picks: 0 ladder/default, 1-5 blue red gold green white, 6 vanilla, 7 purple
		Row FromOldPick(int a_pick)
		{
			constexpr Color kOld[] = { 0x2468FF, 0xFF3648, 0xFFBE5A, 0x2DC846, kWhite };
			if (a_pick >= 1 && a_pick <= 5) {
				return { RowMode::kCustom, kOld[a_pick - 1] };
			}
			if (a_pick == 6) {
				return { RowMode::kVanilla, kWhite };
			}
			if (a_pick == 7) {
				return { RowMode::kCustom, 0x9646FF };
			}
			return {};
		}
	}

	std::string HexColor(Color a_color)
	{
		return std::format("{:06X}", a_color & 0xFFFFFF);
	}

	std::optional<Color> ParseColor(std::string_view a_text)
	{
		while (!a_text.empty() && (a_text.front() == '#' || a_text.front() == ' ')) {
			a_text.remove_prefix(1);
		}
		if (a_text.starts_with("0x") || a_text.starts_with("0X")) {
			a_text.remove_prefix(2);
		}
		Color v = 0;
		const auto* end = a_text.data() + a_text.size();
		if (a_text.size() != 6) {
			return std::nullopt;
		}
		if (const auto [stop, ec] = std::from_chars(a_text.data(), end, v, 16); ec != std::errc{} || stop != end) {
			return std::nullopt;
		}
		return v;
	}

	Color Mix(Color a_from, Color a_to, int a_percent)
	{
		const int k = std::clamp(a_percent, 0, 100);
		auto      ch = [&](int a_shift) {
            const int a = (a_from >> a_shift) & 0xFF;
            const int b = (a_to >> a_shift) & 0xFF;
            return static_cast<Color>(std::clamp(a + ((b - a) * k + 50) / 100, 0, 255)) << a_shift;
		};
		return ch(16) | ch(8) | ch(0);
	}

	void LoadSettings()
	{
		std::ifstream    in(kPath);
		std::string      line, section;
		std::size_t      read = 0;
		bool             old = false;
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
				} else if (key == "LadderColor") {
					gLadder = ParseColor(val).value_or(gLadder);
				} else if (key == "Ladder") {  // 2.x: 0-4 blue red gold green purple
					constexpr Color kOld[] = { 0x2468FF, 0xFF3648, 0xFFBE5A, 0x2DC846, 0x9646FF };
					gLadder = kOld[std::clamp(v, 0, 4)];
					old = true;
				} else if (key == "LadderStages") {
					gStages = std::clamp(v, kMinStages, kMaxStages);
				} else if (key == "LadderReversed") {
					gReversed = v != 0;
				} else if (key == "Opacity") {
					gOpacity = std::clamp(v, kOpacityMin, 100);
				} else if (key == "Transparency") {
					gTransparency = std::clamp(v, 0, kTransparencyMax);
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
				} else if (key == "KeepStrangeRunesWards") {
					gKeepRunes = v != 0;
				}
			} else if (section == "Compatibility") {
				gMods[Lower(key)] = v != 0;
			} else if (const auto row = RowIndex(key); row < kRows && section == "Colors") {
				if (Lower(val) == "vanilla") {
					gRows[row] = { RowMode::kVanilla, gRows[row].color };
				} else if (Lower(val) == "default") {
					gRows[row] = { RowMode::kDefault, gRows[row].color };
				} else if (const auto c = ParseColor(val)) {
					gRows[row] = { RowMode::kCustom, *c };
				} else {
					gRows[row] = FromOldPick(v);
					old = true;
				}
			}
		}
		SKSE::log::info("settings: ladder {} ({} stages{}), opacity {}%, transparency {}%, dome {}, ward light {}, colored lights {}, 360 unlock rule {}{}, "
						"Crusader shields {}, every ward {} ({} line(s) read{})",
			HexColor(gLadder), gStages, gReversed ? ", reversed" : "", gOpacity, gTransparency, gDome, gLight ? "on" : "off", gColoredLights ? "on" : "off",
			static_cast<int>(gUnlock), gPerk.empty() ? "" : " " + gPerk, gCrusader ? "on" : "off", gEvery ? "on" : "off", read,
			old ? "; a 2.x file, carried over" : "");
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
			<< "[Settings]\nLadderColor=" << HexColor(gLadder) << "\nLadderStages=" << gStages << "\nLadderReversed=" << (gReversed ? 1 : 0)
			<< "\nOpacity=" << gOpacity << "\nTransparency=" << gTransparency << "\nDome=" << gDome << "\nWardLight=" << (gLight ? 1 : 0)
			<< "\nColoredLights=" << (gColoredLights ? 1 : 0) << "\nUnlock360=" << static_cast<int>(gUnlock) << "\nUnlock360Perk=" << gPerk
			<< "\nCrusaderShields=" << (gCrusader ? 1 : 0) << "\nEveryWard=" << (gEvery ? 1 : 0)
			<< "\nKeepStrangeRunesWards=" << (gKeepRunes ? 1 : 0) << "\n[Colors]\n";
		for (std::size_t i = 0; i < kRows; ++i) {
			const auto& r = gRows[i];
			out << kTokens[i] << "=" << (r.mode == RowMode::kVanilla ? std::string("vanilla") : r.mode == RowMode::kDefault ? std::string("default") : HexColor(r.color))
				<< "\n";
		}
		out << "[Compatibility]\n";
		for (const auto& [plugin, on] : gMods) {
			out << plugin << "=" << (on ? 1 : 0) << "\n";
		}
	}

	RowMode RowModeOf(std::size_t a_row)
	{
		std::scoped_lock l{ gLock };
		return a_row < kRows ? gRows[a_row].mode : RowMode::kDefault;
	}
	Color RowCustom(std::size_t a_row)
	{
		std::scoped_lock l{ gLock };
		return a_row < kRows ? gRows[a_row].color : kWhite;
	}
	void SetRow(std::size_t a_row, RowMode a_mode, Color a_color)
	{
		std::scoped_lock l{ gLock };
		if (a_row < kRows) {
			gRows[a_row] = { a_mode, a_color & 0xFFFFFF };
		}
	}
	Color LadderColor()
	{
		std::scoped_lock l{ gLock };
		return gLadder;
	}
	void SetLadderColor(Color a_color)
	{
		std::scoped_lock l{ gLock };
		gLadder = a_color & 0xFFFFFF;
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
	int Opacity()
	{
		std::scoped_lock l{ gLock };
		return gOpacity;
	}
	void SetOpacity(int a_percent)
	{
		std::scoped_lock l{ gLock };
		gOpacity = std::clamp(a_percent, kOpacityMin, 100);
	}
	int Transparency()
	{
		std::scoped_lock l{ gLock };
		return gTransparency;
	}
	void SetTransparency(int a_percent)
	{
		std::scoped_lock l{ gLock };
		gTransparency = std::clamp(a_percent, 0, kTransparencyMax);
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
	bool KeepStrangeRunes()
	{
		std::scoped_lock l{ gLock };
		return gKeepRunes;
	}
	void SetKeepStrangeRunes(bool a_on)
	{
		std::scoped_lock l{ gLock };
		gKeepRunes = a_on;
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

	int StopPercent(std::size_t a_rank)
	{
		int  stages = 0;
		bool rev = false;
		{
			std::scoped_lock l{ gLock };
			stages = gStages;
			rev = gReversed;
		}
		auto pick = [&](const auto& a_stops) {
			const auto n = a_stops.size();
			const auto pos = (std::min)(a_rank, n - 1);  // ranks past the ladder hold its last stop
			return a_stops[rev ? n - 1 - pos : pos];
		};
		return stages == 5 ? pick(kStages5) : stages == 4 ? pick(kStages4) : pick(kStages3);
	}

	std::optional<Color> RowColor(std::size_t a_row)
	{
		if (a_row >= kRows) {
			return std::nullopt;
		}
		Row   r;
		Color ladder;
		{
			std::scoped_lock l{ gLock };
			r = gRows[a_row];
			ladder = gLadder;
		}
		switch (r.mode) {
		case RowMode::kVanilla:
			return std::nullopt;
		case RowMode::kCustom:
			return r.color;
		default:
			return a_row >= kRanks ? kRowDefault[a_row] : Mix(kWhite, ladder, StopPercent(a_row));
		}
	}
}
