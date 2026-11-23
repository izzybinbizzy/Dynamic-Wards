// Our plugins' Advanced Settings file - shared word for word (master: Tools\Shared Plugin Code; shared code sync.py keeps the copies)
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file and the notice at the top of main.cpp.
//
// One ini per mod of the fine values its SKSE menu does not show, laid out by the shared AdvancedLayout.h like every mod's (a boxed
// header, the contents, the values most people change, a banner per section; each value's note above it, its default and range beside it). The download ships it at the defaults; a second file beside it ("... - Custom.ini") that no download
// carries keeps a player's own lines through updates, and its lines win - Illuminated's and RELight's way (FadeTuningText.h), after SSE
// Display Tweaks' _custom.ini (its method only, no code of it). A plugin reads both once when the game starts. A value outside its range
// is clamped, one that does not read keeps the value it had, a key that is no setting is named (with the setting it is most likely a
// misspelling of), a key under an older or misspelled section is still read. Nothing here touches the game: the off-game tests run all of it.

#pragma once

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <istream>
#include <map>
#include <ostream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "AdvancedLayout.h"

namespace AdvancedSettings
{
	// one value: where it sits, its note (one short plain sentence) and its range; a whole one is a count, a common one is listed under "most
	// people only change these"
	struct Knob
	{
		std::string_view section;
		std::string_view key;
		std::string_view note;
		float            def{ 0.0f };
		float            lo{ 0.0f };
		float            hi{ 0.0f };
		bool             whole{ false };
		bool             common{ false };
	};

	struct Result
	{
		std::vector<float>       values;             // one per knob, in the table's order
		std::vector<std::string> warnings;           // each value that did not read, was clamped, or is no setting
		std::size_t              read{ 0 };          // the values taken from the file (or files)
		bool                     complete{ false };  // every knob stood in the file and nothing else did
	};

	namespace Detail
	{
		[[nodiscard]] inline std::string_view Trim(std::string_view a_s) noexcept
		{
			while (!a_s.empty() && std::isspace(static_cast<unsigned char>(a_s.front()))) {
				a_s.remove_prefix(1);
			}
			while (!a_s.empty() && std::isspace(static_cast<unsigned char>(a_s.back()))) {
				a_s.remove_suffix(1);
			}
			return a_s;
		}

		[[nodiscard]] inline std::string Lower(std::string_view a_s)
		{
			std::string out(a_s);
			std::ranges::transform(out, out.begin(), [](unsigned char a_c) { return static_cast<char>(std::tolower(a_c)); });
			return out;
		}

		// a number as a warning shows it: the file's own way (AdvancedLayout::Number)
		[[nodiscard]] inline std::string Shown(float a_v, bool a_whole) { return AdvancedLayout::Number(a_v, a_whole); }

		// how many single letters must be added, taken out or changed to turn one word into the other
		[[nodiscard]] inline std::size_t Distance(std::string_view a_a, std::string_view a_b)
		{
			std::vector<std::size_t> row(a_b.size() + 1);
			for (std::size_t j = 0; j < row.size(); ++j) {
				row[j] = j;
			}
			for (std::size_t i = 1; i <= a_a.size(); ++i) {
				std::size_t diagonal = row[0];
				row[0] = i;
				for (std::size_t j = 1; j <= a_b.size(); ++j) {
					const std::size_t above = row[j];
					const std::size_t change = a_a[i - 1] == a_b[j - 1] ? std::size_t{ 0 } : std::size_t{ 1 };
					row[j] = std::min({ above + 1, row[j - 1] + 1, diagonal + change });
					diagonal = above;
				}
			}
			return row.back();
		}

