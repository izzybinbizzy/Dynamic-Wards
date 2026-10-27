// Dynamic Wards - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// The colored light on a ward's dome, and a light on each hand casting or readying one of our wards, first person included -
// on every lighting pick. The player's hand lights hang under the THIRD-person body in first person too (TickHands has the
// measurement): under the first-person skeleton a light lit nothing on ENB. (The name DWHand1st is kept - reports read it.)
// Each light takes its row's color and its slider every tick, so a change in the menu reaches a ward already up.
// Where a dome light sits, how far it reaches and how strong it is are read from `Dome Lights.txt`, which the build writes.
//
// Why a 100 ms tick on the main thread and not the player's update (Illuminated's frame hook): the player's update stops
// while a menu pauses the game and while DevBench's camera is free (measured on Illuminated's lights), and those are the
// times the wards' colors are set live and filmed. A queued task runs in both.
//
// CREDIT: a light is made and registered the way ReLight by Truman does it (github.com/TrumanGIT/ReLight,
// GPL-3.0-or-later, used with his permission): a master NiPointLight cloned for every use, the size in the radius'
// z, and the light handed to the shadow scene node.

#include "Plugin.h"

#include <cmath>

namespace Plugin
{
	namespace
	{
		constexpr const char* kPath = "Data/SKSE/Plugins/Dynamic Wards/Dome Lights.txt";
		constexpr const char* kLightName = "DWDome";
		constexpr const char* kHandName = "DWHand1st";
		constexpr float       kAmbientRatio = 0.1f;  // RE::Light's default ambientRatio (Truman's config.h)
		constexpr auto        kTick = std::chrono::milliseconds(100);
		// the hand light: the house reach (LTBG section 4)
		constexpr float       kHandFade = 1.0f, kHandReach = 133.0f, kHandSize = 2.0f, kHandPlainRadius = 178.0f, kHandPlainFade = 1.14f;
		constexpr const char* kMagicNodes[] = { "NPC L MagicNode [LMag]", "NPC R MagicNode [RMag]" };  // Actor::SlotTypes order

		struct Spec
		{
			float                     fade{ 1.0f }, reach{ 133.0f }, size{ 2.0f };
			std::vector<RE::NiPoint3> points;
			bool                      plain{ false };
			float                     plainFade{ 1.0f }, plainRadius{ 178.0f };
		};

		struct Lit
		{
			const RE::ModelReferenceEffect* effect{ nullptr };
			RE::NiPointer<RE::NiAVObject>   art;
			RE::NiPointer<RE::NiPointLight> light;
			RE::NiPointer<RE::BSLight>      bs;
			RE::ObjectRefHandle             target;
			std::size_t                     row{ 0 };
			const Spec*                     spec{ nullptr };
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
		// every actor casting one of our wards: a light on each casting hand (his order 2026-10-05: first person on every pick,
		// and the light must reach the GROUND - the game's casting light lit the caster but not the land, so on the picks where
		// this plugin makes the lights it hangs the hand light itself, made like the dome light: affectLand on)
		struct ActorHands
		{
			std::array<Hand1st, 2> h{};
			bool                   seen = false;
		};
		std::unordered_map<RE::FormID, ActorHands> gHands;
		std::size_t                                gHandsMade = 0;
		RE::NiPointer<RE::NiPointLight>            gMaster;
		std::atomic<DomeMode>                      gMode{ DomeMode::kAuto };
		std::atomic_bool                           gQueued{ false };
		RE::NiColor                                gLastAmbient{};
		std::size_t                                gBadLines = 0;  // Dome Lights.txt lines left out (a number that did not read)
		// each dome art object's light and row, found once (ModelKey made a string per effect per tick)
		std::unordered_map<const RE::BGSArtObject*, std::pair<const Spec*, std::size_t>> gDomeOf;

