// Dynamic Wards - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// Which lighting the game runs: the shared GameLighting.h finds it once, at data load, never picked (Community Shaders, ENB or
// Vanilla, the same answer every lighting mod of ours reads); this file keeps it for the rest of the plugin.
//
//   Community Shaders   this plugin makes every ward light: the hand light and the dome light (DomeLights.cpp), inverse square
//                       where Community Shaders draws it (its own shader, or one built into Light Limit Fix), else linear
//   ENB                 an ENB light sprite inside each ward mesh, colored by Colors.cpp, and this plugin's hand and dome lights
//                       as on Vanilla: the sprite alone barely lit the ground (the floor in front of her 18.5 -> 18.2 .. 22.7)
//   Vanilla             as Community Shaders, in the game's own lighting (the house light, 178 / 1.14)
// Every pick also lights the hand in first person (DomeLights.cpp): the game's casting light hangs on the third-person body.
// Both mesh sets ship and Wards.cpp takes the ENB domes on ENB; a `Lighting.txt` an older installer left is not read.
//
// COMMUNITY SHADERS' WORDS: Light Limit Fix reads the first three words of EVERY light's runtime data as its flags, cutoff and
// record (bit 9 Disabled), so whenever CommunityShaders.dll is loaded (Bottled and Open Shaders keep the name) the lights are
// written in its words, never with an ambient color - whatever the pick says.

#include "Plugin.h"

#include <nlohmann/json.hpp>

#include "GameLighting.h"

namespace Plugin
{
	static_assert(static_cast<int>(Lighting::kShaders) == static_cast<int>(GameLighting::Lighting::kShaders) &&
				  static_cast<int>(Lighting::kEnb) == static_cast<int>(GameLighting::Lighting::kEnb) &&
				  static_cast<int>(Lighting::kVanilla) == static_cast<int>(GameLighting::Lighting::kVanilla));

	namespace
	{
		constexpr const char* kEffects11 = "Data/Shaders/Features/Effects11.ini";

		std::atomic<Lighting> gPick{ Lighting::kShaders };
		std::atomic<Lighting> gSeen{ Lighting::kShaders };  // what the game itself shows
		std::atomic<bool>     gE11{ false }, gIsl{ false }, gCs{ false }, gEnbRunning{ false }, gEnbFiles{ false };
	}

	std::string ModelKey(std::string_view a_model)
	{
		auto k = Lower(a_model);
		std::ranges::replace(k, '/', '\\');
		if (k.starts_with("meshes\\")) {
			k.erase(0, 7);
		}
		return k;
	}

	RE::NiColor LightColor(Color a_color, bool a_linear)
	{
		const auto [r, g, b] = LightRgb(a_color, a_linear);
		return { r, g, b };
	}

	void ReadLighting()
	{
		const auto found = GameLighting::Read();
		gCs = found.communityShaders;
		gIsl = found.inverseSquare;
		gEnbRunning = found.enb;
		gEnbFiles = found.enbFilesLeft;
		std::error_code ec;
		gE11 = std::filesystem::exists(kEffects11, ec);
		gSeen = static_cast<Lighting>(found.lighting);
		gPick = gSeen.load();
		const auto lines = GameLighting::Describe(found);
		SKSE::log::info("{}; Effects 11 {}", lines.info, gE11 ? "installed" : "not installed");
		for (const auto& warning : lines.warnings) {
			SKSE::log::warn("{}", warning);
		}
	}

	Lighting LightingPick()
	{
		return gPick.load();
	}

	void SetLightingPick(Lighting a_pick)
	{
		gPick = a_pick;
	}

	bool InverseSquare()
	{
		return gIsl.load();
	}

	bool LightLimitFix()
	{
		return gCs.load();
	}

	nlohmann::json LightingReport()
	{
		return { { "pick", static_cast<int>(gPick.load()) }, { "seen", static_cast<int>(gSeen.load()) }, { "effects11", gE11.load() },
			{ "inverseSquare", gIsl.load() }, { "communityShaders", gCs.load() }, { "enbRunning", gEnbRunning.load() }, { "enbFiles", gEnbFiles.load() } };
	}
}
