// Dynamic Wards - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// DynamicWards.ini as text, game-free (tests\testwards.cpp reads and writes it without the game): every setting in ONE
// table (SettingsValues), its reader and its writer. Settings.cpp keeps the live table under its lock, reads the file at data
// load and writes it - beside the file, then moved over it - after every change. 3.0 stores colors as RRGGBB; a 2.x file
// (numbered ladders and picks) is read and carried over, then written back in the new form.

#pragma once

#include "WardMath.h"

#include <array>
#include <functional>
#include <istream>
#include <map>
#include <ostream>
#include <string>

namespace Plugin
{
	struct RowPick
	{
		RowMode mode = RowMode::kDefault;
		Color   color = kWhite;
	};

	// every setting the menu shows, with its default (his defaults 2026-10-06: Transparency 0, Opacity 100, Ward brightness
	// 100, Hand Brightness 100)
	struct SettingsValues
	{
		std::array<RowPick, kRows>               rows{};
		std::array<LookPick, kLooks>             looks = DefaultLooks();  // [Others]: other casters' looks
		Color                                    ladder = kPresets[0].color;
		int                                      stages = kMinStages;
		bool                                     reversed = false;
		int                                      opacity = 100;
		int                                      transparency = kTransparencyDefault;
		int                                      brightness = 100;
		int                                      castingGlow = 100;  // the menu's Hand Brightness (the ini key stays CastingGlow)
		int                                      domeBoost = 0;      // the menu's Dome color boost, 0-100 percent; 0 = off
		int                                      dome = 0;           // 0 the 360 dome, 1 the normal dome
		bool                                     light = true;       // Ward casting light
		bool                                     coloredLights = true;
		Unlock                                   unlock = Unlock::kSkill75;
		std::string                              perk;
		bool                                     crusader = true;
		bool                                     every = true;
		bool                                     keepRunes = false;  // Strange Runes loaded: leave the wards it restyles to it
		std::map<std::string, bool, std::less<>> mods;               // [Compatibility] plugin=0/1, lowercased; missing = every
		// ShieldDome per kShieldRows entry (0 off, 1 normal, 2 360); -1 = not set yet - ReadSettingsText always sets it, so only a
		// table that was never read (a test's) meets ShieldPick's -1 branch
		std::array<int, kShieldRows.size()> shieldDome{ -1, -1, -1 };
	};

	struct SettingsRead
	{
		std::size_t lines = 0;    // key lines read
		bool        old = false;  // a 2.x file, carried over
	};

	namespace SettingsText
	{
		inline std::size_t ShieldIndex(std::size_t a_row)
		{
			const auto it = std::ranges::find(kShieldRows, a_row);
			return static_cast<std::size_t>(it - kShieldRows.begin());
		}
		// a shield's pick, an unset one following the Dome pick (the old on switch: the dome the spell wards wear)
		inline int ShieldPick(const SettingsValues& a_s, std::size_t a_index)
		{
			const int d = a_index < a_s.shieldDome.size() ? a_s.shieldDome[a_index] : -1;
			return d >= 0 ? d : a_s.dome == 0 ? static_cast<int>(ShieldDome::k360) :
			                                    static_cast<int>(ShieldDome::kNormal);
		}
		inline std::string ShieldKey(std::size_t a_row)
		{
			return std::format("{}Ward", kTokens[a_row]);  // SpellbreakerWard, CrusaderWard, RemanWard: the 2026-10-08 on / off (kept for older versions)
		}
		inline std::string ShieldDomeKey(std::size_t a_row)
		{
			return std::format("{}Dome", kTokens[a_row]);  // SpellbreakerDome, CrusaderDome, RemanDome: 0 off, 1 normal dome, 2 360 dome
		}

		// 2.x's numbered picks: 0 ladder/default, 1-5 blue red gold green white, 6 vanilla, 7 purple
		inline RowPick FromOldPick(int a_pick)
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

