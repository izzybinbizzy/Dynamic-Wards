// Our plugins' Advanced Settings file - its off-game checks (master: Tools\Shared Plugin Code, every copy kept equal by shared code sync.py)
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file.
//
// Run by each carrying plugin's own tests: Run (the reader and writer on a made-up table) adds kCount to the test's floor, Table (the
// plugin's own knobs) kTableCount, Shipped (the file in its package equals the file at the defaults) kShippedCount. With the environment
// variable ADVANCED_DEFAULTS_WRITE set, Shipped writes the package's file first: it is made from the table, never by hand.
#pragma once

#include "AdvancedSettings.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <string_view>

namespace AdvancedSettingsChecks
{
	using Check = void (*)(bool a_ok, const char* a_what, int a_line);

	// the checks each call makes (raise them when checks are added)
	inline constexpr int kCount = 26;
	inline constexpr int kTableCount = 5;
	inline constexpr int kShippedCount = 2;

	namespace Detail
	{
		inline constexpr std::array<AdvancedSettings::Knob, 3> kSample{ {
			{ "Lights", "Count", "How many lights.", 16.0f, 1.0f, 32.0f, true, true },
			{ "Lights", "Swing", "How much it swings.", 1.0f, 0.0f, 2.0f, false, false },
			{ "Fade", "Seconds", "How long it fades.", 4.0f, 0.5f, 20.0f, false, false },
		} };

		[[nodiscard]] inline AdvancedSettings::Result ReadText(const std::string& a_text)
		{
			std::istringstream in(a_text);
			return AdvancedSettings::Read(in, kSample);
		}

		[[nodiscard]] inline std::string FileText(const std::filesystem::path& a_path)
		{
			std::ifstream in(a_path, std::ios::binary);
			return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
		}

		inline void Put(const std::filesystem::path& a_path, std::string_view a_text)
		{
			std::ofstream out(a_path, std::ios::binary | std::ios::trunc);
			out << a_text;
		}

		// whether an environment variable is set: MSVC refuses getenv as unsafe (C4996, an error under /WX), so its own _dupenv_s there
		[[nodiscard]] inline bool EnvSet(const char* a_name)
		{
#if defined(_MSC_VER)
			char*       value = nullptr;
			std::size_t size = 0;
			const bool  set = _dupenv_s(&value, &size, a_name) == 0 && value != nullptr;
			std::free(value);
			return set;
#else
			return std::getenv(a_name) != nullptr;
#endif
		}
	}

#define ADVANCED_CHECK(x) a_check((x), #x, __LINE__)

