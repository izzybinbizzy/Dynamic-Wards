// Dynamic Wards - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// The colored light on a ward's dome, and a light on each hand casting or readying one of our wards, first person included -
// on every lighting pick. The player's hand lights hang under the THIRD-person body in first person too (TickHands has the
// measurement): under the first-person skeleton a light lit nothing on ENB. (The name DWHand1st is kept - reports read it.)
// Each light takes its row's color and its slider at each round, so a change in the menu reaches a ward already up.
// Where a dome light sits, how far it reaches and how strong it is are read from `Dome Lights.txt`, which the build writes.
//
// Nothing here runs on a clock. The game's own moments queue one main-thread round (an art effect of ours starting or ending, an
// actor's draw, sheathe, cast, equip, load or death, a setting), and the round reads only what its event names. A queued task
// also runs while a menu pauses the game and while DevBench's camera is free, when the wards' colors are set live and filmed.
//
// CREDIT: a light is made and registered the way ReLight by Truman does it (github.com/TrumanGIT/ReLight,
// GPL-3.0-or-later, used with his permission): a master NiPointLight cloned for every use, the size in the radius'
// z, and the light handed to the shadow scene node.

#include "Plugin.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <numbers>

namespace Plugin
{
	namespace
	{
		constexpr const char* kPath = "Data/SKSE/Plugins/Dynamic Wards/Dome Lights.txt";
		constexpr const char* kLightName = "DWDome";
		constexpr const char* kHandName = "DWHand1st";
		constexpr float       kAmbientRatio = 0.1f;                                                    // RE::Light's default ambientRatio (Truman's config.h)
		constexpr const char* kMagicNodes[] = { "NPC L MagicNode [LMag]", "NPC R MagicNode [RMag]" };  // Actor::SlotTypes order

		// a light's numbers: its reach under inverse square, and in the game's own lighting (plain); the house light by default
		struct Light
		{
			float fade{ 1.0f }, reach{ 133.0f }, size{ 2.0f };
			bool  plain{ false };
			float plainFade{ 1.0f }, plainRadius{ 178.0f };
		};
		constexpr Light kHand{ 1.0f, 133.0f, 2.0f, true, 1.14f, 178.0f };  // the hand light: the house reach (LTBG section 4)

		// a dome's light (Dome Lights.txt) and where its points sit
		struct Spec : Light
		{
			std::vector<RE::NiPoint3> points;
		};

		struct Lit
		{
			const RE::ModelReferenceEffect* effect{ nullptr };
			RE::NiPointer<RE::NiNode>       parent;  // held, so a body the game lets go of never leaves the light a dangling parent
			RE::NiPointer<RE::NiPointLight> light;
			RE::NiPointer<RE::BSLight>      bs;
			RE::ObjectRefHandle             target;
			std::size_t                     row{ 0 };
			const Spec*                     spec{ nullptr };
			bool                            shadow{ false };      // hung as an engine shadow light (the domeShadow test switch)
			bool                            shadowable{ false };  // the player's dome, its first point: the light the switch decides
			std::size_t                     point{ 0 };           // which of the spec's points
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
		std::atomic_bool                           gShadowTest{ false };  // devbench's domeShadow: the dome lights cast shadows (a trial)
		RE::NiColor                                gLastAmbient{};
		bool                                       gDomeWaiting = false;    // a dome that shows has a point not hung yet (the round only, under gLock)
		std::atomic_bool                           gBodiesMatter{ false };  // a light of ours up, or a round reading bodies: a body let go with none ends nothing
		std::size_t                                gBadLines = 0;           // Dome Lights.txt lines left out (a number that did not read)
		// each dome art object's light and row, found once (ModelKey made a string per effect per tick)
		std::unordered_map<const RE::BGSArtObject*, std::pair<const Spec*, std::size_t>> gDomeOf;
		std::unordered_map<const RE::BGSArtObject*, std::size_t>                         gRowOfArt;  // the same for a casting art's row (every hand, every tick)