		// a key that is no setting, named; with the setting it is most likely a misspelling of (a few letters off, the nearer section first)
		// when exactly one is nearest
		[[nodiscard]] inline std::string NoSetting(std::string_view a_shownSection, std::string_view a_section, std::string_view a_key,
			std::span<const Knob> a_knobs)
		{
			const auto  key = Lower(a_key);
			std::size_t limit = 1;  // the letters a misspelling may be off by: more for a longer key
			if (key.size() >= 9) {
				limit = 3;
			} else if (key.size() >= 5) {
				limit = 2;
			}
			const Knob* best = nullptr;
			std::pair   bestScore{ limit + 1, std::size_t{ 0 } };
			bool        tie = false;
			for (const auto& k : a_knobs) {
				const std::pair score{ Distance(key, Lower(k.key)), Distance(a_section, Lower(k.section)) };
				if (score.first > limit) {
					continue;
				}
				if (!best || score < bestScore) {
					best = &k;
					bestScore = score;
					tie = false;
				} else if (score == bestScore) {
					tie = true;
				}
			}
			if (!best || tie) {
				return std::format("[{}] {} is no setting; it is left out", a_shownSection, a_key);
			}
			return std::format("[{}] {} is no setting; it is left out - did you mean [{}] {}?", a_shownSection, a_key, best->section, best->key);
		}
	}

	[[nodiscard]] inline std::vector<float> Defaults(std::span<const Knob> a_knobs)
	{
		std::vector<float> out;
		out.reserve(a_knobs.size());
		for (const auto& k : a_knobs) {
			out.push_back(k.def);
		}
		return out;
	}