	inline void Run(Check a_check)
	{
		using Detail::kSample;
		using Detail::ReadText;
		const auto defaults = AdvancedSettings::Defaults(kSample);
		// nothing read: every default, and the file needs writing
		const auto none = ReadText("");
		ADVANCED_CHECK(none.values == defaults);
		ADVANCED_CHECK(!none.complete && none.read == 0 && none.warnings.empty());
		// a UTF-8 mark, notes, other cases, a note after a value: read; a whole value rounded
		const auto good = ReadText("\xEF\xBB\xBF; a note\n[lights]\n  COUNT = 7.6 ; seven\nswing=0.5\n\n# another\n[FADE]\nseconds=2.25\n");
		ADVANCED_CHECK(good.complete && good.read == 3 && good.warnings.empty());
		ADVANCED_CHECK(good.values[0] == 8.0f && good.values[1] == 0.5f && good.values[2] == 2.25f);
		// out of range: clamped and named; not a number: the value it had and named; the same key twice: the first
		const auto bad = ReadText("[Lights]\nCount=99\nSwing=fast\nSwing=1.5\n[Fade]\nSeconds=-3\n");
		ADVANCED_CHECK(bad.values[0] == 32.0f && bad.values[1] == 1.0f && bad.values[2] == 0.5f);
		ADVANCED_CHECK(bad.warnings.size() == 4 && bad.read == 2);
		// a key in the wrong section (read where it belongs, the same key again later is written twice), and a line with no =: named, and
		// the file is written again
		const auto odd = ReadText("[Fade]\nCount=3\n[Lights]\nCount=5\nSwing=1\nWhat\n[Fade]\nSeconds=4\n");
		ADVANCED_CHECK(!odd.complete && odd.warnings.size() == 3 && odd.values[0] == 3.0f);
		// an older file's section (renamed or misspelled): its keys still read, each named, and the file written again in today's sections
		const auto moved = ReadText("[Old Lights]\nCount=5\nSwing=0.5\n[Fade]\nSeconds=4\n");
		ADVANCED_CHECK(!moved.complete && moved.read == 3 && moved.values[0] == 5.0f && moved.values[1] == 0.5f && moved.warnings.size() == 2);
		ADVANCED_CHECK(moved.warnings[0] == "[Old Lights] Count is read as [Lights] Count");
		// a misspelled key: named with the setting it most likely is; one too far off from any: named alone; nothing of theirs read
		const auto spelt = ReadText("[Lights]\nCuont=5\nswign=0\n[Fade]\nSecs=2\nZebra=1\n");
		ADVANCED_CHECK(!spelt.complete && spelt.read == 0 && spelt.values == defaults && spelt.warnings.size() == 4);
		ADVANCED_CHECK(spelt.warnings[0] == "[Lights] Cuont is no setting; it is left out - did you mean [Lights] Count?" &&
					   spelt.warnings[1] == "[Lights] swign is no setting; it is left out - did you mean [Lights] Swing?" &&
					   spelt.warnings[2] == "[Fade] Secs is no setting; it is left out" && spelt.warnings[3] == "[Fade] Zebra is no setting; it is left out");
		// a comma, a unit, nan and inf are not numbers
		for (const char* text : { "[Fade]\nSeconds=0,5\n", "[Fade]\nSeconds=3s\n", "[Fade]\nSeconds=nan\n", "[Fade]\nSeconds=inf\n" }) {
			const auto r = ReadText(text);
			ADVANCED_CHECK(r.values[2] == 4.0f && r.warnings.size() == 1);
		}
		// a missing value: the rest read, the file written again
		const auto part = ReadText("[Lights]\nCount=2\nSwing=0\n");
		ADVANCED_CHECK(!part.complete && part.read == 2 && part.values[2] == 4.0f);
		// the written file reads back to the same values, complete, with each note, range and default above its value
		const std::array<float, 3> values{ 9.0f, 0.25f, 12.5f };
		const auto                 text = AdvancedSettings::Text("A Mod", kSample, values);
		const auto                 back = ReadText(text);
		ADVANCED_CHECK(back.complete && back.warnings.empty() && back.values == std::vector<float>(values.begin(), values.end()));
		ADVANCED_CHECK(text.find("; How many lights.\nCount = 9     ; default 16 - range 1 to 32\n") != std::string::npos);
		ADVANCED_CHECK(text.find("; How much it swings.\nSwing = 0.25  ; default 1 - range 0 to 2\n") != std::string::npos);
		ADVANCED_CHECK(text.find("A Mod - Advanced Settings (file version 1)") != std::string::npos &&
					   text.find("A Mod - Advanced Settings - Custom.ini") != std::string::npos && text.find("MOST PEOPLE ONLY CHANGE THESE") != std::string::npos);
		ADVANCED_CHECK(text.find("\n[Lights]\n") != std::string::npos && text.find("\n[Fade]\n") != std::string::npos);
		// both files: the shipped one missing is written whole at the defaults; a whole one is read and left alone (a player's edit stays);
		// one that lacks a value is written again with its values kept; the Custom file's lines win and it is never written
		const auto      dir = std::filesystem::temp_directory_path() / ("advanced settings check " + std::to_string(std::rand()));
		const auto      main = AdvancedSettings::MainPath(dir, "A Mod");
		const auto      custom = AdvancedSettings::CustomPath(dir, "A Mod");
		std::error_code ec;
		std::filesystem::remove_all(dir, ec);
		bool       wrote = false;
		const auto first = AdvancedSettings::Load(dir, "A Mod", kSample, wrote);
		ADVANCED_CHECK(wrote && first.values == defaults && Detail::FileText(main) == AdvancedSettings::Text("A Mod", kSample, defaults));
		const auto edited = AdvancedSettings::Text("A Mod", kSample, values) + "; my own note\n";
		Detail::Put(main, edited);
		const auto again = AdvancedSettings::Load(dir, "A Mod", kSample, wrote);
		ADVANCED_CHECK(!wrote && again.values[2] == 12.5f && Detail::FileText(main) == edited);
		Detail::Put(main, "[Lights]\nCount=3\n");
		Detail::Put(custom, "[Fade]\nSeconds=6\n[Lights]\nNope=1\n");
		const auto mixed = AdvancedSettings::Load(dir, "A Mod", kSample, wrote);
		const auto mainBack = ReadText(Detail::FileText(main));
		ADVANCED_CHECK(wrote && mainBack.complete && mainBack.values[0] == 3.0f && mainBack.values[2] == 4.0f);
		ADVANCED_CHECK(mixed.values[0] == 3.0f && mixed.values[2] == 6.0f && mixed.warnings.size() == 1 &&
					   Detail::FileText(custom) == "[Fade]\nSeconds=6\n[Lights]\nNope=1\n");
		std::filesystem::remove_all(dir, ec);
		ADVANCED_CHECK(!ec);
	}