	// a look's line in [Others]: RRGGBB, or mine / vanilla / default (a faction only) followed by the color it keeps for when it is
	// switched back to its own color (a line without one, before 4.0's, keeps a_keep)
	inline std::optional<LookPick> ParseLook(std::string_view a_text, std::size_t a_look, Color a_keep)
	{
		const auto space = a_text.find(' ');
		const auto word = a_text.substr(0, space);
		if (space != std::string_view::npos) {
			auto rest = a_text.substr(space + 1);
			rest.remove_prefix(std::min(rest.find_first_not_of(' '), rest.size()));
			const auto kept = ParseColor(rest);
			if (!kept) {
				return std::nullopt;
			}
			a_keep = *kept;
		}
		if (SameText(word, "mine")) {
			return LookPick{ LookMode::kMine, a_keep };
		}
		if (SameText(word, "vanilla")) {
			return LookPick{ LookMode::kVanilla, a_keep };
		}
		if (SameText(word, "default")) {
			return a_look >= kGroups ? std::optional<LookPick>(LookPick{ LookMode::kDefault, a_keep }) : std::nullopt;
		}
		if (const auto c = space == std::string_view::npos ? ParseColor(a_text) : std::nullopt) {
			return LookPick{ LookMode::kCustom, *c };
		}
		return std::nullopt;
	}

	inline std::string LookText(const LookPick& a_p)
	{
		switch (a_p.mode) {
		case LookMode::kVanilla:
			return "vanilla " + HexColor(a_p.color);
		case LookMode::kDefault:
			return "default " + HexColor(a_p.color);
		case LookMode::kCustom:
			return HexColor(a_p.color);
		default:
			return "mine " + HexColor(a_p.color);
		}
	}

	// a look of a token ("Thalmor" -> its index), any case; kLooks when it is none
	inline std::size_t LookOfToken(std::string_view a_token)
	{
		for (std::size_t i = 0; i < kLooks; ++i) {
			if (SameText(kLookTokens[i], a_token)) {
				return i;
			}
		}
		return kLooks;
	}

	// every value in its slider's range: the ini reader and every setter end here, so the two can never disagree
	inline void Normalize(SettingsValues& a_s)
	{
		a_s.ladder &= 0xFFFFFF;
		a_s.stages = std::clamp(a_s.stages, kMinStages, kMaxStages);
		a_s.opacity = std::clamp(a_s.opacity, kOpacityMin, 100);
		a_s.transparency = std::clamp(a_s.transparency, 0, kTransparencyMax);
		a_s.brightness = std::clamp(a_s.brightness, kBrightnessMin, kBrightnessMax);
		a_s.castingGlow = std::clamp(a_s.castingGlow, kBrightnessMin, kBrightnessMax);
		a_s.domeBoost = std::clamp(a_s.domeBoost, 0, 100);
		a_s.dome = std::clamp(a_s.dome, 0, 1);
		for (auto& r : a_s.rows) {
			r.color &= 0xFFFFFF;
		}
		for (auto& l : a_s.looks) {
			l.color &= 0xFFFFFF;
		}
		for (auto& d : a_s.shieldDome) {
			d = std::clamp(d, -1, 2);
		}
	}

