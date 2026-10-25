// Dynamic Wards - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// Which lighting the game runs - found by the plugin itself, never picked - and the hand-light records it makes because of it.
//
//   Community Shaders   this plugin makes every ward light: the hand light and the dome light (DomeLights.cpp), inverse square
//                       whenever Community Shaders runs
//   ENB                 an ENB light sprite inside each ward mesh, colored by Colors.cpp, and this plugin's hand and dome lights
//                       as on Vanilla: the sprite alone barely lights the ground
//   Vanilla             as Community Shaders, in the game's own lighting; the hand-light copies take the house light (178 / 1.14)
// Every pick also lights the hand in first person (DomeLights.cpp): the game's casting light hangs on the third-person body.
//
// The game is looked at the way Illuminated looks at it (Lighting.cpp there): ENB only when an ENB module runs. Both mesh sets
// ship and Wards.cpp takes the ENB domes on ENB; a `Lighting.txt` an older installer left is not read.
//
// COMMUNITY SHADERS' WORDS: Light Limit Fix reads the first three words of EVERY light's runtime data as its flags, cutoff and
// record (LLFCommon::RuntimeLightDataExt; bit 9 Disabled), so whenever CommunityShaders.dll is loaded (Bottled and Open Shaders
// keep the name) the lights are written in its words, never with an ambient color - whatever the pick says.

#include "Plugin.h"

#include <Windows.h>
#include <TlHelp32.h>

namespace Plugin
{
	namespace
	{
		constexpr const char* kEffects11 = "Data/Shaders/Features/Effects11.ini";
		constexpr const char* kIslShader = "Data/Shaders/InverseSquareLighting/InverseSquareLighting.hlsli";
		// a Community Shaders that builds inverse square lights into Light Limit Fix (no separate shader file): its attenuation
		// reads each light's InverseSquare flag
		constexpr const char*   kLlfAttenuation = "Data/Shaders/LightLimitFix/Attenuation.hlsli";
		constexpr const char*   kEnbFiles[] = { "enbseries.ini", "enblocal.ini" };
		constexpr const char*   kGame = "Skyrim.esm";
		constexpr std::uint32_t kWardLight = 0x02F3EF;  // MagicLightWardHand01, the light every vanilla ward casts with
		// Vanilla: the game's ward light (radius 50) barely reads in the game's own lighting, so our ward copies take the house
		// light (wardgen.plain_light: LTBG section 4's reach 133 drawn at radius 178, fade 1.14)
		constexpr std::uint32_t kPlainRadius = 178;
		constexpr float         kPlainFade = 1.14f;
		constexpr const char*   kNames[] = { "Community Shaders", "ENB", "Vanilla" };

		std::atomic<Lighting> gPick{ Lighting::kShaders };
		std::atomic<Lighting> gSeen{ Lighting::kShaders };  // what the game itself shows
		std::atomic<bool>     gE11{ false }, gIsl{ false }, gCs{ false }, gEnbRunning{ false }, gEnbFiles{ false };

		std::mutex                                   gLock;  // the hand-light records
		const RE::TESObjectLIGH*                     gGame = nullptr;
		std::array<RE::TESObjectLIGH*, kRows>        gHand{};
		std::unordered_set<const RE::TESObjectLIGH*> gOurs;
		float                                        gHandFade = 1.0f;  // the hand lights' strength before Hand Brightness

		[[nodiscard]] bool Exists(const char* a_path)
		{
			std::error_code ec;
			return std::filesystem::exists(a_path, ec);
		}

		[[nodiscard]] bool CommunityShadersLoaded() { return REX::W32::GetModuleHandleA("CommunityShaders.dll") != nullptr; }