	// a plugin's own table: every default inside its range, a whole default a whole number, the keys unique, each note one short sentence
	template <std::size_t N>
	inline void Table(Check a_check, const std::array<AdvancedSettings::Knob, N>& a_knobs)
	{
		bool inside = true, whole = true, notes = true;
		for (const auto& k : a_knobs) {
			inside = inside && k.lo <= k.def && k.def <= k.hi && k.lo < k.hi;
			whole = whole && (!k.whole || (k.def == std::round(k.def) && k.lo == std::round(k.lo) && k.hi == std::round(k.hi)));
			notes = notes && !k.note.empty() && k.note.size() <= 140 && k.note.ends_with('.') && k.note.find('\n') == std::string_view::npos &&
			        k.note.find(';') == std::string_view::npos && k.note.find('#') == std::string_view::npos;
		}
		std::set<std::string> keys;
		for (const auto& k : a_knobs) {
			keys.insert(AdvancedSettings::Detail::Lower(k.section) + "|" + AdvancedSettings::Detail::Lower(k.key));
		}
		ADVANCED_CHECK(inside);
		ADVANCED_CHECK(whole);
		ADVANCED_CHECK(notes);
		ADVANCED_CHECK(keys.size() == N);
		const auto         text = AdvancedSettings::Text("t", a_knobs, AdvancedSettings::Defaults(a_knobs));
		std::istringstream in(text);
		const auto         r = AdvancedSettings::Read(in, a_knobs);
		ADVANCED_CHECK(r.complete && r.warnings.empty() && r.values == AdvancedSettings::Defaults(a_knobs));
	}

	// the file the download carries equals the file at the defaults (written first with ADVANCED_DEFAULTS_WRITE set). The tests run from the
	// plugin's own folder (plugin build.py, clang check.py), so a_folder is the package's folder from there
	template <std::size_t N>
	inline void Shipped(Check a_check, const std::filesystem::path& a_folder, std::string_view a_title, const std::array<AdvancedSettings::Knob, N>& a_knobs)
	{
		const auto path = AdvancedSettings::MainPath(a_folder, a_title);
		const auto want = AdvancedSettings::Text(a_title, a_knobs, AdvancedSettings::Defaults(a_knobs));
		if (Detail::EnvSet("ADVANCED_DEFAULTS_WRITE")) {
			std::error_code ec;
			std::filesystem::create_directories(a_folder, ec);
			Detail::Put(path, want);
		}
		ADVANCED_CHECK(std::filesystem::exists(path));
		ADVANCED_CHECK(Detail::FileText(path) == want);
	}

#undef ADVANCED_CHECK
}
