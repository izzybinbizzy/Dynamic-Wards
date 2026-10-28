// Dynamic Wards - tests for src/WardMath.h and src/SettingsText.h (no game needed).
// Copyright (C) 2026 izzydoingit. GPL-3.0-or-later.
//
// PC Runner\plugin build.py dw builds and runs them (xmake target testwards), then again under clang's address and
// undefined-behavior checkers. The run fails when a check fails, AND when fewer checks ran than kFloor (a test that stopped
// part way proves nothing - Illuminated's fade tests count theirs the same way).

#include "SettingsText.h"
#include "WardMath.h"

#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace
{
	int gFailed = 0, gPassed = 0;

	void Check(bool a_ok, const char* a_what, int a_line)
	{
		if (a_ok) {
			++gPassed;
		} else {
			++gFailed;
			if (gFailed <= 30) {
				std::printf("FAIL line %d: %s\n", a_line, a_what);
			}
		}
	}
#define CHECK(x) Check((x), #x, __LINE__)

	// the fewest checks a whole run makes (raise it when checks are added; lower it only when checks are removed on purpose)
	constexpr int kFloor = 251 + 10;

	using namespace Plugin;

	// ---------------------------------------------------------------- text
	void Text()
	{
		CHECK(Lower("AbC.ESP") == "abc.esp");
		CHECK(SameText("Settings", "sETTINGS"));
		CHECK(!SameText("Settings", "Setting"));
		CHECK(Contains("magic\\dynamic wards\\x", "dynamic wards"));
		CHECK(!Contains("abc", ""));
		CHECK(Trim("  a b \t\r\n") == "a b");
		CHECK(Trim(" \t ").empty());
		CHECK(HexColor(0x2468FF) == "2468FF");
		CHECK(HexColor(0x12345678) == "345678");  // only the color's three bytes
		CHECK(ParseColor("2468FF") == Color{ 0x2468FF });
		CHECK(ParseColor("#2468ff") == Color{ 0x2468FF });
		CHECK(ParseColor("0x2468FF") == Color{ 0x2468FF });
		CHECK(ParseColor(" #FFFFFF") == kWhite);
		CHECK(!ParseColor("2468F"));
		CHECK(!ParseColor("2468FFF"));
		CHECK(!ParseColor("2468FG"));
		CHECK(!ParseColor(""));
		CHECK(!ParseColor("-468FF"));
		CHECK(RowOfToken("Adept") == 2);
		CHECK(RowOfToken("vampire") == 8);
		CHECK(RowOfToken("Nope") == kRows);
		CHECK(RowOfToken("") == kRows);
	}

	// ---------------------------------------------------------------- the ladder
	struct Stop
	{
		Color ladder;
		int   percent;
		Color expect;
	};
	// wardgen's ladder_stop, worked out by Python (int(round(255 + (c - 255) * k / 100)) per channel) - the oracle the DLL's
	// Mix must match (the outside review's 1-unit rounding: 3.4 read C9DAFF for the first row)
	constexpr Stop kStops[] = {
		{ 0x2468FF, 25, 0xC8D9FF },
		{ 0x2468FF, 33, 0xB7CDFF },
		{ 0x2468FF, 50, 0x92B4FF },
		{ 0x2468FF, 55, 0x87ACFF },
		{ 0x2468FF, 67, 0x6C9AFF },
		{ 0x2468FF, 75, 0x5B8EFF },
		{ 0xD0102E, 25, 0xF3C3CB },
		{ 0xD0102E, 33, 0xEFB0BA },
		{ 0xD0102E, 50, 0xE88896 },
		{ 0xD0102E, 55, 0xE57C8C },
		{ 0xD0102E, 67, 0xE05F73 },
		{ 0xD0102E, 75, 0xDC4C62 },
		{ 0xFF6A10, 25, 0xFFDAC3 },
		{ 0xFF6A10, 33, 0xFFCEB0 },
		{ 0xFF6A10, 50, 0xFFB488 },
		{ 0xFF6A10, 55, 0xFFAD7C },
		{ 0xFF6A10, 67, 0xFF9B5F },
		{ 0xFF6A10, 75, 0xFF8F4C },
		{ 0xFFC420, 25, 0xFFF0C7 },
		{ 0xFFC420, 33, 0xFFECB5 },
		{ 0xFFC420, 50, 0xFFE290 },
		{ 0xFFC420, 55, 0xFFDF84 },
		{ 0xFFC420, 67, 0xFFD76A },
		{ 0xFFC420, 75, 0xFFD358 },
		{ 0x2ED452, 25, 0xCBF4D4 },
		{ 0x2ED452, 33, 0xBAF1C6 },
		{ 0x2ED452, 50, 0x96EAA8 },
		{ 0x2ED452, 55, 0x8CE7A0 },
		{ 0x2ED452, 67, 0x73E28B },
		{ 0x2ED452, 75, 0x62DF7D },
		{ 0x00D2B0, 25, 0xBFF4EB },
		{ 0x00D2B0, 33, 0xABF0E5 },
		{ 0x00D2B0, 50, 0x80E8D8 },
		{ 0x00D2B0, 55, 0x73E6D4 },
		{ 0x00D2B0, 67, 0x54E1CA },
		{ 0x00D2B0, 75, 0x40DDC4 },
		{ 0x40DCFF, 25, 0xCFF6FF },
		{ 0x40DCFF, 33, 0xC0F3FF },
		{ 0x40DCFF, 50, 0xA0EEFF },
		{ 0x40DCFF, 55, 0x96ECFF },
		{ 0x40DCFF, 67, 0x7FE8FF },
		{ 0x40DCFF, 75, 0x70E5FF },
		{ 0x7A3CFF, 25, 0xDECEFF },
		{ 0x7A3CFF, 33, 0xD3BFFF },
		{ 0x7A3CFF, 50, 0xBC9EFF },
		{ 0x7A3CFF, 55, 0xB694FF },
		{ 0x7A3CFF, 67, 0xA67CFF },
		{ 0x7A3CFF, 75, 0x9B6DFF },
		{ 0xFF2EC4, 25, 0xFFCBF0 },
		{ 0xFF2EC4, 33, 0xFFBAEC },
		{ 0xFF2EC4, 50, 0xFF96E2 },
		{ 0xFF2EC4, 55, 0xFF8CDF },
		{ 0xFF2EC4, 67, 0xFF73D7 },
		{ 0xFF2EC4, 75, 0xFF62D3 },
		{ 0x9646FF, 25, 0xE5D1FF },
		{ 0x9646FF, 33, 0xDCC2FF },
		{ 0x9646FF, 50, 0xCAA2FF },
		{ 0x9646FF, 55, 0xC599FF },
		{ 0x9646FF, 67, 0xB983FF },
		{ 0x9646FF, 75, 0xB074FF },
	};

	void Ladder()
	{
		int wrong = 0;
		for (const auto& s : kStops) {
			const Color got = Mix(kWhite, s.ladder, s.percent);
			if (got != s.expect) {
				++wrong;
				std::printf("  Mix(white, %06X, %d) = %06X, wardgen %06X\n", s.ladder, s.percent, got, s.expect);
			}
			CHECK(got == s.expect);
		}
		CHECK(wrong == 0);
		CHECK(Mix(kWhite, 0x2468FF, 0) == kWhite);
		CHECK(Mix(kWhite, 0x2468FF, 100) == Color{ 0x2468FF });
		CHECK(Mix(kWhite, 0x2468FF, -10) == kWhite);  // clamped
		CHECK(Mix(kWhite, 0x2468FF, 400) == Color{ 0x2468FF });
		CHECK(Mix(0x000000, 0x010101, 50) == Color{ 0x000000 });  // 0.5 -> 0, half to even
		CHECK(Mix(0x000000, 0x030303, 50) == Color{ 0x020202 });  // 1.5 -> 2
		CHECK(Mix(0x000000, 0xFFFFFF, 50) == Color{ 0x808080 });  // 127.5 -> 128
		CHECK(Mix(0xFFFFFF, 0x000000, 50) == Color{ 0x808080 });  // going down rounds the same way as going up
		bool symmetric = true;
		for (int k = 0; k <= 100; ++k) {
			symmetric = symmetric && Mix(0x000000, 0xFFFFFF, k) == Mix(0xFFFFFF, 0x000000, 100 - k);
		}
		CHECK(symmetric);

		CHECK(StopPercentOf(3, false, 0) == 0);
		CHECK(StopPercentOf(3, false, 1) == 55);
		CHECK(StopPercentOf(3, false, 2) == 100);
		CHECK(StopPercentOf(3, false, 4) == 100);  // ranks past the ladder hold its last stop
		CHECK(StopPercentOf(3, true, 0) == 100);
		CHECK(StopPercentOf(3, true, 1) == 55);
		CHECK(StopPercentOf(3, true, 2) == 0);
		CHECK(StopPercentOf(3, true, 4) == 0);
		CHECK(StopPercentOf(4, false, 1) == 33);
		CHECK(StopPercentOf(4, false, 2) == 67);
		CHECK(StopPercentOf(4, true, 3) == 0);
		CHECK(StopPercentOf(5, false, 2) == 50);
		CHECK(StopPercentOf(5, false, 4) == 100);
		CHECK(StopPercentOf(5, true, 4) == 0);
		CHECK(StopPercentOf(9, false, 3) == 75);  // out of range stages read as 5
	}

	// ---------------------------------------------------------------- a ward's rank
	void Ranks()
	{
		CHECK(RankRow(0) == 0);
		CHECK(RankRow(24) == 0);
		CHECK(RankRow(25) == 1);
		CHECK(RankRow(50) == 2);
		CHECK(RankRow(74) == 2);
		CHECK(RankRow(75) == 3);
		CHECK(RankRow(100) == 4);
		CHECK(RankRow(-5) == 0);
		CHECK(PowerRow(0.0f) == 0);
		CHECK(PowerRow(40.0f) == 0);   // Lesser Ward
		CHECK(PowerRow(60.0f) == 1);   // Steadfast Ward
		CHECK(PowerRow(80.0f) == 2);   // Greater Ward
		CHECK(PowerRow(90.0f) == 3);   // past Greater: Expert
		CHECK(PowerRow(100.0f) == 4);  // Master
		CHECK(PowerRow(999.0f) == 4);  // Reliquary of Myth's Magnus ward
		CHECK(UnlockSkill(Unlock::kAlways) == 0.0f);
		CHECK(UnlockSkill(Unlock::kSkill50) == 50.0f);
		CHECK(UnlockSkill(Unlock::kSkill75) == 75.0f);
		CHECK(UnlockSkill(Unlock::kSkill100) == 100.0f);
		CHECK(!UnlockSkill(Unlock::kPerk));
	}

	// ---------------------------------------------------------------- the palettes
	std::vector<std::uint8_t> Dds(std::uint32_t a_w, std::uint32_t a_h, std::uint32_t a_bits, const std::vector<std::uint8_t>& a_bgra)
	{
		std::vector<std::uint8_t> raw(128, 0);
		std::memcpy(raw.data(), "DDS ", 4);
		std::memcpy(raw.data() + 12, &a_h, 4);
		std::memcpy(raw.data() + 16, &a_w, 4);
		std::memcpy(raw.data() + 88, &a_bits, 4);
		raw.insert(raw.end(), a_bgra.begin(), a_bgra.end());
		return raw;
	}

	void Palettes()
	{
		CHECK(GoldHue(0xFFC420));   // Aedric
		CHECK(GoldHue(0xFF6A10));   // Ember
		CHECK(GoldHue(0xFFBE5A));   // the 2.x gold
		CHECK(!GoldHue(0x2468FF));  // blue
		CHECK(!GoldHue(0xFF0000));  // red: hue 0
		CHECK(!GoldHue(kWhite));
		CHECK(!GoldHue(0x000000));
		CHECK(!GoldHue(0xFF2EC4));  // magenta
		CHECK(HoldsHue(0x2468FF));
		CHECK(HoldsHue(0x2ED452));
		CHECK(!HoldsHue(0xFF3648));
		CHECK(!HoldsHue(0xF8FFF8));  // nearly white: no hue to hold

		std::string why;
		CHECK(!PaletteFromDds({}, why));
		CHECK(!why.empty());
		std::vector<std::uint8_t> notDds(200, 0);
		CHECK(!PaletteFromDds(notDds, why));
		CHECK(!PaletteFromDds(Dds(0, 4, 32, {}), why));
		CHECK(!PaletteFromDds(Dds(20000, 1, 32, {}), why));
		CHECK(!PaletteFromDds(Dds(2048, 1024, 32, {}), why));  // too many pixels for a ramp
		CHECK(why.find("too large") != std::string::npos);
		CHECK(!PaletteFromDds(Dds(2, 1, 24, std::vector<std::uint8_t>(8, 0)), why));
		CHECK(!PaletteFromDds(Dds(2, 2, 32, std::vector<std::uint8_t>(8, 0)), why));  // shorter than its size says
		// a 4x1 grey ramp: black, dark, mid, white
		const std::vector<std::uint8_t> grey{ 0, 0, 0, 255, 64, 64, 64, 200, 128, 128, 128, 100, 255, 255, 255, 50 };
		const auto                      img = PaletteFromDds(Dds(4, 1, 32, grey), why);
		CHECK(img.has_value());
		if (!img) {
			return;
		}
		CHECK(img->w == 4 && img->h == 1);
		CHECK(img->bgra.size() == 16);
		CHECK(img->sref == 0.0f);  // a grey ramp has no color of its own: tinted flat
		// flat tint toward blue: each texel's value x the color over its brightest channel
		const auto blue = Retint(*img, 0x2468FF, 0.0f);
		CHECK(blue.size() == 16);
		CHECK(blue[0] == 0 && blue[1] == 0 && blue[2] == 0);                           // black stays black
		CHECK(blue[3] == 255 && blue[7] == 200 && blue[11] == 100 && blue[15] == 50);  // alpha is the source's
		CHECK(blue[12] == 255);                                                        // white's blue channel: 255
		CHECK(blue[13] == 104);                                                        // its green: 0x68
		CHECK(blue[14] == 36);                                                         // its red: 0x24
		CHECK(blue[9] == 52 && blue[10] == 18);                                        // mid grey 128: 128 x 104 / 255 = 52.2, 128 x 36 / 255 = 18.1
		// a ramp with color of its own: its saturation is kept; white stays white unless a core floor pulls it
		const std::vector<std::uint8_t> warm{ 40, 120, 255, 255, 255, 255, 255, 255 };  // an orange texel (BGRA) and a white one
		const auto                      w = PaletteFromDds(Dds(2, 1, 32, warm), why);
		CHECK(w.has_value() && w->sref >= kSatFloor);
		if (w) {
			const auto red = Retint(*w, 0xFF3648, 0.0f);  // red holds no hue: white stays white
			CHECK(red[4] == 255 && red[5] == 255 && red[6] == 255);
			const auto core = Retint(*w, 0xFF3648, 1.0f);  // a full core: white takes the whole color
			CHECK(core[6] == 255 && core[5] == 0x36 && core[4] == 0x48);
			const auto green = Retint(*w, 0x2ED452, 0.0f);  // green holds half its hue on white (kHueHold)
			CHECK(green[5] == 255 && green[6] < 255 && green[6] > 0x2E);
		}
	}

	// ---------------------------------------------------------------- DynamicWards.ini
	SettingsValues Read(const std::string& a_text, SettingsRead* a_result = nullptr)
	{
		SettingsValues     s;
		std::istringstream in(a_text);
		const auto         r = ReadSettingsText(in, s);
		if (a_result) {
			*a_result = r;
		}
		return s;
	}

	void Settings()
	{
		// no file: his defaults; the shields wear the dome the spell wards wear (the 360 dome by default)
		const auto none = Read("");
		CHECK(none.opacity == 100 && none.transparency == 0 && none.brightness == 100 && none.castingGlow == 100);
		CHECK(none.domeBoost == 0);  // off unless the player turns it up
		CHECK(none.looks == DefaultLooks() && none.looks[kEnemies].mode == LookMode::kMine && none.looks[kGroups].mode == LookMode::kDefault);
		const auto others = Read("[Others]\nEnemies=FF0000\nthalmor=Vanilla\nNeutral=default\nBandits=default\nNope=FF00FF\n");
		CHECK((others.looks[kEnemies] == LookPick{ LookMode::kCustom, 0xFF0000 }));
		CHECK(others.looks[LookOfToken("Thalmor")].mode == LookMode::kVanilla);
		CHECK(others.looks[kNeutral].mode == LookMode::kMine);  // Default is a faction's only: a group keeps its pick
		CHECK(others.looks[LookOfToken("Bandits")].mode == LookMode::kDefault && LookOfToken("Nope") == kLooks);
		CHECK(LookFor(true, true, LookOfToken("Thalmor")) == kFollowers);  // a follower always wears the Followers look
		CHECK(LookFor(false, true, LookOfToken("Thalmor")) == LookOfToken("Thalmor") && LookFor(false, true, kLooks) == kEnemies);
		CHECK(LookFor(false, false, kLooks) == kNeutral && LookFor(false, false, kEnemies) == kNeutral);
		CHECK(SlotToken(kRows) == "Followers" && SlotToken(kSlots - 1) == "HostileMages" && SlotToken(kSlots).empty() && SlotToken(0) == "Novice");
		CHECK(Read("[Settings]\nDomeColorBoost=500\n").domeBoost == 100);
		CHECK(Read("[Settings]\nDomeColorBoost=-5\n").domeBoost == 0);
		CHECK(none.ladder == Color{ 0x2468FF } && none.stages == 3 && !none.reversed);
		CHECK(none.dome == 0 && none.unlock == Unlock::kSkill75 && none.light && none.coloredLights && none.every);
		CHECK(none.shieldDome[0] == 2 && none.shieldDome[1] == 2 && none.shieldDome[2] == 2);
		CHECK(Read("[Settings]\nDome=1\n").shieldDome[0] == 1);  // the normal dome: the shields follow it

		// any case, sections and keys; clamped to the sliders; a bad number keeps the value
		CHECK(Read("[settings]\nopacity=50\n").opacity == 50);
		CHECK(Read("[SETTINGS]\nOPACITY = 60 \n").opacity == 60);
		CHECK(Read("[Settings]\nOpacity=5\n").opacity == kOpacityMin);
		CHECK(Read("[Settings]\nOpacity=abc\n").opacity == 100);
		CHECK(Read("[Settings]\nOpacity=70x\n").opacity == 100);
		CHECK(Read("[Settings]\nTransparency=200\n").transparency == kTransparencyMax);
		CHECK(Read("[Settings]\nBrightness=1000\nCastingGlow=1\n").brightness == kBrightnessMax);
		CHECK(Read("[Settings]\nCastingGlow=1\n").castingGlow == kBrightnessMin);
		CHECK(Read("[Settings]\nLadderStages=9\n").stages == 5);
		CHECK(Read("[Settings]\nUnlock360=9\n").unlock == Unlock::kPerk);
		CHECK(Read("[Settings]\nLadderColor=zz\n").ladder == Color{ 0x2468FF });
		CHECK(Read("[Settings]\nLadderColor=#7a3cff\n").ladder == Color{ 0x7A3CFF });
		CHECK(Read("[Settings]\nUnlock360Perk=0x58F7A~Skyrim.esm\n").perk == "0x58F7A~Skyrim.esm");
		CHECK(Read("; a comment\n# another\n[Settings]\nWardLight=0\n").light == false);
		CHECK(Read("Opacity=50\n").opacity == 100);                 // a key outside its section is not read as a setting
		CHECK(Read("[Settings]\r\nOpacity=40\r\n").opacity == 40);  // a file saved with Windows line ends

		// the shields: the dome pick, else the old on / off key, else the Dome pick
		const auto sh = Read("[Settings]\nDome=1\nSpellbreakerWard=0\nCrusaderWard=1\nRemanDome=2\n");
		CHECK(sh.shieldDome[0] == 0);                                        // off stays off
		CHECK(sh.shieldDome[1] == 1);                                        // on = the normal dome the spell wards wear
		CHECK(sh.shieldDome[2] == 2);                                        // its own pick
		CHECK(Read("[Settings]\nSpellbreakerDome=7\n").shieldDome[0] == 2);  // clamped

		// a 2.x file is carried over
		SettingsRead r;
		const auto   old = Read("[Settings]\nLadder=4\n[Colors]\nAdept=3\nNovice=6\nMaster=0\n", &r);
		CHECK(r.old);
		CHECK(r.lines == 4);
		CHECK(old.ladder == Color{ 0x9646FF });
		CHECK(old.rows[2].mode == RowMode::kCustom && old.rows[2].color == Color{ 0xFFBE5A });
		CHECK(old.rows[0].mode == RowMode::kVanilla);
		CHECK(old.rows[4].mode == RowMode::kDefault);
		SettingsRead fresh;
		Read("[Colors]\nAdept=2468FF\n", &fresh);
		CHECK(!fresh.old);

		// the colors and the compatibility ticks
		const auto c = Read("[Colors]\nadept=vanilla\nSpellbreaker=FF0000\nCrusader=default\nNope=123456\n[Compatibility]\nMy Mod.ESP=0\nOther.esp=1\nBad.esp=x\n");
		CHECK(c.rows[2].mode == RowMode::kVanilla);
		CHECK(c.rows[5].mode == RowMode::kCustom && c.rows[5].color == Color{ 0xFF0000 });
		CHECK(c.rows[6].mode == RowMode::kDefault);
		CHECK(c.mods.size() == 3);
		CHECK(c.mods.contains("bad.esp") && !c.mods.at("bad.esp"));  // a word reads as off, as before 3.5
		CHECK(c.mods.contains("my mod.esp") && !c.mods.at("my mod.esp"));
		CHECK(c.mods.contains("other.esp") && c.mods.at("other.esp"));

		// written and read back: the same table
		SettingsValues w;
		w.ladder = 0x00D2B0;
		w.stages = 4;
		w.reversed = true;
		w.opacity = 80;
		w.transparency = 30;
		w.brightness = 150;
		w.castingGlow = 40;
		w.domeBoost = 35;
		w.looks[kFollowers] = { LookMode::kCustom, 0x2DC846 };
		w.looks[kEnemies] = { LookMode::kVanilla, 0xFFFFFF };
		w.looks[kGroups] = { LookMode::kCustom, 0x9646FF };
		w.dome = 1;
		w.light = false;
		w.coloredLights = false;
		w.unlock = Unlock::kPerk;
		w.perk = "0x1~X.esp";
		w.crusader = false;
		w.every = false;
		w.keepRunes = true;
		w.mods["a.esp"] = true;
		w.mods["b.esp"] = false;
		w.shieldDome = { 0, 2, 1 };
		w.rows[0] = { RowMode::kCustom, 0x123456 };
		w.rows[3] = { RowMode::kVanilla, kWhite };
		std::ostringstream out;
		WriteSettingsText(w, out);
		const auto text = out.str();
		CHECK(text.find("SpellbreakerWard=0") != std::string::npos);  // the old key too, for a version before the pick
		CHECK(text.find("CrusaderWard=1") != std::string::npos);
		CHECK(text.find("Novice=123456") != std::string::npos);
		CHECK(text.find("Expert=vanilla") != std::string::npos);
		CHECK(text.find("Adept=default") != std::string::npos);
		SettingsRead rr;
		const auto   back = Read(text, &rr);
		CHECK(!rr.old);
		CHECK(back.ladder == w.ladder && back.stages == w.stages && back.reversed == w.reversed);
		CHECK(back.opacity == w.opacity && back.transparency == w.transparency && back.brightness == w.brightness);
		CHECK(back.domeBoost == w.domeBoost && back.looks == w.looks);
		CHECK(back.castingGlow == w.castingGlow && back.dome == w.dome && back.light == w.light && back.coloredLights == w.coloredLights);
		CHECK(back.unlock == w.unlock && back.perk == w.perk && back.crusader == w.crusader && back.every == w.every);
		CHECK(back.keepRunes == w.keepRunes && back.mods == w.mods && back.shieldDome == w.shieldDome);
		bool rows = true;
		for (std::size_t i = 0; i < kRows; ++i) {
			rows = rows && back.rows[i].mode == w.rows[i].mode && (w.rows[i].mode != RowMode::kCustom || back.rows[i].color == w.rows[i].color);
		}
		CHECK(rows);
		// the defaults, written and read back, are still the defaults
		std::ostringstream d;
		WriteSettingsText(SettingsValues{}, d);
		const auto dflt = Read(d.str());
		CHECK(dflt.opacity == 100 && dflt.transparency == 0 && dflt.shieldDome[0] == 2 && dflt.mods.empty());

		// what a row wears
		SettingsValues s;
		CHECK(RowColorOf(s, 0) == kWhite);                     // Lesser: the white end of the ladder
		CHECK(RowColorOf(s, 1) == Mix(kWhite, 0x2468FF, 55));  // Steadfast: 55% (3 stages)
		CHECK(RowColorOf(s, 2) == Color{ 0x2468FF });          // Greater: the color
		CHECK(RowColorOf(s, 4) == Color{ 0x2468FF });          // Master holds the last stop
		CHECK(RowColorOf(s, 5) == Color{ 0xFFBE5A });          // Spellbreaker: its own default
		CHECK(RowColorOf(s, 8) == Color{ 0xFF3648 });          // the vampire ward
		CHECK(!RowColorOf(s, kRows));
		s.reversed = true;
		CHECK(RowColorOf(s, 0) == Color{ 0x2468FF });
		CHECK(RowColorOf(s, 2) == kWhite);
		s.rows[2] = { RowMode::kVanilla, kWhite };
		CHECK(!RowColorOf(s, 2));
		s.rows[2] = { RowMode::kCustom, 0xABCDEF };
		CHECK(RowColorOf(s, 2) == Color{ 0xABCDEF });
		s.stages = 5;
		s.reversed = false;
		CHECK(RowColorOf(s, 3) == Mix(kWhite, 0x2468FF, 75));
		CHECK(SettingsText::ShieldPick(s, 9) == 2);  // no such shield: the Dome pick's
	}
}

