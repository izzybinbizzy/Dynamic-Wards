// Dynamic Wards - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// The colored light on a ward's dome where Light Placer is not loaded (RE::Light setups). Light Placer lights the
// dome mesh from our config; RE::Light cannot, because the dome is art on the actor, not an object it loads. So the
// same light (Dome Lights.txt, written by the build from the Light Placer config) is hung under the dome's own 3D
// while it shows, and goes with it.
//
// CREDIT: a light is made and registered the way ReLight by Truman does it (github.com/TrumanGIT/ReLight,
// GPL-3.0-or-later, used with his permission): a master NiPointLight cloned for every use, the size in the radius'
// z, and the light handed to the shadow scene node.

#include "Plugin.h"

namespace Plugin
{
	namespace
	{
		constexpr const char*   kPath = "Data/SKSE/Plugins/Dynamic Wards/Dome Lights.txt";
		constexpr const char*   kIslShader = "Data/Shaders/InverseSquareLighting/InverseSquareLighting.hlsli";
		constexpr const char*   kLightName = "DWDome";
		constexpr float         kK = 3918.88f;  // the build's cutoff constant: cutoff = kK * fade / (reach^2 + size^2)
		constexpr std::uint32_t kIslFlag = 1u << 10;
		constexpr auto          kTick = std::chrono::milliseconds(100);

		struct Spec
		{
			RE::NiColor               color;
			float                     fade{ 1.0f }, reach{ 133.0f }, size{ 2.0f };
			std::vector<RE::NiPoint3> points;
		};

		struct Lit
		{
			const RE::ModelReferenceEffect* effect;
			RE::NiPointer<RE::NiAVObject>   art;
			RE::NiPointer<RE::NiPointLight> light;
			RE::NiPointer<RE::BSLight>      bs;
			RE::ObjectRefHandle             target;
		};

		std::mutex                                 gLock;
		std::unordered_map<std::string, Spec>      gSpecs;  // dome model (lower case, under meshes\) -> its light
		std::vector<Lit>                           gLit;
		RE::NiPointer<RE::NiPointLight>            gMaster;
		std::atomic<DomeMode>                      gMode{ DomeMode::kAuto };
		std::atomic_bool                           gQueued{ false };
		bool                                       gReLight = false;
		bool                                       gIsl = false;

		std::string Key(std::string_view a_model)
		{
			auto k = Lower(a_model);
			std::ranges::replace(k, '/', '\\');
			if (k.starts_with("meshes\\")) {
				k.erase(0, 7);
			}
			return k;
		}

		bool Wanted()
		{
			const auto mode = gMode.load();
			return mode == DomeMode::kOn || (mode == DomeMode::kAuto && gReLight && !LightPlacerLoaded());
		}

		void ReadSpecs()
		{
			std::ifstream in(kPath);
			std::string   line;
			while (in && std::getline(in, line)) {
				if (line.empty() || line[0] == '#') {
					continue;
				}
				std::vector<std::string> f;
				std::size_t              at = 0;
				for (std::size_t tab; (tab = line.find('\t', at)) != std::string::npos; at = tab + 1) {
					f.emplace_back(line.substr(at, tab - at));
				}
				f.emplace_back(line.substr(at));
				// dome <model> <r> <g> <b> <fade> <reach> <size> <x> <y> <z>
				if (f.size() != 11 || f[0] != "dome") {
					continue;
				}
				auto n = [&](std::size_t i) { return static_cast<float>(std::atof(f[i].c_str())); };
				auto& s = gSpecs[Key(f[1])];
				s.color = { n(2) / 255.0f, n(3) / 255.0f, n(4) / 255.0f };
				s.fade = n(5);
				s.reach = n(6);
				s.size = n(7);
				s.points.push_back({ n(8), n(9), n(10) });
			}
		}

		RE::NiPointLight* CloneMaster()
		{
			if (!gMaster) {
				// held so it is freed once the master is cloned from it (a bare pointer leaked it)
				const RE::NiPointer<RE::NiPointLight> fresh(RE::NiPointLight::Create());
				auto* clone = fresh ? netimmerse_cast<RE::NiPointLight*>(fresh->Clone()) : nullptr;
				if (!clone) {
					return nullptr;
				}
				gMaster.reset(clone);
			}
			return netimmerse_cast<RE::NiPointLight*>(gMaster->Clone());
		}

