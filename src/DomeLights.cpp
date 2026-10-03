// Dynamic Wards - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// The colored light on a ward's dome (every pick but ENB, 3.0), and - only where Community Shaders' Effects 11 is installed
// - a light on the FIRST-PERSON hand while a ward is cast: the game's casting light and Light Placer's hang on the
// third-person body, which first person does not draw. Never on a plain ENB (his call, 2026-10-02).
// Each light takes its row's color and the opacity slider every tick, so a change in the menu reaches a ward already up.
// Where it sits, how far it reaches and how strong it is are read from `Dome Lights.txt`, which the build writes.
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
		constexpr const char*   kLightName = "DWDome";
		constexpr const char*   kHandName = "DWHand1st";
		constexpr float         kK = 3918.88f;  // the build's cutoff constant: cutoff = kK * fade / (reach^2 + size^2)
		constexpr std::uint32_t kIslFlag = 1u << 10;
		constexpr auto          kTick = std::chrono::milliseconds(100);
		// the first-person hand light: the house reach (LTBG §4), a little in front of the palm
		constexpr float         kHandFade = 1.0f, kHandReach = 133.0f, kHandSize = 2.0f, kHandPlainRadius = 178.0f, kHandPlainFade = 1.14f;
		constexpr const char*   kMagicNodes[] = { "NPC L MagicNode [LMag]", "NPC R MagicNode [RMag]" };  // Actor::SlotTypes order

		struct Spec
		{
			float                     fade{ 1.0f }, reach{ 133.0f }, size{ 2.0f };
			std::vector<RE::NiPoint3> points;
			bool                      plain{ false };
			float                     plainFade{ 1.0f }, plainRadius{ 178.0f };
		};

		struct Lit
		{
			const RE::ModelReferenceEffect* effect;
			RE::NiPointer<RE::NiAVObject>   art;
			RE::NiPointer<RE::NiPointLight> light;
			RE::NiPointer<RE::BSLight>      bs;
			RE::ObjectRefHandle             target;
			std::size_t                     row;
			const Spec*                     spec;
		};

		struct Hand1st
		{
			RE::NiPointer<RE::NiNode>       node;
			RE::NiPointer<RE::NiPointLight> light;
			RE::NiPointer<RE::BSLight>      bs;
		};

		std::mutex                            gLock;
		std::unordered_map<std::string, Spec> gSpecs;  // dome model (lower case, under meshes\) -> its light
		std::vector<Lit>                      gLit;
		std::array<Hand1st, 2>                gHands{};
		std::size_t                           gHandsMade = 0;
		RE::NiPointer<RE::NiPointLight>       gMaster;
		std::atomic<DomeMode>                 gMode{ DomeMode::kAuto };
		std::atomic_bool                      gQueued{ false };

		bool Wanted()
		{
			const auto mode = gMode.load();
			return mode == DomeMode::kOn || (mode == DomeMode::kAuto && OwnLights());
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
				auto n = [&](std::size_t i) { return static_cast<float>(std::atof(f[i].c_str())); };
				// vdome <model> <r> <g> <b> <fade> <radius> <x> <y> <z> - the colour columns are 2.x's; 3.0 colors at run time
				if (f.size() == 10 && f[0] == "vdome") {
					auto& s = gSpecs[ModelKey(f[1])];
					s.plain = true;
					s.plainFade = n(5);
					s.plainRadius = n(6);
					continue;
				}
				// dome <model> <r> <g> <b> <fade> <reach> <size> <x> <y> <z>
				if (f.size() != 11 || f[0] != "dome") {
					continue;
				}
				auto& s = gSpecs[ModelKey(f[1])];
				s.fade = n(5);
				s.reach = n(6);
				s.size = n(7);
				s.points.push_back({ n(8), n(9), n(10) });
			}
		}

		RE::NiPointLight* CloneMaster()
		{
			if (!gMaster) {
				const RE::NiPointer<RE::NiPointLight> fresh(RE::NiPointLight::Create());
				auto* clone = fresh ? netimmerse_cast<RE::NiPointLight*>(fresh->Clone()) : nullptr;
				if (!clone) {
					return nullptr;
				}
				gMaster.reset(clone);
			}
			return netimmerse_cast<RE::NiPointLight*>(gMaster->Clone());
		}

		// color, strength and reach: the row's color, dimmed by the opacity slider (his call: "light should dim with opacity")
		void Dress(RE::NiPointLight* a_light, Color a_color, float a_fade, float a_reach, float a_size, float a_plainFade, float a_plainRadius, bool a_plain)
		{
			const bool  isl = InverseSquare();
			const float dim = Opacity() / 100.0f;
			auto&       data = a_light->GetLightRuntimeData();
			if (!isl && a_plain) {
				data.diffuse = LightColor(a_color, false);  // the game's own lighting (Vanilla, an ENB): an sRGB color, drawn as it is
				data.fade = a_plainFade * dim;
				data.radius = { a_plainRadius, a_plainRadius, a_plainRadius };
			} else {
				data.diffuse = LightColor(a_color, isl);
				data.fade = a_fade * dim;
				data.radius = { a_reach, a_reach, a_size };
			}
			if (isl) {
				// Community Shaders' inverse square lighting: a flag and the cutoff in the two words before the color
				auto* words = reinterpret_cast<std::uint32_t*>(&data);
				words[0] |= kIslFlag;
				words[1] = std::bit_cast<std::uint32_t>(std::clamp(kK * a_fade / (a_reach * a_reach + a_size * a_size), 0.01f, 0.99f));
			}
		}

		RE::NiPointLight* Make(RE::NiNode* a_parent, const char* a_name, const RE::NiPoint3& a_at, RE::ShadowSceneNode* a_scene, RE::BSLight*& a_bs)
		{
			auto* light = CloneMaster();
			if (!light) {
				return nullptr;
			}
			light->name = a_name;
			light->local.translate = a_at;
			light->SetLightAttenuation(light->GetLightRuntimeData().radius.x);
			a_parent->AttachChild(light, true);
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
			a_bs = a_scene->AddLight(light, params);
			if (!a_bs) {
				a_parent->DetachChild(light);
				return nullptr;
			}
			return light;
		}

		void Hang(RE::ModelReferenceEffect* a_effect, std::size_t a_row, const Spec& a_spec, RE::ShadowSceneNode* a_scene)
		{
			auto*      parent = a_effect->artObject3D ? a_effect->artObject3D->AsNode() : nullptr;
			const auto color = RowColor(a_row);
			if (!parent || !color) {
				return;
			}
			for (const auto& at : a_spec.points) {
				RE::BSLight* bs = nullptr;
				auto*        light = CloneMaster();
				if (!light) {
					return;
				}
				Dress(light, *color, a_spec.fade, a_spec.reach, a_spec.size, a_spec.plainFade, a_spec.plainRadius, a_spec.plain);
				light->name = kLightName;
				light->local.translate = at;
				light->SetLightAttenuation(light->GetLightRuntimeData().radius.x);
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
				bs = a_scene->AddLight(light, params);
				if (!bs) {
					parent->DetachChild(light);
					return;
				}
				gLit.push_back({ a_effect, a_effect->artObject3D, RE::NiPointer<RE::NiPointLight>(light), RE::NiPointer<RE::BSLight>(bs),
					a_effect->target, a_row, &a_spec });
			}
		}

		void Drop(RE::NiPointLight* a_light, RE::NiPointer<RE::BSLight>& a_bs, RE::ShadowSceneNode* a_scene)
		{
			if (a_scene && a_bs) {
				a_scene->RemoveLight(a_bs);
			}
			if (a_light && a_light->parent) {
				a_light->parent->DetachChild(a_light);
			}
		}

		// the row of the ward a first-person hand is casting now, or kRows
		std::size_t CastingRow(RE::PlayerCharacter* a_player, std::size_t a_slot)
		{
			auto* caster = a_player->GetActorRuntimeData().magicCasters[a_slot];
			if (!caster || !caster->currentSpell) {
				return kRows;
			}
			using S = RE::MagicCaster::State;
			const auto state = caster->state.get();
			if (state != S::kCasting && state != S::kCharging && state != S::kReady) {
				return kRows;
			}
			for (auto* e : caster->currentSpell->effects) {
				const auto* art = e && e->baseEffect ? e->baseEffect->data.castingArt : nullptr;
				if (const auto row = art && art->GetModel() ? RowOfModel(art->GetModel()) : kRows; row < kRows) {
					return row;
				}
			}
			return kRows;
		}

		// Effects 11 only: a light on each first-person hand that is casting one of our wards
		void TickHands(RE::ShadowSceneNode* a_scene, bool a_on)
		{
			auto*      player = RE::PlayerCharacter::GetSingleton();
			auto*      cam = RE::PlayerCamera::GetSingleton();
			const bool first = cam && cam->IsInFirstPerson();
			auto*      body = player ? player->Get3D(true) : nullptr;
			for (std::size_t slot = 0; slot < 2; ++slot) {
				auto&      h = gHands[slot];
				const auto row = (a_on && first && body && player) ? CastingRow(player, slot) : kRows;
				const auto color = row < kRows ? RowColor(row) : std::nullopt;
				if (!color) {
					if (h.light) {
						Drop(h.light.get(), h.bs, a_scene);
						h = {};
					}
					continue;
				}
				auto* node = body->GetObjectByName(RE::BSFixedString(kMagicNodes[slot]));
				auto* parent = node ? node->AsNode() : nullptr;
				if (!parent) {
					continue;
				}
				if (h.light && h.node.get() != parent) {
					Drop(h.light.get(), h.bs, a_scene);
					h = {};
				}
				if (!h.light) {
					RE::BSLight* bs = nullptr;
					if (auto* light = Make(parent, kHandName, { 0.0f, 0.0f, 0.0f }, a_scene, bs)) {
						h = { RE::NiPointer<RE::NiNode>(parent), RE::NiPointer<RE::NiPointLight>(light), RE::NiPointer<RE::BSLight>(bs) };
						++gHandsMade;
					}
				}
				if (h.light) {
					Dress(h.light.get(), *color, kHandFade, kHandReach, kHandSize, kHandPlainFade, kHandPlainRadius, true);
				}
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
			std::vector<std::tuple<RE::ModelReferenceEffect*, std::size_t, const Spec*>> live;
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
					if (const auto it = gSpecs.find(ModelKey(model ? model : "")); it != gSpecs.end()) {
						if (const auto row = RowOfModel(model); row < kRows) {
							live.emplace_back(m, row, &it->second);
						}
					}
				}
			}
			std::erase_if(gLit, [&](Lit& a_lit) {
				const bool keep = a_lit.light && a_lit.light->parent && std::ranges::any_of(live, [&](const auto& a_live) {
					return std::get<0>(a_live) == a_lit.effect && std::get<0>(a_live)->artObject3D.get() == a_lit.art.get();
				});
				if (!keep) {
					Drop(a_lit.light.get(), a_lit.bs, scene);
				}
				return !keep;
			});
			for (const auto& [effect, row, spec] : live) {
				if (std::ranges::none_of(gLit, [effect](const Lit& a_lit) { return a_lit.effect == effect; })) {
					Hang(effect, row, *spec, scene);
				}
			}
			for (auto& lit : gLit) {
				const auto ref = lit.target.get();
				const auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
				const bool  hide = actor && actor->IsSneaking();
				if (lit.light->GetAppCulled() != hide) {
					lit.light->SetAppCulled(hide);
				}
				if (const auto c = RowColor(lit.row)) {
					Dress(lit.light.get(), *c, lit.spec->fade, lit.spec->reach, lit.spec->size, lit.spec->plainFade, lit.spec->plainRadius, lit.spec->plain);
				}
			}
			TickHands(scene, Effects11() && LightingPick() == Lighting::kShaders && WardLightOn());
		}
	}

	void StartDomeLights()
	{
		{
			std::scoped_lock l{ gLock };
			ReadSpecs();
		}
		SKSE::log::info("dome lights: {} dome model(s) in {}; {}; first-person hand light {}", gSpecs.size(), kPath,
			Wanted() ? "hung by this plugin" : MeshLights() ? "in the ward meshes (ENB)" : "off",
			Effects11() && LightingPick() == Lighting::kShaders ? "on (Effects 11)" : "off");
		// detached, never joined: a join from a DLL's static destructor at exit can hang on the loader lock
		std::thread([]() {
			for (;;) {
				std::this_thread::sleep_for(kTick);
				bool active = false;
				{
					std::scoped_lock l{ gLock };
					active = Wanted() || !gLit.empty() || gHands[0].light || gHands[1].light || Effects11();
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
		return std::format(R"({{"models":{},"mode":{},"active":{},"lit":{},"hand1st":[{},{}],"hand1stMade":{}}})", gSpecs.size(),
			static_cast<int>(gMode.load()), Wanted(), gLit.size(), gHands[0].light != nullptr, gHands[1].light != nullptr, gHandsMade);
	}
}