		bool Wanted()
		{
			return gMode.load() != DomeMode::kOff;  // every lighting pick gets this plugin's lights
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
				// a number that does not read, or is not finite, leaves the whole line out (atof took "nan" and the locale's comma)
				bool bad = false;
				auto n = [&](std::size_t i) {
					float       v = 0.0f;
					const auto& s = f[i];
					const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
					bad = bad || ec != std::errc{} || end != s.data() + s.size() || !std::isfinite(v);
					return v;
				};
				// vdome <model> <r> <g> <b> <fade> <radius> <x> <y> <z> - the colour columns are 2.x's; 3.0 colors at run time
				if (f.size() == 10 && f[0] == "vdome") {
					const float fade = n(5), radius = n(6);
					if (bad) {
						++gBadLines;
						continue;
					}
					auto& s = gSpecs[ModelKey(f[1])];
					s.plain = true;
					s.plainFade = fade;
					s.plainRadius = radius;
					continue;
				}
				// dome <model> <r> <g> <b> <fade> <reach> <size> <x> <y> <z>
				if (f.size() != 11 || f[0] != "dome") {
					continue;
				}
				const float        fade = n(5), reach = n(6), size = n(7);
				const RE::NiPoint3 point{ n(8), n(9), n(10) };
				if (bad) {
					++gBadLines;
					continue;
				}
				auto& s = gSpecs[ModelKey(f[1])];
				s.fade = fade;
				s.reach = reach;
				s.size = size;
				s.points.push_back(point);
			}
		}

		// ENB (his report 2026-10-06 ~21:30, "wards still dont provide enb light and if they do its too small") and Vanilla (his
		// report ~22:10, "fix vanilla ward light because ... it is too low or not there as well"): the dome light in the game's
		// own lighting reaches 178 (the house light) - barely past the dome itself (~83). On those two picks it reaches
		// kPlainDomeReach times as far (Community Shaders' inverse square light keeps its own reach).
		constexpr float kPlainDomeReach = 2.25f;
		float           DomeRadius(const Spec& a_spec)
		{
			return a_spec.plainRadius * (LightingPick() != Lighting::kShaders ? kPlainDomeReach : 1.0f);
		}

		RE::NiPointLight* CloneMaster()
		{
			if (!gMaster) {
				const RE::NiPointer<RE::NiPointLight> fresh(RE::NiPointLight::Create());
				auto*                                 clone = fresh ? netimmerse_cast<RE::NiPointLight*>(fresh->Clone()) : nullptr;
				if (!clone) {
					return nullptr;
				}
				gMaster.reset(clone);
			}
			return netimmerse_cast<RE::NiPointLight*>(gMaster->Clone());
		}

		// color, strength and reach: the row's color, dimmed by `a_dim` - a dome's light by Opacity x Ward brightness, a hand's by
		// Hand Brightness alone (Settings.cpp LightDim / HandDim)
		void Dress(RE::NiPointLight* a_light, Color a_color, float a_fade, float a_reach, float a_size, float a_plainFade, float a_plainRadius, bool a_plain,
			float a_dim)
		{
			const bool isl = InverseSquare();
			auto&      data = a_light->GetLightRuntimeData();
			if (!isl && a_plain) {
				data.diffuse = LightColor(a_color, false);  // the game's own lighting (Vanilla, an ENB): an sRGB color, drawn as it is
				data.fade = a_plainFade * a_dim;
				data.radius = { a_plainRadius, a_plainRadius, a_plainRadius };
			} else {
				data.diffuse = LightColor(a_color, isl);
				data.fade = a_fade * a_dim;
				data.radius = { a_reach, a_reach, a_size };
			}
			// it writes the first two of the words below, so it goes first: written after them, every later tick ORed the flag onto
			// the attenuation's float, which could read as Light Limit Fix's Disabled bit
			a_light->SetLightAttenuation(data.radius.x);
			if (!isl) {
				// RE::Light's rule (Truman, LightData.cpp setNiPointLightAmbientAndDiffuse): ambient = diffuse x 0.1 - a new
				// NiPointLight starts with a WHITE ambient. Under inverse square lighting these words carry its flags and cutoff.
				data.ambient = { data.diffuse.red * kAmbientRatio, data.diffuse.green * kAmbientRatio, data.diffuse.blue * kAmbientRatio };
				gLastAmbient = data.ambient;
				return;
			}
			// Community Shaders' flags, cutoff and light record in the three words before the color (LLFCommon::RuntimeLightDataExt).
			// A light of our own, so Community Shaders set none of them: the words are ours whole, written as bytes. Initialised as
			// Light Limit Fix marks its own lights (else it re-marks and logs ours each frame); no record - the clone's ambient blue
			// (1.0) would read as one. The cutoff follows the DIMMED fade, so the reach holds while the light dims: off the
			// undimmed fade Light Limit Fix's radius went below zero under ~11 % and the light went out
			const std::array<std::uint32_t, 3> words{ kIslFlag | kIslInitialised, std::bit_cast<std::uint32_t>(CutoffFor(data.fade, a_reach, a_size)), 0u };
			std::memcpy(reinterpret_cast<std::byte*>(&data), words.data(), sizeof(words));
		}

