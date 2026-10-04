// Dynamic Wards - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// Which lighting the player picked in the installer, and the lights this plugin makes because of it (3.0).
//
//   Community Shaders   this plugin makes every ward light: the hand light (the game's ward light record, one copy per row in
//                       that row's color) and the dome light (DomeLights.cpp, inverse square when Community Shaders has it).
//                       With Effects 11 installed it also lights the first-person hand (DomeLights.cpp) - Light Placer's light
//                       hangs on the third-person body, which first person does not draw.
//   ENB                 the light is an ENB particle light inside each ward mesh, colored by Colors.cpp; the game's light goes.
//                       First person also gets this plugin's hand light (DomeLights.cpp; his call 2026-10-03).
//   Vanilla             as Community Shaders, in the game's own lighting.
//
// The pick is one word in `SKSE\Plugins\Dynamic Wards\Lighting.txt`, which the installer's option installs. No file reads
// as Community Shaders.

#include "Plugin.h"

namespace Plugin
{
	namespace
	{
		constexpr const char*   kPickPath = "Data/SKSE/Plugins/Dynamic Wards/Lighting.txt";
		constexpr const char*   kEffects11 = "Data/Shaders/Features/Effects11.ini";
		constexpr const char*   kIslShader = "Data/Shaders/InverseSquareLighting/InverseSquareLighting.hlsli";
		constexpr const char*   kGame = "Skyrim.esm";
		constexpr std::uint32_t kWardLight = 0x02F3EF;  // MagicLightWardHand01, the light every vanilla ward casts with

		std::mutex                                gLock;
		std::atomic<Lighting>                     gPick{ Lighting::kShaders };
		bool                                      gPickRead = false;
		bool                                      gE11 = false;
		bool                                      gIsl = false;
		const RE::TESObjectLIGH*                  gGame = nullptr;
		std::array<RE::TESObjectLIGH*, kRows>     gHand{};
		std::unordered_set<const RE::TESObjectLIGH*> gOurs;

		Lighting ReadPick()
		{
			std::ifstream in(kPickPath);
			std::string   line;
			while (in && std::getline(in, line)) {
				const auto word = Lower(line.substr(0, line.find_first_of(" \t\r")));
				if (word.empty() || word[0] == '#') {
					continue;
				}
				gPickRead = true;
				return word == "enb" ? Lighting::kEnb : word == "vanilla" ? Lighting::kVanilla : Lighting::kShaders;
			}
			return Lighting::kShaders;
		}
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
		auto ch = [&](int a_shift) {
			const float v = ((a_color >> a_shift) & 0xFF) / 255.0f;
			return a_linear ? (v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f)) : v;
		};
		return { ch(16), ch(8), ch(0) };
	}

	void ReadLighting()
	{
		std::scoped_lock l{ gLock };
		gPick = ReadPick();
		gE11 = std::filesystem::exists(kEffects11);
		gIsl = std::filesystem::exists(kIslShader);
		constexpr const char* kNames[] = { "Community Shaders", "ENB", "Vanilla" };
		SKSE::log::info("lighting: {} ({}); Effects 11 {}; inverse square {}", kNames[static_cast<int>(gPick.load())],
			gPickRead ? "the installer's pick" : "no Lighting.txt, the default", gE11 ? "installed" : "not installed", gIsl ? "on" : "off");
	}

	void MakeHandLights()
	{
		auto*            dh = RE::TESDataHandler::GetSingleton();
		std::scoped_lock l{ gLock };
		gGame = dh ? dh->LookupForm<RE::TESObjectLIGH>(kWardLight, kGame) : nullptr;
		if (!gGame) {
			SKSE::log::warn("lighting: the game's ward light was NOT found - no hand lights");
			return;
		}
		for (auto& h : gHand) {
			auto* out = NewForm<RE::TESObjectLIGH>();
			if (!out) {
				continue;
			}
			out->data = gGame->data;
			out->fade = gGame->fade;
			out->emittanceColor = gGame->emittanceColor;
			out->lensFlare = gGame->lensFlare;
			out->sound = gGame->sound;
			out->SetModel(gGame->GetModel());
			h = out;
			gOurs.insert(out);
		}
	}

	void ColorHandLights()
	{
		const float      dim = Opacity() / 100.0f;
		std::scoped_lock l{ gLock };
		for (std::size_t i = 0; i < kRows; ++i) {
			auto* h = gHand[i];
			const auto c = RowColor(i);
			if (!h || !c || !gGame) {
				continue;
			}
			h->data.color.red = static_cast<std::uint8_t>((*c >> 16) & 0xFF);
			h->data.color.green = static_cast<std::uint8_t>((*c >> 8) & 0xFF);
			h->data.color.blue = static_cast<std::uint8_t>(*c & 0xFF);
			h->fade = gGame->fade * dim;  // his call 2026-10-02: "light should dim with opacity"
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

	bool MeshLights()
	{
		return gPick.load() == Lighting::kEnb;
	}

	bool OwnLights()
	{
		return gPick.load() != Lighting::kEnb;
	}

	bool Effects11()
	{
		std::scoped_lock l{ gLock };
		return gE11;
	}

	bool HandLight1st()
	{
		std::scoped_lock l{ gLock };
		return gPick.load() == Lighting::kEnb || (gE11 && gPick.load() == Lighting::kShaders);
	}

	bool InverseSquare()
	{
		std::scoped_lock l{ gLock };
		return gIsl;
	}

	RE::TESObjectLIGH* HandLightFor(std::size_t a_row)
	{
		std::scoped_lock l{ gLock };
		return a_row < kRows ? gHand[a_row] : nullptr;
	}

	bool IsOurHandLight(const RE::TESObjectLIGH* a_light)
	{
		std::scoped_lock l{ gLock };
		return a_light && gOurs.contains(a_light);
	}

	std::string LightingReport()
	{
		std::scoped_lock l{ gLock };
		return std::format(R"({{"pick":{},"fromInstaller":{},"effects11":{},"inverseSquare":{},"handLights":{}}})", static_cast<int>(gPick.load()),
			gPickRead, gE11, gIsl, gOurs.size());
	}
}