	// the file's values over a_base (one per knob): sections and keys in any case, a UTF-8 mark, blank lines and notes (; or #, a whole line
	// or after a value) passed over; a key written twice keeps its first value. A key under another section (an older file, a section
	// renamed or misspelled) is read where it belongs when no other section has that key, named, and the file is written again; a key that
	// is no setting is named with the setting it is most likely a misspelling of
	[[nodiscard]] inline Result Read(std::istream& a_in, std::span<const Knob> a_knobs, std::span<const float> a_base)
	{
		Result r{ std::vector<float>(a_base.begin(), a_base.end()), {}, 0, true };
		r.values.resize(a_knobs.size());
		std::map<std::string, std::size_t>              index;
		std::map<std::string, std::vector<std::size_t>> byKey;
		for (std::size_t i = 0; i < a_knobs.size(); ++i) {
			index.emplace(Detail::Lower(a_knobs[i].section) + "|" + Detail::Lower(a_knobs[i].key), i);
			byKey[Detail::Lower(a_knobs[i].key)].push_back(i);
		}
		std::vector<bool> seen(a_knobs.size(), false);
		std::string       line, section, shownSection;
		bool              first = true;
		while (std::getline(a_in, line)) {
			std::string_view l = line;
			if (first && l.starts_with("\xEF\xBB\xBF")) {
				l.remove_prefix(3);
			}
			first = false;
			const auto t = Detail::Trim(l.substr(0, l.find_first_of(";#")));
			if (t.empty()) {
				continue;
			}
			if (t.front() == '[' && t.back() == ']') {
				shownSection = std::string(Detail::Trim(t.substr(1, t.size() - 2)));
				section = Detail::Lower(shownSection);
				continue;
			}
			const auto eq = t.find('=');
			if (eq == std::string_view::npos) {
				r.warnings.push_back(std::format("[{}] \"{}\" is no setting (no =)", shownSection, t));
				r.complete = false;
				continue;
			}
			const auto  key = Detail::Trim(t.substr(0, eq));
			const auto  value = Detail::Trim(t.substr(eq + 1));
			const auto  lowKey = Detail::Lower(key);
			std::size_t i = 0;
			if (const auto it = index.find(section + "|" + lowKey); it != index.end()) {
				i = it->second;
			} else if (const auto moved = byKey.find(lowKey); moved != byKey.end() && moved->second.size() == 1) {
				i = moved->second.front();
				r.warnings.push_back(std::format("[{}] {} is read as [{}] {}", shownSection, key, a_knobs[i].section, a_knobs[i].key));
				r.complete = false;
			} else {
				r.warnings.push_back(Detail::NoSetting(shownSection, section, key, a_knobs));
				r.complete = false;
				continue;
			}
			const auto& k = a_knobs[i];
			if (seen[i]) {
				r.warnings.push_back(std::format("[{}] {} is written twice; the first stays", k.section, k.key));
				continue;
			}
			seen[i] = true;
			float v = 0.0f;
			const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), v);
			if (value.empty() || ec != std::errc{} || end != value.data() + value.size() || !std::isfinite(v)) {
				r.warnings.push_back(std::format("[{}] {}={} is not a number; {} stays", k.section, k.key, value, Detail::Shown(r.values[i], k.whole)));
				continue;
			}
			if (k.whole) {
				v = std::round(v);
			}
			if (v < k.lo || v > k.hi) {
				r.warnings.push_back(std::format("[{}] {}={} is outside {} to {}; clamped", k.section, k.key, value, Detail::Shown(k.lo, k.whole),
					Detail::Shown(k.hi, k.whole)));
				v = std::clamp(v, k.lo, k.hi);
			}
			r.values[i] = v;
			++r.read;
		}
		r.complete = r.complete && std::ranges::all_of(seen, [](bool a_b) { return a_b; });
		return r;
	}

	[[nodiscard]] inline Result Read(std::istream& a_in, std::span<const Knob> a_knobs)
	{
		return Read(a_in, a_knobs, Defaults(a_knobs));
	}

	// the layout's version (AdvancedLayout's title line); the reader needs none: a file that lacks a value is written again whole
	inline constexpr int kLayoutVersion = 1;

	// the whole file through the shared layout: every knob with its value
	[[nodiscard]] inline std::string Text(std::string_view a_title, std::span<const Knob> a_knobs, std::span<const float> a_values)
	{
		std::vector<AdvancedLayout::Row> rows;
		rows.reserve(a_knobs.size());
		for (std::size_t i = 0; i < a_knobs.size(); ++i) {
			const auto& k = a_knobs[i];
			rows.push_back({ std::string(k.section), std::string(k.key), std::string(k.note), k.def, k.lo, k.hi, i < a_values.size() ? a_values[i] : k.def,
				k.whole, k.common });
		}
		return AdvancedLayout::Render(a_title, std::string(a_title) + " - Advanced Settings - Custom.ini", kLayoutVersion, rows);
	}

	// "<folder>\<title> - Advanced Settings.ini" and the player's "... - Custom.ini" beside it
	[[nodiscard]] inline std::filesystem::path MainPath(const std::filesystem::path& a_folder, std::string_view a_title)
	{
		return a_folder / (std::string(a_title) + " - Advanced Settings.ini");
	}

	[[nodiscard]] inline std::filesystem::path CustomPath(const std::filesystem::path& a_folder, std::string_view a_title)
	{
		return a_folder / (std::string(a_title) + " - Advanced Settings - Custom.ini");
	}

	// both files when the game starts: the shipped file, written whole (its values kept) when it is missing or lacks a value - never when it
	// is whole, so a file a player edited stays as it is; then the player's Custom file over it, never written. a_wrote says whether the
	// shipped file was written; a write that fails is a warning, and the values read still hold
	[[nodiscard]] inline Result Load(const std::filesystem::path& a_folder, std::string_view a_title, std::span<const Knob> a_knobs, bool& a_wrote)
	{
		a_wrote = false;
		const auto main = MainPath(a_folder, a_title);
		const auto custom = CustomPath(a_folder, a_title);
		Result     r{ Defaults(a_knobs), {}, 0, false };
		if (std::ifstream in(main); in) {
			r = Read(in, a_knobs);
			for (auto& w : r.warnings) {
				w = main.filename().string() + ": " + w;
			}
		}
		if (!r.complete) {
			std::error_code ec;
			std::filesystem::create_directories(a_folder, ec);
			auto tmp = main;
			tmp += ".tmp";
			bool written = false;
			{
				std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
				out << Text(a_title, a_knobs, r.values);
				written = static_cast<bool>(out.flush());
			}
			if (!written) {
				r.warnings.push_back(std::format("{} could not be written", main.string()));
				std::filesystem::remove(tmp, ec);
			} else if (std::filesystem::rename(tmp, main, ec); ec) {
				r.warnings.push_back(std::format("{} could not be replaced ({})", main.string(), ec.message()));
				std::filesystem::remove(tmp, ec);
			} else {
				a_wrote = true;
			}
		}
		if (std::ifstream in(custom); in) {
			auto mine = Read(in, a_knobs, r.values);
			for (auto& w : mine.warnings) {
				r.warnings.push_back(custom.filename().string() + ": " + w);
			}
			r.values = std::move(mine.values);
			r.read += mine.read;
		}
		return r;
	}
}