		// A new light under a_parent, registered with the scene; a_dress sets its color and strength before the scene first sees
		// it. nullptr when the game could not make it or the scene refused it (then it is taken off again). Main thread.
		template <class F>
		RE::NiPointLight* Make(RE::NiNode* a_parent, const char* a_name, const RE::NiPoint3& a_at, RE::ShadowSceneNode* a_scene,
			RE::NiPointer<RE::BSLight>& a_bs, F&& a_dress)
		{
			auto* light = CloneMaster();
			if (!light) {
				return nullptr;
			}
			std::forward<F>(a_dress)(light);
			light->name = a_name;
			light->local.translate = a_at;
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
			a_bs.reset(a_scene->AddLight(light, params));
			if (!a_bs) {
				a_parent->DetachChild(light);
				return nullptr;
			}
			return light;
		}

		void DressDome(RE::NiPointLight* a_light, Color a_color, const Spec& a_spec)
		{
			Dress(a_light, a_color, a_spec.fade, a_spec.reach, a_spec.size, a_spec.plainFade, DomeRadius(a_spec), a_spec.plain, LightDim());
		}

		void DressHand(RE::NiPointLight* a_light, Color a_color)
		{
			Dress(a_light, a_color, kHandFade, kHandReach, kHandSize, kHandPlainFade, kHandPlainRadius, true, HandDim());
		}

		// a dome that shows now: its effect only names it (never read - it may be gone once the effects' lock is let go), its art
		// and wearer are held while the lock is
		struct Showing
		{
			const RE::ModelReferenceEffect* effect{ nullptr };
			RE::NiPointer<RE::NiAVObject>   art;
			RE::ObjectRefHandle             target;
			std::size_t                     row{ kRows };
			const Spec*                     spec{ nullptr };
		};