		void Hang(RE::ModelReferenceEffect* a_effect, const Spec& a_spec, RE::ShadowSceneNode* a_scene)
		{
			auto* parent = a_effect->artObject3D ? a_effect->artObject3D->AsNode() : nullptr;
			if (!parent) {
				return;
			}
			for (const auto& at : a_spec.points) {
				auto* light = CloneMaster();
				if (!light) {
					return;
				}
				light->name = kLightName;
				auto& data = light->GetLightRuntimeData();
				data.diffuse = a_spec.color;
				data.fade = a_spec.fade;
				data.radius = { a_spec.reach, a_spec.reach, a_spec.size };
				light->SetLightAttenuation(a_spec.reach);
				if (gIsl) {
					// Community Shaders' inverse square lighting: a flag and the cutoff in the two words before the color
					auto* words = reinterpret_cast<std::uint32_t*>(&data);
					words[0] |= kIslFlag;
					words[1] = std::bit_cast<std::uint32_t>(
						std::clamp(kK * a_spec.fade / (a_spec.reach * a_spec.reach + a_spec.size * a_spec.size), 0.01f, 0.99f));
				}
				light->local.translate = at;
				parent->AttachChild(light, true);
				RE::NiUpdateData update{};
				light->Update(update);
				RE::ShadowSceneNode::LIGHT_CREATE_PARAMS params{};
				params.dynamic = true;
				params.shadowLight = false;
				params.portalStrict = true;
				params.affectLand = true;
				params.affectWater = true;
				params.neverFades = true;
				params.fov = 90.0f;
				params.falloff = 1.0f;
				params.nearDistance = 5.0f;
				params.depthBias = 1.0f;
				auto* bs = a_scene->AddLight(light, params);
				if (!bs) {
					parent->DetachChild(light);
					return;
				}
				gLit.push_back({ a_effect, a_effect->artObject3D, RE::NiPointer<RE::NiPointLight>(light), RE::NiPointer<RE::BSLight>(bs),
					a_effect->target });
			}
		}

		void Drop(Lit& a_lit, RE::ShadowSceneNode* a_scene)
		{
			if (a_scene && a_lit.bs) {
				a_scene->RemoveLight(a_lit.bs);
			}
			if (a_lit.light && a_lit.light->parent) {
				a_lit.light->parent->DetachChild(a_lit.light.get());
			}
		}

		// main thread: light every dome that shows, drop the lights of domes that have gone, dark while the wearer sneaks
		void Tick()
		{
			gQueued = false;
			auto* scene = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
			auto* lists = RE::ProcessLists::GetSingleton();
			if (!scene || !lists) {
				return;
			}
			const bool on = Wanted() && ColoredLightsOn();
			std::vector<std::pair<RE::ModelReferenceEffect*, const Spec*>> live;
			std::scoped_lock l{ gLock };
			if (on) {
				RE::BSSpinLockGuard guard{ lists->magicEffectsLock };
				for (auto& temp : lists->magicEffects) {
					auto* e = temp.get();
					if (!e || e->GetType() != RE::TEMP_EFFECT_TYPE::kRefModel) {
						continue;
					}
					auto* m = static_cast<RE::ModelReferenceEffect*>(e);
					if (m->finished || !m->artObject || !m->artObject3D || !m->artObject3D->parent) {
						continue;
					}
					const auto* model = m->artObject->GetModel();
					if (const auto it = gSpecs.find(Key(model ? model : "")); it != gSpecs.end()) {
						live.emplace_back(m, &it->second);
					}
				}
			}
			std::erase_if(gLit, [&](Lit& a_lit) {
				const bool keep = a_lit.light && a_lit.light->parent && std::ranges::any_of(live, [&](const auto& a_live) {
					return a_live.first == a_lit.effect && a_live.first->artObject3D.get() == a_lit.art.get();
				});
				if (!keep) {
					Drop(a_lit, scene);
				}
				return !keep;
			});
			for (const auto& [effect, spec] : live) {
				if (std::ranges::none_of(gLit, [effect](const Lit& a_lit) { return a_lit.effect == effect; })) {
					Hang(effect, *spec, scene);
				}
			}
			for (auto& lit : gLit) {
				const auto ref = lit.target.get();
				const auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
				const bool  hide = actor && actor->IsSneaking();
				if (lit.light->GetAppCulled() != hide) {
					lit.light->SetAppCulled(hide);
				}
			}
		}
	}

	void StartDomeLights()
	{
		{
			std::scoped_lock l{ gLock };
			ReadSpecs();
			gReLight = REX::W32::GetModuleHandleA("ReLight.dll") != nullptr;
			gIsl = std::filesystem::exists(kIslShader);
		}
		SKSE::log::info("dome lights: {} dome model(s) in {}; RE::Light {}; Light Placer {}; inverse square {} - {}", gSpecs.size(), kPath,
			gReLight ? "loaded" : "not loaded", LightPlacerLoaded() ? "loaded" : "not loaded", gIsl ? "on" : "off",
			Wanted() ? "hung by this plugin" : "left to Light Placer or off");
		if (gSpecs.empty()) {
			return;
		}
		// detached, never joined: a join from a DLL's static destructor at exit can hang on the loader lock
		std::thread([]() {
			for (;;) {
				std::this_thread::sleep_for(kTick);
				bool active = false;
				{
					std::scoped_lock l{ gLock };
					active = Wanted() || !gLit.empty();  // after a switch-off, one more tick takes the lights down
				}
				if (active && !gQueued.exchange(true)) {
					Later(Tick);
				}
			}
		}).detach();
	}

	void SetDomeMode(DomeMode a_mode)
	{
		gMode = a_mode;
	}

	std::string DomeLightsReport()
	{
		std::scoped_lock l{ gLock };
		return std::format(R"({{"models":{},"mode":{},"relight":{},"isl":{},"active":{},"lit":{}}})", gSpecs.size(),
			static_cast<int>(gMode.load()), gReLight, gIsl, Wanted(), gLit.size());
	}
}