	// The file's text into a_out, over the values it already holds (the defaults, for a fresh table). Sections and keys in
	// any case, as RELight - Spell Addon and Illuminated read theirs; a hand-edited number that does not read keeps the value
	// it had instead of falling to the slider's floor; every number is clamped to its slider's range
	inline SettingsRead ReadSettingsText(std::istream& a_in, SettingsValues& a_out)
	{
		using namespace SettingsText;
		SettingsRead                        result;
		std::string                         line, section;
		std::array<int, kShieldRows.size()> oldSwitch{ -1, -1, -1 };  // the 2026-10-08 on / off keys, when the file has them
		std::array<int, kShieldRows.size()> picks{ -1, -1, -1 };      // the dome picks the file has
		while (a_in && std::getline(a_in, line)) {
			line = Trim(line);
			if (line.empty() || line[0] == ';' || line[0] == '#') {
				continue;
			}
			if (line.front() == '[' && line.back() == ']') {
				section = Trim(std::string_view(line).substr(1, line.size() - 2));
				continue;
			}
			// [Compatibility]'s key is a plugin name, which may hold '=': its value is the text after the last one
			const auto eq = SameText(section, "Compatibility") ? line.rfind('=') : line.find('=');
			if (eq == std::string::npos) {
				continue;
			}
			const auto key = Trim(std::string_view(line).substr(0, eq));
			const auto val = Trim(std::string_view(line).substr(eq + 1));
			int        v = 0;
			const auto [end, ec] = std::from_chars(val.data(), val.data() + val.size(), v);
			const bool number = ec == std::errc{} && end == val.data() + val.size();
			++result.lines;
			auto       is = [&key](std::string_view a_name) { return SameText(key, a_name); };
			const bool settings = SameText(section, "Settings");
			if (settings && !number && !is("LadderColor") && !is("Unlock360Perk")) {
				continue;
			}
			if (settings) {
				if (is("WardLight")) {
					a_out.light = v != 0;
				} else if (is("ColoredLights")) {
					a_out.coloredLights = v != 0;
				} else if (is("LadderColor")) {
					a_out.ladder = ParseColor(val).value_or(a_out.ladder);
				} else if (is("Ladder")) {  // 2.x: 0-4 blue red gold green purple
					constexpr Color kOld[] = { 0x2468FF, 0xFF3648, 0xFFBE5A, 0x2DC846, 0x9646FF };
					a_out.ladder = kOld[std::clamp(v, 0, 4)];
					result.old = true;
				} else if (is("LadderStages")) {
					a_out.stages = v;
				} else if (is("LadderReversed")) {
					a_out.reversed = v != 0;
				} else if (is("Opacity")) {
					a_out.opacity = v;
				} else if (is("Transparency")) {
					a_out.transparency = v;
				} else if (is("Brightness")) {
					a_out.brightness = v;
				} else if (is("CastingGlow")) {
					a_out.castingGlow = v;
				} else if (is("DomeColorBoost")) {
					a_out.domeBoost = v;
				} else if (is("Dome")) {
					a_out.dome = std::clamp(v, 0, 1);
				} else if (is("Unlock360")) {
					a_out.unlock = static_cast<Unlock>(std::clamp(v, 0, 4));
				} else if (is("Unlock360Perk")) {
					a_out.perk = val;
				} else if (is("CrusaderShields")) {
					a_out.crusader = v != 0;
				} else if (is("EveryWard")) {
					a_out.every = v != 0;
				} else if (is("KeepStrangeRunesWards")) {
					a_out.keepRunes = v != 0;
				} else {
					for (const auto row : kShieldRows) {
						if (is(ShieldKey(row))) {
							oldSwitch[ShieldIndex(row)] = v != 0 ? 1 : 0;
						} else if (is(ShieldDomeKey(row))) {
							picks[ShieldIndex(row)] = std::clamp(v, 0, 2);
						}
					}
				}
			} else if (SameText(section, "Compatibility")) {
				a_out.mods[Lower(key)] = number && v != 0;  // a word ("off") reads as off, as it always did
			} else if (SameText(section, "Others")) {
				if (const auto look = LookOfToken(key); look < kLooks) {
					if (const auto p = ParseLook(val, look, a_out.looks[look].color)) {
						a_out.looks[look] = *p;
					}
				}
			} else if (const auto row = RowOfToken(key); row < kRows && SameText(section, "Colors")) {
				auto& r = a_out.rows[row];
				if (SameText(val, "vanilla")) {
					r.mode = RowMode::kVanilla;
				} else if (SameText(val, "default")) {
					r.mode = RowMode::kDefault;
				} else if (const auto c = ParseColor(val)) {
					r = { RowMode::kCustom, *c };
				} else if (number) {
					r = FromOldPick(v);
					result.old = true;
				}
			}
		}
		// a shield with no pick in the file keeps what its old switch did: off stays off, on (or no line) wears the dome the
		// spell wards wear
		for (std::size_t i = 0; i < picks.size(); ++i) {
			if (picks[i] >= 0) {
				a_out.shieldDome[i] = picks[i];
			} else if (oldSwitch[i] >= 0 || a_out.shieldDome[i] < 0) {
				a_out.shieldDome[i] = oldSwitch[i] == 0 ? static_cast<int>(ShieldDome::kOff) : (a_out.dome == 0 ? static_cast<int>(ShieldDome::k360) : static_cast<int>(ShieldDome::kNormal));
			}
		}
		Normalize(a_out);
		return result;
	}