		void Hang(const Showing& a_s, RE::ShadowSceneNode* a_scene)
		{
			auto*      parent = a_s.art ? a_s.art->AsNode() : nullptr;
			const auto color = RowColor(a_s.row);
			if (!parent || !color) {
				return;
			}
			for (const auto& at : a_s.spec->points) {
				RE::NiPointer<RE::BSLight> bs;
				auto*                      light = Make(parent, kLightName, at, a_scene, bs, [&](RE::NiPointLight* a_light) { DressDome(a_light, *color, *a_s.spec); });
				if (!light) {
					return;
				}
				gLit.push_back({ a_s.effect, a_s.art, RE::NiPointer<RE::NiPointLight>(light), std::move(bs), a_s.target, a_s.row, a_s.spec });
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

		// the row of the ward a hand is casting now, or kRows
		// the hand's ward: while it is cast (charging, ready, held) the caster's spell; while it is only readied in that hand
		// (the casting art shows, nothing cast yet - his catch 2026-10-05, "im talking about the casting art too") the spell
		// equipped there, hands drawn
		// a STAFF drawn in that hand while an always-on ward of ours is up (Reliquary of Myth's Staff of Magnus raises its ward
		// as an ability while held): that ward's row (3.4, a user report: no light in first person). Asked after the spell
		// above found no ward, so firing the staff keeps the light; read once per actor and tick (a_alwaysOn)
		std::size_t CastingRow(RE::Actor* a_actor, std::size_t a_slot, std::optional<std::size_t>& a_alwaysOn)
		{
			auto*                caster = a_actor->GetActorRuntimeData().magicCasters[a_slot];
			const RE::MagicItem* spell = nullptr;
			using S = RE::MagicCaster::State;
			if (caster && caster->currentSpell) {
				const auto state = caster->state.get();
				if (state == S::kCasting || state == S::kCharging || state == S::kReady) {
					spell = caster->currentSpell;
				}
			}
			const bool drawn = a_actor->AsActorState()->IsWeaponDrawn();
			auto*      held = drawn ? a_actor->GetEquippedObject(a_slot == 0) : nullptr;
			if (!spell) {
				spell = skyrim_cast<RE::SpellItem*>(held);
			}
			if (spell) {
				for (auto* e : spell->effects) {
					const auto* art = e && e->baseEffect ? e->baseEffect->data.castingArt : nullptr;
					if (const auto row = art && art->GetModel() ? RowOfModel(art->GetModel()) : kRows; row < kRows) {
						return row;
					}
				}
			}
			if (const auto* weap = held ? held->As<RE::TESObjectWEAP>() : nullptr; weap && weap->IsStaff()) {
				if (!a_alwaysOn) {
					a_alwaysOn = AlwaysOnWardRow(a_actor);
				}
				return *a_alwaysOn;
			}
			return kRows;
		}

		// made on the first tick, after data load (the game's string cache is up by then)
		const RE::BSFixedString& MagicNode(std::size_t a_slot)
		{
			static const std::array<RE::BSFixedString, 2> names{ RE::BSFixedString(kMagicNodes[0]), RE::BSFixedString(kMagicNodes[1]) };
			return names[a_slot];
		}

		void DropHand(Hand1st& a_h, RE::ShadowSceneNode* a_scene)
		{
			if (a_h.light) {
				Drop(a_h.light.get(), a_h.bs, a_scene);
			}
			a_h = {};
		}

		// one actor's two hands: a light on each hand casting one of our wards, under that hand's magic node
		void TickActor(RE::Actor* a_actor, RE::NiAVObject* a_body, ActorHands& a_hands, RE::ShadowSceneNode* a_scene)
		{
			std::optional<std::size_t> alwaysOn;  // AlwaysOnWardRow, read at most once for both hands
			for (std::size_t slot = 0; slot < 2; ++slot) {
				auto&      h = a_hands.h[slot];
				const auto row = a_body ? CastingRow(a_actor, slot, alwaysOn) : kRows;
				const auto color = row < kRows ? RowColor(row) : std::nullopt;
				auto*      node = color ? a_body->GetObjectByName(MagicNode(slot)) : nullptr;
				auto*      parent = node ? node->AsNode() : nullptr;
				if (!parent || (h.light && h.node.get() != parent)) {
					DropHand(h, a_scene);
				}
				if (!parent) {
					continue;
				}
				if (h.light) {
					DressHand(h.light.get(), *color);
				} else {
					RE::NiPointer<RE::BSLight> bs;
					if (auto* light = Make(parent, kHandName, {}, a_scene, bs, [&](RE::NiPointLight* a_light) { DressHand(a_light, *color); })) {
						h = { RE::NiPointer<RE::NiNode>(parent), RE::NiPointer<RE::NiPointLight>(light), std::move(bs) };
						++gHandsMade;
					}
				}
			}
		}

		// The hand lights: on every actor casting one of our wards, the player included, on every pick (ENB too).
		// Always under the THIRD-person body's magic nodes, first person included - MEASURED 2026-10-07
		// on ENB (a user's report "the ENB light in first person did not work"): the same hand light hung under the
		// first-person skeleton's magic nodes sat right (in front of her at camera height) yet lit nothing - the ground ON /
		// OFF / ON 45.6 / 44.2 / 45.4 - while under the third-person nodes it doubled the ground (117.9 / 57.5 / 117.9).
		void TickHands(RE::ShadowSceneNode* a_scene, bool a_on)
		{
			for (auto& [id, a] : gHands) {
				a.seen = false;
			}
			auto visit = [&](RE::Actor* a_actor) {
				if (!a_actor) {
					return;
				}
				auto* body = a_actor->Get3D(false);
				auto& a = gHands[a_actor->GetFormID()];
				a.seen = true;
				TickActor(a_actor, body, a, a_scene);
			};
			if (a_on) {
				auto* player = RE::PlayerCharacter::GetSingleton();
				visit(player);
				if (auto* lists = RE::ProcessLists::GetSingleton()) {
					for (auto& handle : lists->highActorHandles) {
						if (auto actor = handle.get(); actor && actor.get() != player) {
							visit(actor.get());
						}
					}
				}
			}
			std::erase_if(gHands, [&](auto& a_entry) {
				auto& a = a_entry.second;
				if (!a.seen) {
					DropHand(a.h[0], a_scene);
					DropHand(a.h[1], a_scene);
				}
				return !a.seen && !a.h[0].light && !a.h[1].light;
			});
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
			const bool           on = Wanted() && ColoredLightsOn();
			std::vector<Showing> live;
			std::scoped_lock     l{ gLock };
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
					auto [it, fresh] = gDomeOf.try_emplace(m->artObject, nullptr, kRows);
					if (fresh) {
						const auto* model = m->artObject->GetModel();
						if (const auto spec = gSpecs.find(ModelKey(model ? model : "")); spec != gSpecs.end()) {
							it->second = { &spec->second, RowOfModel(model) };
						}
					}
					if (const auto [spec, row] = it->second; spec && row < kRows) {
						live.push_back({ m, m->artObject3D, m->target, row, spec });
					}
				}
			}
			std::erase_if(gLit, [&](Lit& a_lit) {
				const bool keep = a_lit.light && a_lit.light->parent && std::ranges::any_of(live, [&](const Showing& a_s) {
					return a_s.effect == a_lit.effect && a_s.art.get() == a_lit.art.get();
				});
				if (!keep) {
					Drop(a_lit.light.get(), a_lit.bs, scene);
				}
				return !keep;
			});
			for (const auto& s : live) {
				if (std::ranges::none_of(gLit, [&](const Lit& a_lit) { return a_lit.effect == s.effect; })) {
					Hang(s, scene);
				}
			}
			for (auto& lit : gLit) {
				const auto  ref = lit.target.get();
				const auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
				const bool  hide = actor && actor->IsSneaking();
				if (lit.light->GetAppCulled() != hide) {
					lit.light->SetAppCulled(hide);
				}
				if (const auto c = RowColor(lit.row)) {
					DressDome(lit.light.get(), *c, *lit.spec);
				}
			}
			TickHands(scene, WardLightOn());
		}
	}