namespace
{
	bool Near(float a_a, float a_b, float a_tol = 0.002f)
	{
		return std::fabs(a_a - a_b) <= a_tol;
	}

	bool NearRgb(const std::array<float, 3>& a_a, float a_r, float a_g, float a_b)
	{
		return Near(a_a[0], a_r) && Near(a_a[1], a_g) && Near(a_a[2], a_b);
	}

	// ---------------------------------------------------------------- the lights
	void Lights()
	{
		// the build's cutoff (wardgen's K): the dome light's 133 reach at fade 1, size 2
		CHECK(Near(CutoffFor(1.0f, 133.0f, 2.0f), 3918.88f / (133.0f * 133.0f + 4.0f), 0.0001f));
		CHECK(CutoffFor(100.0f, 1.0f, 0.0f) == kHighestCutoff);     // clamped high
		CHECK(CutoffFor(0.0001f, 5000.0f, 2.0f) == kLowestCutoff);  // clamped low
		CHECK(CutoffFor(1.0f, 0.0f, 0.0f) == kHighestCutoff);       // no reach at all: never a division by zero
		// the cutoff follows the dimmed fade: half the fade, half the cutoff (inside the clamp)
		CHECK(Near(CutoffFor(0.5f, 133.0f, 2.0f) * 2.0f, CutoffFor(1.0f, 133.0f, 2.0f), 0.0001f));
		// sRGB -> linear: the ends hold, the knee and a mid value (0.5 -> 0.2140)
		CHECK(ToLinear(0.0f) == 0.0f);
		CHECK(Near(ToLinear(1.0f), 1.0f));
		CHECK(Near(ToLinear(0.04045f), 0.04045f / 12.92f, 0.00001f));
		CHECK(Near(ToLinear(0.5f), 0.21404f, 0.0001f));
		CHECK(NearRgb(LightRgb(0xFF8000, false), 1.0f, 128.0f / 255.0f, 0.0f));
		CHECK(NearRgb(LightRgb(0xFF8000, true), 1.0f, ToLinear(128.0f / 255.0f), 0.0f));
		CHECK(NearRgb(LightRgb(0x000000, true), 0.0f, 0.0f, 0.0f));
		// the ENB sprite: a saturated hue keeps its hue at full value; a pale one is pushed to the floor; white stays white
		CHECK(NearRgb(SpriteRgb(0xFF0000, 0.75f), 1.0f, 0.0f, 0.0f));
		CHECK(NearRgb(SpriteRgb(0x00FF00, 0.75f), 0.0f, 1.0f, 0.0f));
		CHECK(NearRgb(SpriteRgb(0x0000FF, 0.75f), 0.0f, 0.0f, 1.0f));
		CHECK(NearRgb(SpriteRgb(0x800000, 0.75f), 1.0f, 0.0f, 0.0f));  // full value whatever the color's own
		CHECK(NearRgb(SpriteRgb(kWhite, 0.75f), 1.0f, 1.0f, 1.0f));
		CHECK(NearRgb(SpriteRgb(0xFFC0C0, 0.75f), 1.0f, 0.25f, 0.25f));                     // 25% saturated pink -> 75%
		CHECK(NearRgb(SpriteRgb(0x2468FF, 0.75f), 36.0f / 255.0f, 104.0f / 255.0f, 1.0f));  // the game's ward blue: 86% already
		// every hue sixth lands on its own pair of channels
		CHECK(NearRgb(SpriteRgb(0xFFFF00, 0.75f), 1.0f, 1.0f, 0.0f));
		CHECK(NearRgb(SpriteRgb(0x00FFFF, 0.75f), 0.0f, 1.0f, 1.0f));
		CHECK(NearRgb(SpriteRgb(0xFF00FF, 0.75f), 1.0f, 0.0f, 1.0f));
		CHECK(NearRgb(SpriteRgb(0x8000FF, 0.75f), 128.0f / 255.0f, 0.0f, 1.0f));
	}