	// every setting as DynamicWards.ini carries it
	inline void WriteSettingsText(const SettingsValues& a_s, std::ostream& a_out)
	{
		using namespace SettingsText;
		a_out << "; Dynamic Wards - written by its menu (SKSE Menu Framework). Change these in game, not here.\n"
			  << "[Settings]\nLadderColor=" << HexColor(a_s.ladder) << "\nLadderStages=" << a_s.stages << "\nLadderReversed=" << (a_s.reversed ? 1 : 0)
			  << "\nOpacity=" << a_s.opacity << "\nTransparency=" << a_s.transparency << "\nBrightness=" << a_s.brightness
			  << "\nCastingGlow=" << a_s.castingGlow << "\nDomeColorBoost=" << a_s.domeBoost << "\nDome=" << a_s.dome << "\nWardLight=" << (a_s.light ? 1 : 0)
			  << "\nColoredLights=" << (a_s.coloredLights ? 1 : 0) << "\nUnlock360=" << static_cast<int>(a_s.unlock) << "\nUnlock360Perk=" << a_s.perk
			  << "\nCrusaderShields=" << (a_s.crusader ? 1 : 0) << "\nEveryWard=" << (a_s.every ? 1 : 0)
			  << "\nKeepStrangeRunesWards=" << (a_s.keepRunes ? 1 : 0);
		for (const auto row : kShieldRows) {
			// the old on / off key too, so a version before the pick reads the same choice (the code review of 2026-10-12)
			const int pick = ShieldPick(a_s, ShieldIndex(row));
			a_out << "\n"
				  << ShieldDomeKey(row) << "=" << pick << "\n"
				  << ShieldKey(row) << "=" << (pick != static_cast<int>(ShieldDome::kOff) ? 1 : 0);
		}
		a_out << "\n[Colors]\n";
		for (std::size_t i = 0; i < kRows; ++i) {
			const auto& r = a_s.rows[i];
			a_out << kTokens[i] << "=" << (r.mode == RowMode::kVanilla ? std::string("vanilla") : r.mode == RowMode::kDefault ? std::string("default") :
																																HexColor(r.color))
				  << "\n";
		}
		a_out << "[Others]\n";
		for (std::size_t i = 0; i < kLooks; ++i) {
			a_out << kLookTokens[i] << "=" << LookText(a_s.looks[i]) << "\n";
		}
		a_out << "[Compatibility]\n";
		for (const auto& [plugin, on] : a_s.mods) {
			a_out << plugin << "=" << (on ? 1 : 0) << "\n";
		}
	}

	// what a row wears with these settings; nothing = Vanilla (the game's own art)
	inline std::optional<Color> RowColorOf(const SettingsValues& a_s, std::size_t a_row)
	{
		if (a_row >= kRows) {
			return std::nullopt;
		}
		const auto& r = a_s.rows[a_row];
		switch (r.mode) {
		case RowMode::kVanilla:
			return std::nullopt;
		case RowMode::kCustom:
			return r.color;
		default:
			return a_row >= kRanks ? kRowDefault[a_row] : Mix(kWhite, a_s.ladder, StopPercentOf(a_s.stages, a_s.reversed, a_row));
		}
	}
}