	void StartDomeLights()
	{
		{
			std::scoped_lock l{ gLock };
			ReadSpecs();
		}
		SKSE::log::info("dome lights: {} dome model(s) in {}{}; {}; first-person hand light {}", gSpecs.size(), kPath,
			gBadLines ? std::format(" ({} line(s) left out: a number that does not read)", gBadLines) : std::string(),
			Wanted() ? "hung by this plugin" : MeshLights() ? "in the ward meshes (ENB)" :
															  "off",
			MeshLights() ? "on (ENB)" : "on");
		// detached, never joined: a join from a DLL's static destructor at exit can hang on the loader lock. Nothing thrown in it
		// may leave the thread (that ends the game): each round is guarded, and so is the tick on the main thread
		std::thread([]() {
			for (;;) {
				std::this_thread::sleep_for(kTick);
				Guarded("dome lights thread", []() {
					// every round: the hand lights are on every pick
					if (!gQueued.exchange(true) && !Later("dome and hand lights", []() { Tick(); })) {
						gQueued = false;  // nothing queued: the next round tries again
					}
				});
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
		const auto*      player = RE::PlayerCharacter::GetSingleton();
		const auto       it = player ? gHands.find(player->GetFormID()) : gHands.end();
		const bool       leftLit = it != gHands.end() && it->second.h[0].light, rightLit = it != gHands.end() && it->second.h[1].light;
		std::size_t      lights = 0;
		for (const auto& [id, a] : gHands) {
			lights += (a.h[0].light ? 1 : 0) + (a.h[1].light ? 1 : 0);
		}
		return std::format(R"({{"models":{},"mode":{},"active":{},"lit":{},"hand1st":[{},{}],"handLights":{},"hand1stMade":{},"ambient":[{:.3f},{:.3f},{:.3f}]}})",
			gSpecs.size(), static_cast<int>(gMode.load()), Wanted(), gLit.size(), leftLit, rightLit, lights, gHandsMade,
			gLastAmbient.red, gLastAmbient.green, gLastAmbient.blue);
	}
}
