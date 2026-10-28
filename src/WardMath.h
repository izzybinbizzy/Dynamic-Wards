// Dynamic Wards - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// The game-free parts of the plugin (no CommonLib here - tests\testwards.cpp builds and runs them without the game, the way
// Illuminated tests its LightMath.h and ConfigText.h): the rows and their colors, the ladder, a ward's rank, the palette
// recolor (wardgen.pal_retint) and the palette file's checks. Plugin.h includes it; every file of the plugin sees it.

#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace Plugin
{
	// a row: five ranks, three worn shield wards, the vampire ward - one set of neutral meshes each (3.0)
	inline constexpr std::string_view kTokens[] = { "Novice", "Apprentice", "Adept", "Expert", "Master", "Spellbreaker",
		"Crusader", "Reman", "Vampire" };
	inline constexpr std::size_t      kRows = std::size(kTokens);
	inline constexpr std::size_t      kRanks = 5;
	// the rows a worn shield raises (Spellbreaker, the Crusader shields, Reman's): each has its own pick - off, the normal dome
	// or the 360 dome (ShieldDome; his order 2026-10-08 made it on / off, his yes 2026-10-12 added the dome)
	inline constexpr std::array<std::size_t, 3> kShieldRows{ 5, 6, 7 };

	// colors are 0xRRGGBB; the ladder runs from white to one color
	using Color = std::uint32_t;

	// Other casters' looks: Followers, Enemies (hostile to the player), Neutral, then the faction groups - one color each, on
	// meshes of their own (`<kind> - <token>.nif`, wardgen LOOK3_ROWS). A look is the art slot kRows + its index; the player's
	// nine rows stay slots 0..kRows-1.
	inline constexpr std::string_view kLookTokens[] = { "Followers", "Enemies", "Neutral", "College", "Thalmor", "PenitusOculatus",
		"Vigilants", "Legion", "Stormcloaks", "Cultists", "DragonPriests", "Afflicted", "Thralls", "Forsworn", "Hagravens",
		"Necromancers", "Dremora", "Falmer", "Draugr", "Bandits", "HostileMages" };
	inline constexpr std::size_t      kLooks = std::size(kLookTokens);
	inline constexpr std::size_t      kSlots = kRows + kLooks;
	inline constexpr std::size_t      kFollowers = 0, kEnemies = 1, kNeutral = 2, kGroups = 3;  // the looks before the factions

	// what a look does: the player's own look, a color of its own, the game's own art, or (a faction only) pass the caster on
	// to Enemies / Neutral
	enum class LookMode : int
	{
		kMine = 0,
		kCustom = 1,
		kVanilla = 2,
		kDefault = 3,
	};

	struct LookPick
	{
		LookMode mode = LookMode::kMine;
		Color    color = 0xFFFFFF;

		friend bool operator==(const LookPick&, const LookPick&) = default;
	};

	// nothing changes until the player picks: the three groups wear the player's look, every faction passes its casters on
	inline constexpr std::array<LookPick, kLooks> DefaultLooks()
	{
		std::array<LookPick, kLooks> out{};
		for (std::size_t i = kGroups; i < kLooks; ++i) {
			out[i].mode = LookMode::kDefault;
		}
		return out;
	}

	// the look a caster who is not the player wears: a follower always the Followers look; else the first faction group in
	// list order he is in whose pick is not Default (a_faction, kLooks when none); else Enemies when he is hostile to the
	// player, Neutral when not
	inline std::size_t LookFor(bool a_follower, bool a_hostile, std::size_t a_faction)
	{
		if (a_follower) {
			return kFollowers;
		}
		if (a_faction >= kGroups && a_faction < kLooks) {
			return a_faction;
		}
		return a_hostile ? kEnemies : kNeutral;
	}
	inline constexpr Color kWhite = 0xFFFFFF;
	struct Preset
	{
		const char* name;
		Color       color;
	};
	// the five colors of 2.x, as quick picks (and how a 2.x settings file's numbers read)
	inline constexpr Preset kPresets[] = { { "Blue", 0x2468FF }, { "Red", 0xFF3648 }, { "Gold", 0xFFBE5A }, { "Green", 0x2DC846 },
		{ "Purple", 0x9646FF }, { "White", kWhite } };
	// what a rankless row wears on its default
	inline constexpr Color kRowDefault[] = { 0, 0, 0, 0, 0, 0xFFBE5A, kWhite, kWhite, 0xFF3648 };
	static_assert(std::size(kRowDefault) == kRows);

	// a ladder's stops, in percent of the way from white to its color (wardgen STAGES2, the same numbers)
	inline constexpr std::array<int, 3> kStages3{ 0, 55, 100 };
	inline constexpr std::array<int, 4> kStages4{ 0, 33, 67, 100 };
	inline constexpr std::array<int, 5> kStages5{ 0, 25, 50, 75, 100 };
	inline constexpr int                kMinStages = 3;
	inline constexpr int                kMaxStages = 5;

	enum class RowMode : int
	{
		kDefault = 0,  // a rank follows the ladder; a rankless row wears its own default
		kCustom = 1,   // a color of its own
		kVanilla = 2,  // the game's own (or another mod's) art
	};

	enum class Unlock : int
	{
		kAlways = 0,
		kSkill50 = 1,
		kSkill75 = 2,
		kSkill100 = 3,
		kPerk = 4,
	};

	// what one shield's ward does (his yes 2026-10-12, a user report: the Spellbreaker ward should not have to wear the 360
	// sphere while the spell wards do): no ward, the normal dome, or the 360 dome (needs 360 Ward + its Universal Patch, and
	// the 360 unlock) - each in that shield's color. Was an on / off switch (2026-10-08).
	enum class ShieldDome : int
	{
		kOff = 0,
		kNormal = 1,
		k360 = 2,
	};

	inline constexpr int kOpacityMin = 25;          // lower reads as nearly invisible
	inline constexpr int kTransparencyDefault = 0;  // his default 2026-10-06 (was 25, 2026-10-03): "transparency 0, opacity 100"
	inline constexpr int kTransparencyMax = 90;     // the dome's fill facing you keeps at least a tenth: the rim alone reads as no ward
	inline constexpr int kBrightnessMin = 25;       // the range is centred on 100 (as built): the slider starts in the middle (his call)
	inline constexpr int kBrightnessMax = 175;

	// ------------------------------------------------------------------ text

	inline std::string Lower(std::string_view a_text)
	{
		std::string out(a_text);
		std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return out;
	}

	// equal, ignoring ASCII case
	inline bool SameText(std::string_view a_a, std::string_view a_b)
	{
		return std::ranges::equal(a_a, a_b, [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
	}

	inline bool Contains(std::string_view a_haystack, std::string_view a_needle)
	{
		return !a_needle.empty() && a_haystack.find(a_needle) != std::string_view::npos;
	}

	inline std::string Trim(std::string_view a_text)
	{
		const auto first = a_text.find_first_not_of(" \t\r\n\f\v");
		if (first == std::string_view::npos) {
			return {};
		}
		const auto last = a_text.find_last_not_of(" \t\r\n\f\v");
		return std::string(a_text.substr(first, last - first + 1));
	}

	// "2468FF"
	inline std::string HexColor(Color a_color)
	{
		return std::format("{:06X}", a_color & 0xFFFFFF);
	}

	// "2468FF", "#2468FF" or "0x2468FF" -> the color; anything else (a wrong length, a stray letter) -> nothing
	inline std::optional<Color> ParseColor(std::string_view a_text)
	{
		while (!a_text.empty() && (a_text.front() == '#' || a_text.front() == ' ')) {
			a_text.remove_prefix(1);
		}
		if (a_text.starts_with("0x") || a_text.starts_with("0X")) {
			a_text.remove_prefix(2);
		}
		if (a_text.size() != 6) {
			return std::nullopt;
		}
		Color       v = 0;
		const auto* end = a_text.data() + a_text.size();
		if (const auto [stop, ec] = std::from_chars(a_text.data(), end, v, 16); ec != std::errc{} || stop != end) {
			return std::nullopt;
		}
		return v;
	}

	// a slot's token: a row's, then a look's
	inline std::string_view SlotToken(std::size_t a_slot)
	{
		return a_slot < kRows ? kTokens[a_slot] : a_slot < kSlots ? kLookTokens[a_slot - kRows] :
		                                                            std::string_view();
	}

	// the row of a token ("Adept" -> 2), any case; kRows when it is none
	inline std::size_t RowOfToken(std::string_view a_token)
	{
		for (std::size_t i = 0; i < kRows; ++i) {
			if (SameText(kTokens[i], a_token)) {
				return i;
			}
		}
		return kRows;
	}

	// ------------------------------------------------------------------ the ladder

	// a_percent of the way from a_from to a_to, each channel rounded to the nearest unit - half to even, as wardgen's
	// ladder_stop rounds (Python's round). Until 3.5 a channel that went DOWN (white toward a color: every ladder) truncated
	// toward zero after adding a half, so it landed one unit off wardgen's own stop (the outside review's 1-unit rounding):
	// Blue's ladder at 25% read DA where wardgen has D9 in green (217.25), C9 for C8 in red (200.25)
	inline Color Mix(Color a_from, Color a_to, int a_percent)
	{
		const int k = std::clamp(a_percent, 0, 100);
		auto      ch = [&](int a_shift) {
			const int a = static_cast<int>((a_from >> a_shift) & 0xFF);
			const int b = static_cast<int>((a_to >> a_shift) & 0xFF);
			const int n = a * 100 + (b - a) * k;  // the channel x 100, exactly: always 0 - 25500
			int       q = n / 100;
			const int r = n % 100;
			if (r > 50 || (r == 50 && (q & 1))) {
				++q;
			}
			return static_cast<Color>(std::clamp(q, 0, 255)) << a_shift;
		};
		return ch(16) | ch(8) | ch(0);
	}

	// where a rank sits on a ladder of a_stages stops, 0 white - 100 the color; ranks past the ladder hold its last stop
	inline int StopPercentOf(int a_stages, bool a_reversed, std::size_t a_rank)
	{
		auto pick = [&](const auto& a_stops) {
			const auto n = a_stops.size();
			const auto pos = (std::min)(a_rank, n - 1);
			return a_stops[a_reversed ? n - 1 - pos : pos];
		};
		return a_stages >= 5 ? pick(kStages5) : a_stages == 4 ? pick(kStages4) :
		                                                        pick(kStages3);
	}

	// ------------------------------------------------------------------ a ward's rank

	// a ward found by what it is, ranked by its minimum skill (Novice 0, Apprentice 25, Adept 50, Expert 75, Master 100)
	inline std::size_t RankRow(std::int32_t a_skill)
	{
		return a_skill >= 100 ? 4 : a_skill >= 75 ? 3 :
		                        a_skill >= 50     ? 2 :
		                        a_skill >= 25     ? 1 :
		                                            0;
	}

	// an always-on ward's rank by its Ward Power, on the vanilla wards' own scale: Lesser Ward 40 (Novice), Steadfast Ward 60
	// (Apprentice), Greater Ward 80 (Adept); past them Expert, and 100 and up Master (Reliquary of Myth's Magnus ward, 999).
	// No magnitude known (0): Novice, as its minimum skill 0 would give
	inline std::size_t PowerRow(float a_power)
	{
		return a_power >= 100.0f ? 4 : a_power > 80.0f ? 3 :
		                           a_power > 60.0f     ? 2 :
		                           a_power > 40.0f     ? 1 :
		                                                 0;
	}

	// the Restoration skill a 360 unlock rule asks for; nothing for the perk rule (a perk decides it)
	inline std::optional<float> UnlockSkill(Unlock a_rule)
	{
		switch (a_rule) {
		case Unlock::kAlways:
			return 0.0f;
		case Unlock::kSkill50:
			return 50.0f;
		case Unlock::kSkill75:
			return 75.0f;
		case Unlock::kSkill100:
			return 100.0f;
		default:
			return std::nullopt;
		}
	}

	// ------------------------------------------------------------------ the palettes (Colors.cpp)

	inline constexpr float         kSatFloor = 0.25f;             // wardgen SAT_FLOOR: a ramp with less color of its own is tinted flat
	inline constexpr float         kHueHold = 0.50f;              // wardgen HUE_HOLD: green and blue keep at least this much hue
	inline constexpr std::uint64_t kMaxPalettePixels = 1u << 20;  // 64x the largest shipped palette

	inline float Sat(float a_b, float a_g, float a_r)
	{
		const float m = (std::max)({ a_b, a_g, a_r });
		return m <= 0.0f ? 0.0f : (m - (std::min)({ a_b, a_g, a_r })) / m;
	}

	// a color whose green or blue leads its red holds part of its hue however pale the texel (white has no hue to hold)
	inline bool HoldsHue(Color a_c)
	{
		const int r = (a_c >> 16) & 0xFF, g = (a_c >> 8) & 0xFF, b = a_c & 0xFF;
		if ((std::max)({ r, g, b }) - (std::min)({ r, g, b }) <= 8) {
			return false;
		}
		return (std::max)(g, b) > r;
	}

	// a gold or orange hue (15-60 degrees, at least 35% saturated): the hues whose vapour spikes Community Shaders shades cream
	// (Aedric FFC420 and Ember FF6A10, his report 2026-10-06; violet, frost, green, pink were fine) - Colors.cpp GoldVapour adds
	// the lighting pick
	inline bool GoldHue(Color a_c)
	{
		const float r = ((a_c >> 16) & 0xFF) / 255.0f, g = ((a_c >> 8) & 0xFF) / 255.0f, b = (a_c & 0xFF) / 255.0f;
		const float mx = (std::max)({ r, g, b }), mn = (std::min)({ r, g, b }), d = mx - mn;
		if (mx <= 0.0f || d / mx < 0.35f || mx != r) {
			return false;
		}
		const float hue = 60.0f * (g - b) / d;  // red is the largest channel: the hue sits between -60 and 60
		return hue >= 15.0f && hue <= 60.0f;
	}

	// one palette's neutral source, mip 0 (BGRA)
	struct PaletteImage
	{
		int                       w = 0, h = 0;
		std::vector<std::uint8_t> bgra;
		float                     sref = 0.0f;  // the ramp's own saturation (its 95th percentile of lit texels)
	};

	// a DDS file's bytes -> its palette image; nothing (and a_why says why) when it is not a small uncompressed 32-bit ramp
	inline std::optional<PaletteImage> PaletteFromDds(const std::vector<std::uint8_t>& a_raw, std::string& a_why)
	{
		if (a_raw.size() < 128 || std::memcmp(a_raw.data(), "DDS ", 4) != 0) {
			a_why = "missing or not a DDS";
			return std::nullopt;
		}
		std::uint32_t h = 0, w = 0, bits = 0;
		std::memcpy(&h, a_raw.data() + 12, 4);
		std::memcpy(&w, a_raw.data() + 16, 4);
		std::memcpy(&bits, a_raw.data() + 88, 4);
		// a palette is a small ramp: no zero side (the mip chain would read an empty buffer) and nothing past the graphics
		// card's 16384 limit (which also keeps w * h * 4 far from overflowing)
		if (w == 0 || h == 0 || w > 16384 || h > 16384) {
			a_why = std::format("an unusable size {}x{}", w, h);
			return std::nullopt;
		}
		// and no more pixels than a ramp could need (CodeRabbit, PR #3: a 16384x16384 file passed the side check and would cost
		// gigabytes here). The shipped palettes are 16384 pixels at most (128x128, 256x64, 512x32).
		if (static_cast<std::uint64_t>(w) * h > kMaxPalettePixels) {
			a_why = std::format("too large for a palette ({}x{})", w, h);
			return std::nullopt;
		}
		if (bits != 32 || a_raw.size() < 128 + static_cast<std::size_t>(w) * h * 4) {
			a_why = "not uncompressed 32-bit";
			return std::nullopt;
		}
		// the texels are read as B, G, R, A: a file in another channel order would come out with red and blue swapped
		std::uint32_t rmask = 0, bmask = 0;
		std::memcpy(&rmask, a_raw.data() + 92, 4);
		std::memcpy(&bmask, a_raw.data() + 100, 4);
		if (rmask != 0x00FF0000 || bmask != 0x000000FF) {
			a_why = "not in BGRA order";
			return std::nullopt;
		}
		PaletteImage p;
		p.w = static_cast<int>(w);
		p.h = static_cast<int>(h);
		p.bgra.assign(a_raw.begin() + 128, a_raw.begin() + 128 + static_cast<std::ptrdiff_t>(w) * h * 4);
		std::vector<float> sats;
		for (std::size_t i = 0; i + 3 < p.bgra.size(); i += 4) {
			const auto b = p.bgra[i], g = p.bgra[i + 1], r = p.bgra[i + 2];
			if ((std::max)({ b, g, r }) >= 32) {
				sats.push_back(Sat(b, g, r));
			}
		}
		std::ranges::sort(sats);
		p.sref = sats.empty() ? 0.0f : sats[static_cast<std::size_t>(static_cast<double>(sats.size()) * 0.95)];
		return p;
	}

	// one mip level down (4 bytes a texel): each texel the average of its 2x2 block; an odd side's last texel is left out, a side
	// of one counts its texel twice
	inline std::vector<std::uint8_t> NextMip(const std::vector<std::uint8_t>& a_prev, int a_pw, int a_ph)
	{
		const int                 w = (std::max)(1, a_pw / 2), h = (std::max)(1, a_ph / 2);
		std::vector<std::uint8_t> next(static_cast<std::size_t>(w) * h * 4);
		for (int y = 0; y < h; ++y) {
			for (int x = 0; x < w; ++x) {
				for (int c = 0; c < 4; ++c) {
					int sum = 0;
					for (int dy = 0; dy < 2; ++dy) {
						for (int dx = 0; dx < 2; ++dx) {
							const int sx = (std::min)(a_pw - 1, x * 2 + dx), sy = (std::min)(a_ph - 1, y * 2 + dy);
							sum += a_prev[(static_cast<std::size_t>(sy) * a_pw + sx) * 4 + c];
						}
					}
					next[(static_cast<std::size_t>(y) * w + x) * 4 + c] = static_cast<std::uint8_t>(sum / 4);
				}
			}
		}
		return next;
	}

	// wardgen.pal_retint, texel for texel: the ramp keeps its own saturation and only its hue moves; alpha is the source's.
	// a_floor: every texel at least this far toward the color (the hand and the domes' cores - Colors.cpp)
	inline std::vector<std::uint8_t> Retint(const PaletteImage& a_s, Color a_c, float a_floor)
	{
		const float               m = static_cast<float>((std::max)({ (a_c >> 16) & 0xFF, (a_c >> 8) & 0xFF, a_c & 0xFF, 1u }));
		const float               tr = ((a_c >> 16) & 0xFF) / m, tg = ((a_c >> 8) & 0xFF) / m, tb = (a_c & 0xFF) / m;
		const bool                curve = a_s.sref >= kSatFloor;
		const float               tsat = Sat(static_cast<float>(a_c & 0xFF), static_cast<float>((a_c >> 8) & 0xFF), static_cast<float>((a_c >> 16) & 0xFF));
		const bool                hold = HoldsHue(a_c);
		std::vector<std::uint8_t> out(a_s.bgra.size());
		for (std::size_t i = 0; i + 3 < a_s.bgra.size(); i += 4) {
			const float b = a_s.bgra[i], g = a_s.bgra[i + 1], r = a_s.bgra[i + 2];
			const float v = (std::max)({ b, g, r });
			float       cb = tb, cg = tg, cr = tr;
			if (curve) {
				float k = tsat <= 0.0f ? 1.0f : (std::min)(1.0f, Sat(b, g, r) / tsat);
				if (hold) {
					k = (std::max)(k, kHueHold);
				}
				k = (std::max)(k, a_floor);
				cb = 1.0f + k * (tb - 1.0f);
				cg = 1.0f + k * (tg - 1.0f);
				cr = 1.0f + k * (tr - 1.0f);
			}
			const auto byte = [v](float a_x) { return static_cast<std::uint8_t>(std::clamp(std::lround(a_x * v), 0L, 255L)); };
			out[i] = byte(cb);
			out[i + 1] = byte(cg);
			out[i + 2] = byte(cr);
			out[i + 3] = a_s.bgra[i + 3];
		}
		return out;
	}

	// one colour key of a plain block: its own brightness a_grey against the brightest key of the block's tracks a_full, at the
	// block's glow a_level - the brightest key wears exactly what the static color did, the fades scale down from it
	[[nodiscard]] constexpr float TrackKey(float a_grey, float a_full, float a_level) noexcept
	{
		return a_full > 0.0f ? a_level * a_grey / a_full : 0.0f;
	}

	// the menu's Dome color boost (0-100 %) as the share of saturation the domes are corrected for (TrueOnScreen): 0 % = none,
	// 100 % = a screen that keeps half of a color's saturation (a room graded like the Bards College cellar keeps about 65 %)
	[[nodiscard]] constexpr float KeptForBoost(int a_boost) noexcept
	{
		return a_boost <= 0 ? 0.0f : 1.0f - 0.5f * static_cast<float>((std::min)(a_boost, 100)) / 100.0f;
	}

	// the color to hand the game so a dome READS a_c on screen. The game draws a glowing effect keeping only a_kept of a color's
	// saturation and adding the rest back as a grey of the color's own brightness (Rec. 709 luminance), so green-heavy colors -
	// gold, yellow, green - turn pale and red / blue / purple barely move. That grey is taken out beforehand, as far as the
	// color's weakest channel allows, and the brightest channel is kept. a_kept 0 (or 1) = the color as it is.
	[[nodiscard]] inline Color TrueOnScreen(Color a_c, float a_kept) noexcept
	{
		if (a_kept <= 0.0f || a_kept >= 1.0f) {
			return a_c;
		}
		const float r = ((a_c >> 16) & 0xFF) / 255.0f, g = ((a_c >> 8) & 0xFF) / 255.0f, b = (a_c & 0xFF) / 255.0f;
		const float mx = (std::max)({ r, g, b }), mn = (std::min)({ r, g, b });
		if (mx <= 0.0f || mx - mn <= 0.0f) {
			return a_c;  // black or a grey: no color to keep
		}
		const float grey = (std::min)((0.2126f * r + 0.7152f * g + 0.0722f * b) * (1.0f - a_kept), mn);
		const float k = mx / (mx - grey);
		const auto  ch = [&](float a_v) { return static_cast<Color>(std::clamp(std::lround((a_v - grey) * k * 255.0f), 0L, 255L)); };
		return (a_c & 0xFF000000u) | (ch(r) << 16) | (ch(g) << 8) | ch(b);
	}

	// ------------------------------------------------------------------ the lights (Lighting.cpp, DomeLights.cpp, Colors.cpp)

	// Community Shaders' inverse-square words (Light Limit Fix's LightFlags), the build's cutoff constant and the cutoff's range:
	// the same numbers as Illuminated's LightMath.h
	inline constexpr std::uint32_t kIslFlag = 1u << 10;
	inline constexpr std::uint32_t kIslInitialised = 1u << 8;
	inline constexpr float         kCutoffK = 3918.88f;
	inline constexpr float         kLowestCutoff = 0.01f, kHighestCutoff = 0.99f;

	// the cutoff an inverse-square light of this fade, reach and size is drawn with: cutoff = K x fade / (reach² + size²)
	[[nodiscard]] inline float CutoffFor(float a_fade, float a_reach, float a_size) noexcept
	{
		const float spread = a_reach * a_reach + a_size * a_size;
		return spread > 0.0f ? std::clamp(kCutoffK * a_fade / spread, kLowestCutoff, kHighestCutoff) : kHighestCutoff;
	}

	// one sRGB channel (0-1) as linear light
	[[nodiscard]] inline float ToLinear(float a_v) noexcept
	{
		return a_v <= 0.04045f ? a_v / 12.92f : std::pow((a_v + 0.055f) / 1.055f, 2.4f);
	}

	// a color's three channels, 0-1: as they are, or as linear light (inverse-square lighting works in linear light)
	[[nodiscard]] inline std::array<float, 3> LightRgb(Color a_color, bool a_linear) noexcept
	{
		std::array<float, 3> out{};
		for (int i = 0; i < 3; ++i) {
			const float v = static_cast<float>((a_color >> (16 - 8 * i)) & 0xFF) / 255.0f;
			out[static_cast<std::size_t>(i)] = a_linear ? ToLinear(v) : v;
		}
		return out;
	}

	// the ENB light sprite's color: the ward's hue at full value and at least a_minSat saturated (a pale light reads white and
	// washes the ward out); a color with no hue to speak of (under 2% saturated) stays white
	[[nodiscard]] inline std::array<float, 3> SpriteRgb(Color a_color, float a_minSat) noexcept
	{
		// at full value each channel is 1 - sat x (its distance below the brightest, in sat's units): only the saturation moves
		const auto  rgb = LightRgb(a_color, false);
		const float mx = (std::max)({ rgb[0], rgb[1], rgb[2] }), mn = (std::min)({ rgb[0], rgb[1], rgb[2] });
		const float sat = mx > 0.0f ? (mx - mn) / mx : 0.0f;
		if (sat <= 0.02f) {
			return { 1.0f, 1.0f, 1.0f };
		}
		const float          scale = (std::max)(sat, a_minSat) / sat;
		std::array<float, 3> out{};
		for (std::size_t i = 0; i < 3; ++i) {
			out[i] = 1.0f - scale * (1.0f - rgb[i] / mx);
		}
		return out;
	}
}