	// the dome's screen correction: on a screen that keeps only `kept` of a color's saturation (the rest back as a grey of its
	// own luminance), the corrected color reads the color asked for
	void ScreenColor()
	{
		const auto screenSat = [](Color a_c, float a_kept) {
			const auto  x = LightRgb(a_c, false);
			const float grey = (0.2126f * x[0] + 0.7152f * x[1] + 0.0722f * x[2]) * (1.0f - a_kept);
			const float mx = (std::max)({ x[0], x[1], x[2] }) * a_kept + grey, mn = (std::min)({ x[0], x[1], x[2] }) * a_kept + grey;
			return mx <= 0.0f ? 0.0f : (mx - mn) / mx;
		};
		const auto sat = [](Color a_c) {
			const auto x = LightRgb(a_c, false);
			return Sat(x[2], x[1], x[0]);
		};
		CHECK(TrueOnScreen(0xFFBE5A, 0.0f) == 0xFFBE5A);   // off
		CHECK(TrueOnScreen(0xFFBE5A, 1.0f) == 0xFFBE5A);   // a screen that keeps everything needs nothing
		CHECK(TrueOnScreen(0x808080, 0.67f) == 0x808080);  // a grey has no color to keep
		CHECK(TrueOnScreen(0x000000, 0.67f) == 0x000000);
		CHECK(TrueOnScreen(0xFFBE5A, 0.67f) == 0xFFA822);  // gold: its luminance's grey out, the bright channel kept
		CHECK(TrueOnScreen(0x2ED452, 0.67f) == 0x00D42E);  // green: as far as its red allows
		CHECK(TrueOnScreen(0xFFFF00, 0.67f) == 0xFFFF00);  // yellow has no weakest channel left to take from
		for (const Color c : { 0xFFBE5Au, 0xFF2020u, 0x2468FFu, 0x7A3CFFu, 0xFF2EC4u }) {
			CHECK(std::fabs(screenSat(TrueOnScreen(c, 0.67f), 0.67f) - sat(c)) < 0.02f);  // reads as asked
			CHECK(screenSat(c, 0.67f) < sat(c) - 0.02f);                                  // and without it would not
		}
		for (const Color c : { 0x2ED452u, 0xFF6A10u }) {  // too little of their weakest channel to take it all: much nearer
			CHECK(screenSat(TrueOnScreen(c, 0.67f), 0.67f) > screenSat(c, 0.67f) + 0.05f);
		}
		// a plain block's colour key: the brightest key at the block's own glow, the fades scaled down from it, nothing without keys
		CHECK(TrackKey(1.0f, 1.0f, 0.6f) == 0.6f && std::fabs(TrackKey(0.5f, 1.0f, 0.6f) - 0.3f) < 1e-6f);
		CHECK(TrackKey(0.0f, 1.0f, 0.6f) == 0.0f && TrackKey(1.0f, 0.0f, 0.6f) == 0.0f);
		// the menu's Dome color boost: 0 = the color as it is, full = corrected for a screen keeping half
		CHECK(KeptForBoost(0) == 0.0f && TrueOnScreen(0xFFBE5A, KeptForBoost(0)) == 0xFFBE5A);
		CHECK(std::fabs(KeptForBoost(100) - 0.5f) < 1e-6f && std::fabs(KeptForBoost(70) - 0.65f) < 1e-6f);
		CHECK(KeptForBoost(500) == KeptForBoost(100) && KeptForBoost(-3) == 0.0f);
		const Color gold = TrueOnScreen(0xFFBE5A, 0.67f);
		CHECK(((gold >> 16) & 0xFF) == 0xFF);  // as bright as asked at its brightest channel
	}
}

int main()
{
	Text();
	Ladder();
	Ranks();
	Palettes();
	Settings();
	Lights();
	ScreenColor();
	std::printf("%d passed, %d failed\n", gPassed, gFailed);
	if (gPassed < kFloor) {
		std::printf("FAIL: only %d checks ran, under the floor of %d - a test stopped part way\n", gPassed, kFloor);
		return 1;
	}
	return gFailed ? 1 : 0;
}