		// what the next round reads, noted on any thread by the game's events and hooks; one round is queued at a time
		std::mutex                                   gNoteLock;
		std::vector<RE::FormID>                      gNotedHands;  // gNoteLock: actors whose hands may have changed
		std::vector<const RE::ModelReferenceEffect*> gGone;        // gNoteLock: art effects of ours that ended - names only, never read
		std::atomic_bool                             gAllHands{ false };
		std::atomic_bool                             gReadDomes{ false };
		std::atomic_bool                             gRoundQueued{ false };
		std::atomic_bool                             gHangAgain{ false };
		std::atomic_bool                             gAnyLit{ false };
		std::atomic<std::uint64_t>                   gTicks{ 0 };  // rounds run since data load: none while nothing happens

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

		// a dome's light reaches this many times the house light on every pick: the house reach (178 plain, 133 under inverse
		// square) barely passes the dome itself (~83) and stayed inside the hand lights' own pool
		constexpr float kDomeReach = 2.25f;

		// a dome's light by its model; the ENB mesh set takes the main set's light (the table names the main set only)
		const Spec* SpecOf(const std::string& a_key)
		{
			const auto it = gSpecs.find(MainSetKey(a_key));
			return it != gSpecs.end() ? &it->second : nullptr;
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
		void Dress(RE::NiPointLight* a_light, Color a_color, const Light& a_l, float a_plainRadius, float a_dim)
		{
			const bool isl = InverseSquare();
			auto&      data = a_light->GetLightRuntimeData();
			if (!isl && a_l.plain) {
				data.diffuse = LightColor(a_color, false);  // the game's own lighting (Vanilla, an ENB): an sRGB color, drawn as it is
				data.fade = a_l.plainFade * a_dim;
				data.radius = { a_plainRadius, a_plainRadius, a_plainRadius };
			} else {
				data.diffuse = LightColor(a_color, isl);
				data.fade = a_l.fade * a_dim;
				data.radius = { a_l.reach, a_l.reach, a_l.size };
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
			const std::array<std::uint32_t, 3> words{ kIslFlag | kIslInitialised, std::bit_cast<std::uint32_t>(CutoffFor(data.fade, a_l.reach, a_l.size)), 0u };
			std::memcpy(reinterpret_cast<std::byte*>(&data), words.data(), sizeof(words));
		}

		// A new light under a_parent, registered with the scene; a_dress sets its color and strength before the scene first sees
		// it. nullptr when the game could not make it or the scene refused it (then it is taken off again). Main thread.
		template <class F>
		RE::NiPointLight* Make(RE::NiNode* a_parent, const char* a_name, const RE::NiPoint3& a_at, RE::ShadowSceneNode* a_scene,
			RE::NiPointer<RE::BSLight>& a_bs, bool a_shadow, F&& a_dress)
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
			params.shadowLight = a_shadow;
			params.portalStrict = true;
			params.affectLand = true;
			params.affectWater = true;
			params.neverFades = true;
			params.fov = a_shadow ? 2.0f * std::numbers::pi_v<float> : 90.0f;  // a shadow light's field of view, radians: omni
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

		void DressDome(RE::NiPointLight* a_light, Color a_color, const Spec& a_spec, float a_dim)
		{
			const Light wide{ a_spec.fade, a_spec.reach * kDomeReach, a_spec.size, a_spec.plain, a_spec.plainFade, a_spec.plainRadius * kDomeReach };
			Dress(a_light, a_color, wide, wide.plainRadius, a_dim);
		}

		void DressHand(RE::NiPointLight* a_light, Color a_color)
		{
			Dress(a_light, a_color, kHand, kHand.plainRadius, HandDim());
		}

		// the node a dome's light hangs under: the wearer's third-person body (in both views), else the dome's art - under the art
		// node its point stood ~190 above the feet, out of the ground's reach
		RE::NiNode* LightParent(RE::NiAVObject* a_art, const RE::ObjectRefHandle& a_target)
		{
			const auto ref = a_target.get();
			auto*      actor = ref ? ref->As<RE::Actor>() : nullptr;
			auto*      body = actor ? actor->Get3D(false) : nullptr;
			if (auto* node = body ? body->AsNode() : nullptr) {
				return node;
			}
			return a_art ? a_art->AsNode() : nullptr;
		}

		// a dome that shows now: its effect only names it (never read - it may be gone once the effects' lock is let go), the node
		// its light hangs under and its wearer are held while the lock is
		struct Showing
		{
			const RE::ModelReferenceEffect* effect{ nullptr };
			RE::NiPointer<RE::NiNode>       parent;
			RE::ObjectRefHandle             target;
			std::size_t                     row{ kRows };
			const Spec*                     spec{ nullptr };
		};

		// the dome's points not hung yet; the ones the scene refused wait for the next round
		void Hang(const Showing& a_s, RE::ShadowSceneNode* a_scene, bool a_shadowOn)
		{
			auto*      parent = a_s.parent.get();
			const auto color = SlotColor(a_s.row);
			if (!parent || !color) {
				return;
			}
			// the trial: one shadow light per dome, and only the player's (each is an omni shadow map the renderer has 3-4 of a frame)
			const auto* wearer = a_s.target.get().get();
			const bool  playerDome = wearer && wearer == RE::PlayerCharacter::GetSingleton();
			for (std::size_t i = 0; i < a_s.spec->points.size(); ++i) {
				if (std::ranges::any_of(gLit, [&](const Lit& a_lit) { return a_lit.effect == a_s.effect && a_lit.point == i; })) {
					continue;
				}
				const auto&                at = a_s.spec->points[i];
				RE::NiPointer<RE::BSLight> bs;
				bool                       shadowable = playerDome && i == 0;
				bool                       shadow = shadowable && a_shadowOn;
				const auto                 dress = [&](RE::NiPointLight* a_light) { DressDome(a_light, *color, *a_s.spec, LightDim()); };
				auto*                      light = Make(parent, kLightName, at, a_scene, bs, shadow, dress);
				if (!light && shadow) {
					// the renderer had no shadow map left: a plain light, and the switch leaves this one alone until the next cast
					shadow = shadowable = false;
					light = Make(parent, kLightName, at, a_scene, bs, false, dress);
				}
				if (!light) {
					return;
				}
				gLit.push_back({ a_s.effect, RE::NiPointer<RE::NiNode>(parent), RE::NiPointer<RE::NiPointLight>(light), std::move(bs), a_s.target, a_s.row,
					a_s.spec, shadow, shadowable, i });
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

		std::size_t RowOfArt(const RE::BGSArtObject* a_art)
		{
			if (!a_art) {
				return kRows;
			}
			auto [it, fresh] = gRowOfArt.try_emplace(a_art, kRows);
			if (fresh) {
				const auto* model = a_art->GetModel();
				it->second = model ? RowOfModel(model) : kRows;
			}
			return it->second;
		}

		// the row of the ward a hand casts, or kRows: while cast (charging, ready, held) the caster's spell; while only readied
		// (the casting art shows, nothing cast yet) the spell equipped in that hand, hands drawn
		// a STAFF drawn in that hand while an always-on ward of ours is up (Reliquary of Myth's Staff of Magnus raises its ward
		// as an ability while held): that ward's row (3.4, a user report: no light in first person). Asked after the spell
		// above found no ward, so firing the staff keeps the light; read once per actor and tick (a_alwaysOn)
		std::size_t CastingRow(RE::Actor* a_actor, std::size_t a_slot, std::optional<std::size_t>& a_alwaysOn, const RE::EffectSetting*& a_effect)
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
					if (const auto row = RowOfArt(art); row < kRows) {
						a_effect = e->baseEffect;
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
			Drop(a_h.light.get(), a_h.bs, a_scene);
			a_h = {};
		}

		// one actor's two hands: a light on each hand casting one of our wards, under that hand's magic node
		void TickActor(RE::Actor* a_actor, RE::NiAVObject* a_body, ActorHands& a_hands, RE::ShadowSceneNode* a_scene)
		{
			std::optional<std::size_t> alwaysOn;  // AlwaysOnWardRow, read at most once for both hands
			// another caster's hand light follows his look (Others.cpp TheirSlot), not the spell record's row: the player's row,
			// his look's color, or while he wears the game's own art the light the effect's own record carried (the apply took it
			// off the record for our hand light) - a drawn staff's always-on ward too, which names no effect: no light then
			const bool ours = a_body != nullptr;
			for (std::size_t slot = 0; slot < 2; ++slot) {
				auto&                    h = a_hands.h[slot];
				const RE::EffectSetting* effect = nullptr;
				const auto               row = ours ? CastingRow(a_actor, slot, alwaysOn, effect) : kRows;
				const auto               use = row < kRows ? TheirSlot(a_actor, row) : kSlots;
				const auto               color = use < kSlots ? SlotColor(use) : row < kRows ? GameLightColor(effect) :
				                                                                               std::nullopt;
				auto*                    node = color ? a_body->GetObjectByName(MagicNode(slot)) : nullptr;
				auto*                    parent = node ? node->AsNode() : nullptr;
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
					if (auto* light = Make(parent, kHandName, {}, a_scene, bs, false, [&](RE::NiPointLight* a_light) { DressHand(a_light, *color); })) {
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

		// the hands of the one actor an event named; one gone or with no 3D loses his lights
		void HandsOf(RE::FormID a_id, RE::ShadowSceneNode* a_scene, bool a_on)
		{
			auto*      actor = a_on ? RE::TESForm::LookupByID<RE::Actor>(a_id) : nullptr;
			const auto it = gHands.find(a_id);
			ActorHands none;
			auto&      hands = it != gHands.end() ? it->second : none;
			if (actor) {
				TickActor(actor, actor->Get3D(false), hands, a_scene);
			} else {
				DropHand(hands.h[0], a_scene);
				DropHand(hands.h[1], a_scene);
			}
			const bool lit = hands.h[0].light || hands.h[1].light;
			if (it == gHands.end() && lit) {
				gHands.emplace(a_id, std::move(none));
			} else if (it != gHands.end() && !lit) {
				gHands.erase(it);
			}
		}

		// every dome that shows, off the game's list: a light on each, the lights of those gone dropped, dark while the wearer sneaks. The light
		// hangs on the wearer's body, so it goes up as the effect starts, whether or not its model has loaded yet
		void ReadDomes(RE::ShadowSceneNode* a_scene, RE::ProcessLists* a_lists, std::span<const RE::ModelReferenceEffect* const> a_gone)
		{
			const bool           on = Wanted() && ColoredLightsOn();
			std::vector<Showing> live;
			if (on) {
				RE::BSSpinLockGuard guard{ a_lists->magicEffectsLock };
				for (auto& temp : a_lists->magicEffects) {
					auto* e = temp.get();
					if (!e || e->GetType() != RE::TEMP_EFFECT_TYPE::kRefModel) {
						continue;
					}
					const auto* m = static_cast<RE::ModelReferenceEffect*>(e);
					if (m->finished || !m->artObject || std::ranges::find(a_gone, m) != a_gone.end()) {
						continue;
					}
					auto [it, fresh] = gDomeOf.try_emplace(m->artObject, nullptr, kSlots);
					if (fresh) {
						const auto* model = m->artObject->GetModel();
						if (const auto* spec = SpecOf(ModelKey(model ? model : ""))) {
							it->second = { spec, RowOfModel(model) };
						}
					}
					if (const auto [spec, row] = it->second; spec && row < kSlots) {
						live.push_back({ m, RE::NiPointer<RE::NiNode>(LightParent(m->artObject3D.get(), m->target)), m->target, row, spec });
					}
				}
			}
			// a dome whose light no longer matches the shadow switch is hung again whole, in both directions
			const bool                                   shadowOn = gShadowTest.load();
			std::vector<const RE::ModelReferenceEffect*> flip;
			for (const auto& lit : gLit) {
				if (lit.shadow != (lit.shadowable && shadowOn)) {
					flip.push_back(lit.effect);
				}
			}
			std::erase_if(gLit, [&](Lit& a_lit) {
				const bool keep = a_lit.light && a_lit.light->parent && std::ranges::find(flip, a_lit.effect) == flip.end() && std::ranges::any_of(live, [&](const Showing& a_s) {
					// a body the wearer swapped (a 3D reload) takes the light down, and this round hangs it on the new one
					return a_s.effect == a_lit.effect && a_s.row == a_lit.row && a_s.parent.get() == a_lit.parent.get();
				});
				if (!keep) {
					Drop(a_lit.light.get(), a_lit.bs, a_scene);
				}
				return !keep;
			});
			const auto allHung = [](const Showing& a_s) {
				return static_cast<std::size_t>(std::ranges::count_if(gLit, [&](const Lit& a_lit) { return a_lit.effect == a_s.effect; })) >= a_s.spec->points.size();
			};
			gDomeWaiting = false;
			for (const auto& s : live) {
				// a dome the scene gave only part of its lights keeps them: the missing points are tried again at the next round
				if (!allHung(s)) {
					Hang(s, a_scene, shadowOn);
					if (!allHung(s)) {
						gDomeWaiting = true;
					}
				}
			}
			// read once a round, not once a light
			const float                              dim = LightDim();
			std::array<std::optional<Color>, kSlots> colors{};
			std::array<bool, kSlots>                 asked{};
			for (auto& lit : gLit) {
				const auto  ref = lit.target.get();
				const auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
				const bool  hide = actor && actor->IsSneaking();
				if (lit.light->GetAppCulled() != hide) {
					lit.light->SetAppCulled(hide);
				}
				if (!asked[lit.row]) {
					colors[lit.row] = SlotColor(lit.row);
					asked[lit.row] = true;
				}
				if (const auto& c = colors[lit.row]) {
					DressDome(lit.light.get(), *c, *lit.spec, dim);
				}
			}
			gAnyLit = !gLit.empty();
		}

		// a cell change takes every light out of the scene's lists and leaves ours hung under the body, lighting nothing: only a new one reaches the scene
		void DropAll(RE::ShadowSceneNode* a_scene)
		{
			for (auto& lit : gLit) {
				Drop(lit.light.get(), lit.bs, a_scene);
			}
			gLit.clear();
			for (auto& [id, a] : gHands) {
				DropHand(a.h[0], a_scene);
				DropHand(a.h[1], a_scene);
			}
			gHands.clear();
		}

		// one round on the main thread: what the events since the last one named, then the copies and palettes nothing holds any more (Colors.cpp
		// ReleaseOld - given back here, outside the game's effect updates)
		void Tick()
		{
			const FrameCost::Scope cost{ 0, "dome and hand lights (each round, on an event)" };
			++gTicks;
			auto* scene = RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
			auto* lists = RE::ProcessLists::GetSingleton();
			if (!scene || !lists) {
				// no scene yet (a load, the main menu): what was asked waits for the next round
				ReleaseOld();
				return;
			}
			const bool                                   domes = gReadDomes.exchange(false);
			const bool                                   all = gAllHands.exchange(false);
			const bool                                   again = gHangAgain.exchange(false);
			std::vector<RE::FormID>                      hands;
			std::vector<const RE::ModelReferenceEffect*> gone;
			{
				std::scoped_lock l{ gNoteLock };
				hands.swap(gNotedHands);
				gone.swap(gGone);
			}
			{
				std::scoped_lock l{ gLock };
				// before any body is read: a body let go after this is named, one let go before it is seen gone
				gBodiesMatter = true;
				if (again) {
					DropAll(scene);
				}
				// an event of an actor may be a wearer's 3D arriving or his sneaking changing: only with a dome of ours up or waiting. A new one says so itself
				if (domes || (!hands.empty() && (!gLit.empty() || gDomeWaiting))) {
					ReadDomes(scene, lists, gone);
				}
				const bool on = WardLightOn();
				if (all) {
					TickHands(scene, on);
				} else {
					for (const auto id : hands) {
						HandsOf(id, scene, on);
					}
				}
				gBodiesMatter = !gHands.empty() || !gLit.empty();
			}
			ReleaseOld();
		}

		void QueueRound() noexcept
		{
			(void)LaterOnce(gRoundQueued, "dome and hand lights", []() { Tick(); });
		}
	}

	void WakeLights() noexcept
	{
		gReadDomes = true;
		gAllHands = true;
		QueueRound();
	}

	void HangLightsAgain() noexcept
	{
		gHangAgain = true;
		WakeLights();
	}

	bool DomeLit() noexcept
	{
		return gAnyLit;
	}

	void NoteDome() noexcept
	{
		gReadDomes = true;
		ArcBarStale();  // a ward replaced by one giving the same Ward Power: the bar reads which is up
		QueueRound();
	}

	// a new effect may sit at the address of one that just ended: it is not that one
	void NoteDomeStart(const RE::ModelReferenceEffect* a_effect) noexcept
	{
		try {
			std::scoped_lock l{ gNoteLock };
			std::erase(gGone, a_effect);
		} catch (...) {
			// a lock the system could not give: his next event reads the domes again
		}
		NoteDome();
	}

	void NoteDomeGone(const RE::ModelReferenceEffect* a_effect) noexcept
	{
		try {
			std::scoped_lock l{ gNoteLock };
			gGone.push_back(a_effect);
		} catch (...) {
			// a lock or memory the system could not give: its finished flag tells the round
		}
		NoteDome();
	}

	void NoteHands(RE::FormID a_actor) noexcept
	{
		try {
			std::scoped_lock l{ gNoteLock };
			if (std::ranges::find(gNotedHands, a_actor) == gNotedHands.end()) {
				gNotedHands.push_back(a_actor);
			}
		} catch (...) {
			// a lock or memory the system could not give: his next event names him again
		}
		QueueRound();
	}

	namespace
	{
		// An actor's 3D let go or set, at his own slot (Light Placer's way of hooking a reference's 3D, powerof3, GPL-3.0, with his permission). The
		// game's load event names only a reference with a script or in an alias: for anyone else nothing says he left, and his lights would
		// stay in the scene where he stood. Every actor passes here, so with nothing lit a body going stops at one read. Any thread: only noted
		template <class T>
		struct BodySet
		{
			static void thunk(T* a_this, RE::NiAVObject* a_object, bool a_queue3DTasks)
			{
				func(a_this, a_object, a_queue3DTasks);
				if (a_object || gBodiesMatter) {
					NoteHands(a_this->GetFormID());
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
			static void                                    Install()
			{
				REL::Relocation<std::uintptr_t> vtbl{ T::VTABLE[0] };
				func = vtbl.write_vfunc(0x6C, thunk);
			}
		};
	}

	void InstallBodyWatch()
	{
		BodySet<RE::Character>::Install();
		BodySet<RE::PlayerCharacter>::Install();
	}

	void StartDomeLights()
	{
		{
			std::scoped_lock l{ gLock };
			ReadSpecs();
		}
		SKSE::log::info("dome lights: {} dome model(s) in {}{}; dome and hand lights hung by this plugin", gSpecs.size(), kPath,
			gBadLines ? std::format(" ({} line(s) left out: a number that does not read)", gBadLines) : std::string());
		WakeLights();
	}

	void SetDomeMode(DomeMode a_mode)
	{
		gMode = a_mode;
		WakeLights();
	}

	void SetDomeShadowTest(bool a_on)
	{
		gShadowTest = a_on;
		WakeLights();
	}

	nlohmann::json DomeLightsReport()
	{
		std::scoped_lock l{ gLock };
		const auto*      player = RE::PlayerCharacter::GetSingleton();
		const auto       it = player ? gHands.find(player->GetFormID()) : gHands.end();
		const bool       leftLit = it != gHands.end() && it->second.h[0].light, rightLit = it != gHands.end() && it->second.h[1].light;
		std::size_t      lights = 0;
		for (const auto& [id, a] : gHands) {
			lights += (a.h[0].light ? 1 : 0) + (a.h[1].light ? 1 : 0);
		}
		return { { "models", gSpecs.size() }, { "mode", static_cast<int>(gMode.load()) }, { "active", Wanted() }, { "lit", gLit.size() },
			{ "hand1st", nlohmann::json::array({ leftLit, rightLit }) }, { "handLights", lights }, { "hand1stMade", gHandsMade }, { "shadow", gShadowTest.load() },
			{ "asleep", !gRoundQueued.load() }, { "ticks", gTicks.load() }, { "ambient", nlohmann::json::array({ gLastAmbient.red, gLastAmbient.green, gLastAmbient.blue }) } };
	}
}