		[[nodiscard]] bool IslBuiltIntoLightLimitFix()
		{
			std::ifstream in(kLlfAttenuation, std::ios::binary);
			if (!in) {
				return false;
			}
			const std::string text{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
			return text.find("InverseSquare") != std::string::npos;
		}

		// an ENB that is running: a loaded module exports its SDK - d3d11.dll as usual, or any other name an injector gave it;
		// nullopt when the module list could not be read, so the caller falls back to ENB's settings files. ENB's settings files
		// alone never make it ENB: a preset left in the game folder with no ENB running is the game's own lighting
		[[nodiscard]] std::optional<bool> EnbRunning()
		{
			for (const char* dll : { "d3d11.dll", "dxgi.dll", "d3d9.dll" }) {
				if (const auto handle = REX::W32::GetModuleHandleA(dll); handle && REX::W32::GetProcAddress(handle, "ENBGetSDKVersion")) {
					return true;
				}
			}
			// a module loading at that moment makes the snapshot fail with ERROR_BAD_LENGTH; Windows asks for a retry
			auto snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);
			for (int tries = 0; snap == INVALID_HANDLE_VALUE && ::GetLastError() == ERROR_BAD_LENGTH && tries < 8; ++tries) {
				snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);
			}
			if (snap == INVALID_HANDLE_VALUE) {
				return std::nullopt;
			}
			MODULEENTRY32W entry{};
			entry.dwSize = sizeof(entry);
			if (!::Module32FirstW(snap, &entry)) {
				::CloseHandle(snap);
				return std::nullopt;
			}
			bool found = false;
			do {
				// a counted reference: a module unloaded after the snapshot cannot be read through a stale handle
				HMODULE module = nullptr;
				if (::GetModuleHandleExW(0, entry.szModule, &module)) {
					found = ::GetProcAddress(module, "ENBGetSDKVersion") != nullptr;
					::FreeLibrary(module);
				}
			} while (!found && ::Module32NextW(snap, &entry));
			::CloseHandle(snap);
			return found;
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
		const auto [r, g, b] = LightRgb(a_color, a_linear);
		return { r, g, b };
	}

	void ReadLighting()
	{
		const bool shaderFile = Exists(kIslShader);
		const bool inLlf = IslBuiltIntoLightLimitFix();
		gCs = CommunityShadersLoaded();
		gIsl = shaderFile || inLlf || gCs;
		gE11 = Exists(kEffects11);
		gEnbFiles = std::ranges::any_of(kEnbFiles, Exists);
		const auto running = EnbRunning();
		gEnbRunning = running.value_or(gEnbFiles.load());
		gSeen = gIsl ? Lighting::kShaders : gEnbRunning ? Lighting::kEnb :
		                                                  Lighting::kVanilla;
		gPick = gSeen.load();
		if (!running) {
			SKSE::log::warn("lighting: the loaded modules could not be listed - ENB judged by its settings files");
		}
		SKSE::log::info(
			"lighting: Automatic - {} (CommunityShaders.dll {}, inverse square shader {}, in Light Limit Fix {}, ENB running {}, ENB "
			"settings in the game folder {}); Effects 11 {}; lights drawn {}",
			kNames[static_cast<int>(gPick.load())], gCs ? "loaded" : "not loaded", shaderFile ? "installed" : "not installed", inLlf ? "yes" : "no",
			gEnbRunning ? "yes" : "no", gEnbFiles ? "yes" : "no", gE11 ? "installed" : "not installed", gIsl ? "inverse square" : "plain");
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
		const bool plain = gPick.load() == Lighting::kVanilla && !gIsl;
		gHandFade = plain ? kPlainFade : gGame->fade;
		for (auto& h : gHand) {
			auto* out = NewForm<RE::TESObjectLIGH>();
			if (!out) {
				continue;
			}
			out->data = gGame->data;
			if (plain) {
				out->data.radius = kPlainRadius;
			}
			out->fade = gHandFade;
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
		const float      dim = HandDim();  // Hand Brightness alone: Opacity and Ward brightness leave the hand
		std::scoped_lock l{ gLock };
		for (std::size_t i = 0; i < kRows; ++i) {
			auto*      h = gHand[i];
			const auto c = RowColor(i);
			if (!h || !c || !gGame) {
				continue;
			}
			h->data.color.red = static_cast<std::uint8_t>((*c >> 16) & 0xFF);
			h->data.color.green = static_cast<std::uint8_t>((*c >> 8) & 0xFF);
			h->data.color.blue = static_cast<std::uint8_t>(*c & 0xFF);
			h->fade = gHandFade * dim;
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
		// every pick: on ENB the mesh's light sprite alone barely lit the ground (measured: the floor in front of her 18.5 ->
		// 18.2 .. 22.7 on a 0-255 scale), so ENB gets this plugin's hand and dome lights too
		return true;
	}

	bool Effects11()
	{
		return gE11.load();
	}

	bool HandLight1st()
	{
		return true;  // every pick: the game's casting light, Light Placer's and the ENB sprite hang on the third-person body
	}

	bool InverseSquare()
	{
		return gIsl.load();
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
		return std::format(
			R"({{"pick":{},"seen":{},"effects11":{},"inverseSquare":{},"communityShaders":{},"enbRunning":{},"enbFiles":{},"handLights":{}}})",
			static_cast<int>(gPick.load()), static_cast<int>(gSeen.load()), gE11.load(), gIsl.load(), gCs.load(), gEnbRunning.load(),
			gEnbFiles.load(), gOurs.size());
	}
}
