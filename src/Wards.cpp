// Dynamic Wards - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.

#include "Plugin.h"

namespace Plugin
{
	namespace
	{
		constexpr std::string_view kWardBody = "wardbodyfx";
		constexpr std::string_view kWardHand = "wardinhandfx";
		constexpr const char*      kEmptyModel = "Effects\\FXEmptyObject.nif";

		// 1.0's rows (wardgen.RUNTIME_TARGETS): hit and casting art, always
		struct TableRow
		{
			const char*   token;
			const char*   file;
			std::uint32_t id;
		};
		constexpr TableRow kTable[] = {
			{ "Novice", "Skyrim.esm", 0x00014C },
			{ "Apprentice", "Skyrim.esm", 0x05AD60 },
			{ "Adept", "Skyrim.esm", 0x05AD61 },
			{ "Spellbreaker", "Skyrim.esm", 0x07DCDB },
			{ "Novice", "MysticismMagic.esp", 0x78E9EA },
			{ "Novice", "MysticismMagic.esp", 0xD3BD22 },
			{ "Apprentice", "MysticismMagic.esp", 0x78E9ED },
			{ "Apprentice", "MysticismMagic.esp", 0xD40E23 },
			{ "Adept", "MysticismMagic.esp", 0x78E9EF },
			{ "Adept", "MysticismMagic.esp", 0xD40E24 },
			{ "Expert", "MysticismMagic.esp", 0x283347 },
			{ "Expert", "MysticismMagic.esp", 0x78E9F1 },
			{ "Expert", "MysticismMagic.esp", 0xD40E25 },
			{ "Master", "MysticismMagic.esp", 0x3DB99B },
			{ "Master", "MysticismMagic.esp", 0x78E9F3 },
			{ "Master", "MysticismMagic.esp", 0xD40E26 },
			{ "Expert", "Odin - Skyrim Magic Overhaul.esp", 0x134746 },
			{ "Adept", "ccbgssse045-hasedoki.esl", 0x000863 },
			{ "Crusader", "ccmtysse001-knightsofthenine.esl", 0x00081D },
			{ "Reman", "LegacyoftheDragonborn.esm", 0x124E5B },
			{ "Spellbreaker", "Artificer.esp", 0x00094C },
			{ "Spellbreaker", "Artificer.esp", 0x00094D },
			// Reliquary of Myth's Spellbreaker (its own ability ward, 100 Ward Power, the game's ward art) - as Artificer's (3.4)
			{ "Spellbreaker", "Reliquary of Myth.esp", 0x051867 },
			{ "Vampire", "Better Vampires.esp", 0x47179D },
			{ "Vampire", "Better Vampires.esp", 0x4717A0 },
			{ "Vampire", "Better Vampire NPCs.esp", 0x074F60 },
			{ "Vampire", "Better Vampire NPCs.esp", 0x074F63 },
			{ "Vampire", "Better Vampire NPCs.esp", 0x074F64 },
			{ "Vampire", "Better Vampire NPCs.esp", 0x074F67 },
		};
		// ShieldConcSelf: the second effect of every vanilla ward spell; silent, or each ward would show two domes
		constexpr TableRow kSilent = { "silent", "Skyrim.esm", 0x0FCC62 };

		constexpr std::uint32_t kCrusaderShields[] = { 0x1662A6, 0x77AE52 };
		constexpr const char*   kLegacy = "LegacyoftheDragonborn.esm";
		constexpr const char*   kCrusaderHub = "DBM_CC_DivineCrusaderHOWPatch.esp";
		constexpr const char*   kKnights = "ccmtysse001-knightsofthenine.esl";
		constexpr std::uint32_t kCrusaderEnch = 0x000822;

		constexpr const char*   k360Plugin = "360 Ward.esp";
		constexpr const char*   k360Patch = "360WardUniversalPatchSKSE.dll";  // 360 Ward Universal Patch SKSE: the 360 dome needs both
		constexpr std::uint32_t k360Flash = 0x000803;                         // WardShieldHit

		constexpr const char*   kLookDir = "magic\\Dynamic Wards\\";
		constexpr const char*   kLightsGlobal = "DynamicWardsLights";    // the dome lights (Colored ward lights)
		constexpr const char*   kHandGlobal = "DynamicWardsHandLight";   // the hand light (Ward casting light)
		constexpr const char*   kPresentGlobal = "DynamicWardsPresent";  // always 1: the light mods' ward lights step down
		constexpr std::uint32_t kArtChanged = 'DWAC';                    // to RELight - Spell Addon: casting art moved, find the hand lights again
		// the two applies made in the background (WatchArt's round), named once: ApplyAll keeps the hands' look for the menu
		constexpr const char* kWhyArtSwapped = "art swapped by another mod";
		constexpr const char* kWhyUnlock = "the 360 unlock changed";

		struct Art
		{
			RE::BGSArtObject*       hand = nullptr;
			RE::BGSArtObject*       dome = nullptr;  // the vanilla-shaped dome; also the 360 dome while it is locked
			RE::BGSArtObject*       dome360 = nullptr;
			RE::BGSReferenceEffect* flash = nullptr;
			// a rank row's looping copies (the build's `worn` meshes, on Magic\BeginLoopEnd.hkx) for an always-on ward: the cast
			// meshes' graph waits for a cast an item's constant effect never makes, so only the idle centre showed and the outer
			// layers were missing - the shields' old bug (his report on the Staff of Magnus, 3.4). The shield rows' own meshes
			// are already looping copies.
			RE::BGSArtObject* wornHand = nullptr;
			RE::BGSArtObject* wornDome = nullptr;
		};

		enum class How
		{
			kTable,
			kFound,  // only the slots that held a ward's art are replaced
			kSilent,
		};

		struct Target
		{
			RE::EffectSetting* effect{ nullptr };
			std::size_t        row{ 0 };
			How                how{};
			bool               castWard{ false }, hitWard{ false }, enchWard{ false };
			RE::BGSArtObject*  ownCast{ nullptr };
			RE::BGSArtObject*  ownHit{ nullptr };
			RE::BGSArtObject*  ownEnch{ nullptr };
			RE::TESObjectLIGH* ownLight{ nullptr };
			RE::TESObjectLIGH* takenLight = nullptr;  // what was there when the switch took it off (another mod's light survives)
			std::string        why;
			std::string        plugin;            // the file that adds the effect: its line on the Compatibility page
			bool               constant = false;  // a constant effect (an item's ward while held or worn): never cast, so it wears the worn copies
		};

		struct Changes
		{
			std::size_t art = 0, light = 0, shield = 0;
		};

		std::mutex              gLock;
		std::array<Art, kSlots> gArt{};  // 3.0: each row's ONE neutral set; Colors.cpp gives it the row's color
		RE::TESGlobal*          gLightsGlobal = nullptr;
		RE::TESGlobal*          gHandGlobal = nullptr;
		RE::TESGlobal*          gPresentGlobal = nullptr;
		bool                    gLightPlacer = false;
		RE::BGSArtObject*       gEmpty = nullptr;
		RE::BGSReferenceEffect* gNoFlash = nullptr;
		std::vector<Target>     gTargets;
		std::size_t             gSkipped = 0;
		std::atomic_bool        gHas360{ false };       // 360 Ward.esp loaded AND its Universal Patch
		bool                    gHas360Plugin = false;  // 360 Ward.esp alone (the menu says what is missing)
		// our 360 domes are installed (any row's): the installer puts them in only when 360 Ward.esp was in the plugin list, so a
		// player who adds 360 Ward later has the plugin and the patch but no 360 dome to wear - the outside review's "360 menu choice
		// shown without the 360 meshes" (3.5: the menu then says how to get them, and never offers a choice that changes nothing)
		std::atomic_bool                                                 gHas360Meshes{ false };
		bool                                                             gUnlocked = true;
		std::size_t                                                      gDressed = 0;
		std::vector<std::pair<RE::TESObjectARMO*, RE::EnchantmentItem*>> gShields;
		RE::EnchantmentItem*                                             gCrusaderEnch = nullptr;
		std::string                                                      gLastApply = "never";
		std::string                                                      gFlashTemplate = "none";
		std::array<const Art*, kRows>                                    gRowArt{};  // what each row wears now, resolved once per apply
		std::unordered_map<const RE::EffectSetting*, std::size_t>        gTargetOf;  // effect -> gTargets index
		std::unordered_set<const RE::BGSArtObject*>                      gOurs;      // every art object made here: a slot holding one was dressed by us

		// His order 2026-10-08: each shield ward has its own switch, and off means that shield raises no ward at all. A worn
		// shield raises its ward through its enchantment (Spellbreaker, the Crusader shields) or through an ability its equip
		// script adds (Reman's shield). While a switch is off, each such entry points at an inert copy of the ward effect (no
		// art, no light, no actor value, hidden), so the enchantment or ability keeps its length and a save's active effects
		// still find their entry. The ward effect itself is left as it is: the Creation Club Warlock ring wears Spellbreaker's
		// effect through its own enchantment and keeps its ward.
		struct ShieldEntry
		{
			RE::Effect*        entry;
			RE::EffectSetting* ward;
			RE::EffectSetting* inert;
			std::size_t        row;
			RE::MagicItem*     carrier;
			bool               ability;  // a spell the shield's script adds; else the shield's enchantment
		};
		std::vector<ShieldEntry> gShieldEntries;
		bool                     gSharedEnchant = false;  // a shield's enchantment record is worn by something that is not a shield

		RE::EffectSetting* InertCopy(const RE::EffectSetting* a_ward)
		{
			auto* e = NewForm<RE::EffectSetting>();
			if (!e) {
				return nullptr;
			}
			using Flag = RE::EffectSetting::EffectSettingData::Flag;
			e->data.archetype = RE::EffectSetting::Archetype::kScript;  // a script effect with no script does nothing
			e->data.castingType = a_ward->data.castingType;             // the carrier's own cast and delivery, so it is accepted
			e->data.delivery = a_ward->data.delivery;
			e->data.primaryAV = RE::ActorValue::kNone;
			e->data.secondaryAV = RE::ActorValue::kNone;
			e->data.flags.set(Flag::kHideInUI, Flag::kNoDuration, Flag::kNoMagnitude, Flag::kNoArea, Flag::kPainless, Flag::kNoHitEvent,
				Flag::kNoHitEffect);
			return e;
		}

		// the 360 dome can be worn at all: 360 Ward, its Universal Patch and our 360 domes. gLock held
		bool Can360()
		{
			return gHas360 && gHas360Meshes;
		}

		// The dome a row wears now: the 360 dome where the player wants it (the Dome pick for the spell wards, each shield's own
		// pick for the shield rows - his yes 2026-10-12 after a user report: Spellbreaker kept the 360 sphere while he wanted it
		// only on his spell wards) and 360 Ward + its Universal Patch are loaded and the 360 dome is unlocked; else the normal
		// dome. Either is our own art, so the Universal Patch never plays its pulse over it. gLock held.
		RE::BGSArtObject* DomeOf(std::size_t a_row, const Art* a_art)
		{
			const bool shield = std::ranges::find(kShieldRows, a_row) != kShieldRows.end();
			const bool want360 = shield ? ShieldDomeOf(a_row) == ShieldDome::k360 : Dome360();
			return (Can360() && want360 && gUnlocked && a_art->dome360) ? a_art->dome360 : a_art->dome;
		}

		// what one ward wears: with a_worn (WearsWorn) its row's looping copies where the row has them (the 360 dome loops
		// already)
		RE::BGSArtObject* HandOf(bool a_worn, const Art* a_art)
		{
			return a_worn && a_art->wornHand ? a_art->wornHand : a_art->hand;
		}

		// DomeOf, with a_worn its looping copy in place of the normal dome where the row has one
		RE::BGSArtObject* WornDomeOf(std::size_t a_row, bool a_worn, const Art* a_art)
		{
			auto* dome = DomeOf(a_row, a_art);
			return dome == a_art->dome && a_worn && a_art->wornDome ? a_art->wornDome : dome;
		}

		// an always-on ward of a rank row that wears ward art (one found by Ward Power alone is dressed with nothing): the
		// one test for wearing a row's worn copies (AlwaysOnWardRow's too)
		bool WearsWorn(const Target& a_t)
		{
			return a_t.constant && a_t.row < kRanks && (a_t.how == How::kTable || a_t.castWard || a_t.hitWard || a_t.enchWard);
		}

		// the art a dressed ward wears in each of its three slots (nullptr: that slot is not ours to dress) - ApplyAll puts it
		// on and WatchArt checks it, so the two never disagree
		struct Dress
		{
			RE::BGSArtObject* cast = nullptr;
			RE::BGSArtObject* hit = nullptr;
			RE::BGSArtObject* ench = nullptr;
		};

		Dress DressOf(const Target& a_t, const Art* a_art)
		{
			const bool  worn = WearsWorn(a_t);
			auto* const dome = WornDomeOf(a_t.row, worn, a_art);
			const bool  table = a_t.how == How::kTable;
			Dress       d;
			if (table || a_t.castWard) {
				d.cast = HandOf(worn, a_art);
			}
			if (table || a_t.hitWard) {
				d.hit = dome;
			}
			// the enchantment slot: where the hit slot already carries ours it keeps what it wore before 3.4 (the cast-graph dome,
			// or the 360 dome on that pick), so the worn copy never adds a second full dome; a ward whose only dome is this slot
			// wears the hit slot's choice
			if (!table && a_t.enchWard) {
				d.ench = a_t.hitWard ? DomeOf(a_t.row, a_art) : dome;
			}
			return d;
		}

		// the ward a shield-row entry raises now: the ward, or its inert copy while that shield's switch is off
		RE::EffectSetting* ShieldWant(const ShieldEntry& a_s)
		{
			return ShieldWardOn(a_s.row) || !a_s.inert ? a_s.ward : a_s.inert;
		}

		// The live effects follow at once, on the player and every actor near her. They are taken off BEFORE the entries are
		// switched (run 1 of the in-game test, 2026-10-08: refreshed after the switch, Reman's ward ended on the inert copy and
		// its +20 Ward Power was never taken back - an effect must end on the base effect it started on): a shield's enchantment
		// effect is dispelled (off: the ward is gone; on: the inert one goes, and since 2026-10-10 the worn shield is given its
		// ward again at once - Apply), an ability is taken off and, once the entries are switched, given back (rebuilt from its
		// entries now).
		std::vector<RE::Actor*> ShieldActors()
		{
			std::vector<RE::Actor*> out;
			auto*                   player = RE::PlayerCharacter::GetSingleton();
			if (player) {
				out.push_back(player);
			}
			// every process level: an NPC in a lower one keeps an effect too, and one switched under it never gives its Ward Power
			// back
			if (auto* lists = RE::ProcessLists::GetSingleton()) {
				for (auto* handles : { &lists->highActorHandles, &lists->middleHighActorHandles, &lists->middleLowActorHandles, &lists->lowActorHandles }) {
					for (auto& handle : *handles) {
						if (auto actor = handle.get(); actor && actor.get() != player) {
							out.push_back(actor.get());
						}
					}
				}
			}
			return out;
		}

		struct ShieldRefresh
		{
			std::vector<std::pair<RE::Actor*, RE::SpellItem*>> giveBack;
			std::size_t                                        dispelled = 0;
		};

		ShieldRefresh TakeShieldEffects(const std::vector<const ShieldEntry*>& a_changing)
		{
			ShieldRefresh                         r;
			std::unordered_set<const RE::Effect*> entries;
			std::unordered_set<RE::SpellItem*>    abilities;
			for (const auto* s : a_changing) {
				if (!s->ability) {
					entries.insert(s->entry);
				} else if (auto* spell = skyrim_cast<RE::SpellItem*>(s->carrier)) {
					abilities.insert(spell);
				}
			}
			for (auto* actor : a_changing.empty() ? std::vector<RE::Actor*>{} : ShieldActors()) {
				std::vector<RE::ActiveEffect*> dispel;
				if (auto* list = entries.empty() ? nullptr : actor->AsMagicTarget()->GetActiveEffectList()) {
					for (auto* ae : *list) {
						if (ae && entries.contains(ae->effect)) {
							dispel.push_back(ae);
						}
					}
				}
				for (auto* ae : dispel) {
					// one dispel may end another: only one still in her list is dispelled (as RecheckShieldWards)
					auto* list = actor->AsMagicTarget()->GetActiveEffectList();
					if (list && std::find(list->begin(), list->end(), ae) != list->end() && ae->flags.none(RE::ActiveEffect::Flag::kDispelled)) {
						ae->Dispel(true);
						++r.dispelled;
					}
				}
				for (auto* spell : abilities) {
					if (actor->HasSpell(spell) && actor->RemoveSpell(spell)) {
						r.giveBack.emplace_back(actor, spell);
					}
				}
			}
			return r;
		}

		// loaded, not merely present: LookupModByName also finds a plugin that is installed but not enabled
		bool Loaded(RE::TESDataHandler* a_dh, std::string_view a_name)
		{
			return a_dh && (a_dh->LookupLoadedModByName(a_name) || a_dh->LookupLoadedLightModByName(a_name));
		}

		bool MeshExists(const std::string& a_rel)
		{
			RE::BSResourceNiBinaryStream s(std::string("meshes\\") + a_rel);
			return s.good();
		}

		RE::BGSArtObject* MakeArtObject(const std::string& a_model)
		{
			auto* art = NewForm<RE::BGSArtObject>();
			if (art) {
				art->SetModel(a_model.c_str());
				gOurs.insert(art);
			}
			return art;
		}

		std::string ModelOf(const RE::BGSArtObject* a_art)
		{
			return Lower(a_art && a_art->GetModel() ? a_art->GetModel() : "");
		}

		// an always-on ward ranked by its strength: a constant effect whose value is Ward Power (an item's ward while held or worn)
		bool AlwaysOnPower(const RE::EffectSetting& a_effect)
		{
			return a_effect.data.castingType == RE::MagicSystem::CastingType::kConstantEffect && a_effect.data.primaryAV == RE::ActorValue::kWardPower;
		}

		float StrongestWard(const RE::EffectSetting* a_effect, const std::unordered_map<const RE::EffectSetting*, float>& a_carriers)
		{
			const auto it = a_carriers.find(a_effect);
			return it == a_carriers.end() ? 0.0f : it->second;
		}

		bool Unlocked()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return true;
			}
			const auto rule = UnlockRule();
			if (rule == Unlock::kPerk) {
				auto* perk = ResolveForm(UnlockPerk());
				return perk && perk->Is(RE::FormType::Perk) && player->HasPerk(perk->As<RE::BGSPerk>());
			}
			// the skill level itself, as the game's own perk requirements read it: a skill raised or lowered by a modifier (a
			// mod's fortify-skill effect, modav) does not lock or unlock the dome
			const auto need = UnlockSkill(rule);
			return !need || player->AsActorValueOwner()->GetBaseActorValue(RE::ActorValue::kRestoration) >= *need;
		}

		template <class T>
		void Put(T*& a_slot, T* a_want, std::size_t& a_changed)
		{
			if (a_slot != a_want) {
				a_slot = a_want;
				++a_changed;
			}
		}

		// a ward we do not dress gets its own art back only where we put ours: art another mod set after load stays
		void Restore(RE::BGSArtObject*& a_slot, RE::BGSArtObject* a_own, std::size_t& a_changed)
		{
			if (a_slot != a_own && gOurs.contains(a_slot)) {
				a_slot = a_own;
				++a_changed;
			}
		}

		// off: take the light and remember it. on: give back what was taken, else the light it loaded with.
		void ApplyLight(Target& a_t, bool a_on, bool a_restoreOwn, std::size_t& a_changed)
		{
			auto& light = a_t.effect->data.light;
			if (!a_on) {
				if (light) {
					a_t.takenLight = light;
					light = nullptr;
					++a_changed;
				}
				return;
			}
			if (!light) {
				auto* back = a_t.takenLight ? a_t.takenLight : (a_restoreOwn ? a_t.ownLight : nullptr);
				if (back) {
					light = back;
					++a_changed;
				}
			}
			a_t.takenLight = nullptr;
		}

		const RE::BGSReferenceEffect* FindFlashTemplate(RE::TESDataHandler* a_dh)
		{
			if (auto* fx = a_dh->LookupForm<RE::BGSReferenceEffect>(k360Flash, k360Plugin)) {
				return fx;
			}
			for (auto* fx : a_dh->GetFormArray<RE::BGSReferenceEffect>()) {
				if (fx && Contains(ModelOf(fx->data.artObject), "wardshieldhitfx")) {
					return fx;
				}
			}
			return nullptr;
		}
		// what a row wears now: its neutral art (Colors.cpp colors it), or nothing for a Vanilla row
		const Art* ArtFor(std::size_t a_row)
		{
			if (a_row >= kRows || !RowColor(a_row)) {
				return nullptr;
			}
			const auto& a = gArt[a_row];
			return a.hand && a.dome ? &a : nullptr;
		}

		// the hands and domes carry an ENB light sprite in the ENB set (`ENB\`), none in the other; the hit flash is one set
		std::string ModelName(std::string_view a_kind, std::size_t a_row)
		{
			const bool enbSet = a_kind != "wardshieldhitfx" && LightingPick() == Lighting::kEnb;
			return std::format("{}{}{} - {}.nif", kLookDir, enbSet ? "ENB\\" : "", a_kind, SlotToken(a_row));
		}

		// what a ward wears now: nothing for the silent effect, a Vanilla row, or a ward found by what it is whose mod is
		// unticked on the Compatibility page (one tick per mod, not one switch for all)
		// Strange Runes restyles every ward spell's effect that carries the MagicWard keyword from its scripts (its MCM's
		// ward look, set again at each load); with KeepStrangeRunes those are its, and we leave them (2026-10-03, his pick)
		bool RunesOwn(const Target& a_t)
		{
			return StrangeRunesLoaded() && KeepStrangeRunes() && a_t.effect->HasKeywordString("MagicWard");
		}

		const Art* Worn(const Target& a_t)
		{
			if (a_t.how == How::kSilent || (a_t.how == How::kFound && !ModOn(a_t.plugin)) || RunesOwn(a_t)) {
				return nullptr;
			}
			return gRowArt[a_t.row];
		}

		// the table entry of a ward we found, or nullptr (gLock held)
		const Target* TargetOf(const RE::EffectSetting* a_effect)
		{
			const auto it = gTargetOf.find(a_effect);
			return it == gTargetOf.end() ? nullptr : &gTargets[it->second];
		}

		RE::TESGlobal* MakeGlobal(const char* a_id)
		{
			if (auto* had = RE::TESForm::LookupByEditorID<RE::TESGlobal>(a_id)) {
				return had;  // registered already (a reload of the data, or another mod's form of that name)
			}
			auto* g = NewForm<RE::TESGlobal>();
			if (!g) {
				return nullptr;
			}
			g->value = 1.0f;
			g->SetFormEditorID(a_id);
			const auto& [map, lock] = RE::TESForm::GetAllFormsByEditorID();
			{
				RE::BSWriteLockGuard guard{ lock };
				if (!map) {
					return nullptr;
				}
				map->insert({ RE::BSFixedString(a_id), g });
			}
			return RE::TESForm::LookupByEditorID<RE::TESGlobal>(a_id) == g ? g : nullptr;
		}

		// A ward held ready kept the hand art of the color picked BEFORE a menu change until another ward was equipped:
		// the art sits on the magic effect and the game attaches it to the hand when the spell is equipped. Measured
		// (devbench): Actor.UnequipSpell + EquipSpell on one hand changed that hand to the new color while the other kept
		// the old; the caster's own Deselect/Select did nothing. So each hand holding a dressed ward is re-equipped through
		// those two Papyrus natives, the equip after the unequip has returned.
		struct AfterCall : RE::BSScript::IStackCallbackFunctor
		{
			std::function<void()> next;
			explicit AfterCall(std::function<void()> a_next) : next(std::move(a_next)) {}
			void operator()(RE::BSScript::Variable) override
			{
				// called by the Papyrus machine when the call has returned: nothing thrown may leave into it
				Guarded("hand refresh", [this]() {
					if (next) {
						next();
					}
				});
			}
			void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}
		};

		bool CallOnPlayer(const char* a_fn, RE::SpellItem* a_spell, std::int32_t a_hand, std::function<void()> a_then)
		{
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
			if (!policy || !player) {
				return false;
			}
			const auto                                               handle = policy->GetHandleForObject(RE::FormType::ActorCharacter, player);
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> done = RE::make_smart<AfterCall>(std::move(a_then));
			return vm->DispatchMethodCall(handle, "Actor", a_fn, RE::MakeFunctionArguments(std::move(a_spell), std::move(a_hand)), done);
		}

		// Only a hand whose ward changed what it wears in the hand is re-equipped (3.5, the outside review's "broad hand refresh":
		// every hand holding any ward of ours was re-equipped after ANY change - an Opacity or Transparency drag, a row it does not
		// wear, a shield's switch - and each re-equip restarts the hand's casting art): its effect's casting art was swapped
		// (a_castChanged - a row turned Vanilla, a Compatibility tick), or its row's hand look moved (a_handRows - the row's color
		// or Hand Brightness; Colors.cpp TakeHandLookChanges). gLock held; SendHands makes the calls once it is released.
		struct HandCall
		{
			RE::SpellItem* spell;
			std::int32_t   hand;  // Papyrus: 0 left, 1 right
		};

		std::vector<HandCall> HandsToRefresh(const std::array<bool, kRows>& a_handRows, const std::unordered_set<const RE::EffectSetting*>& a_castChanged)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || !player->Get3D()) {
				return {};
			}
			const auto changed = [&](const RE::Effect* e) {
				if (!e || !e->baseEffect) {
					return false;
				}
				if (a_castChanged.contains(e->baseEffect)) {
					return true;
				}
				const auto it = gTargetOf.find(e->baseEffect);
				if (it == gTargetOf.end()) {
					return false;
				}
				const auto& t = gTargets[it->second];
				// a hand that wears our casting art of that row (a ward wearing its own hand art keeps it whatever our colors do)
				return t.row < kRows && a_handRows[t.row] && Worn(t) && (t.how == How::kTable || t.castWard);
			};
			std::vector<HandCall> out;
			for (const std::int32_t hand : { 0, 1 }) {
				auto* spell = skyrim_cast<RE::SpellItem*>(player->GetEquippedObject(hand == 0));
				if (spell && std::ranges::any_of(spell->effects, changed)) {
					out.push_back({ spell, hand });
				}
			}
			return out;
		}

		void SendHands(const std::vector<HandCall>& a_hands)
		{
			for (const auto& [spell, hand] : a_hands) {
				const bool sent = CallOnPlayer("UnequipSpell", spell, hand, [spell, hand]() {
					CallOnPlayer("EquipSpell", spell, hand, {});
				});
				SKSE::log::info("hand refresh ({}): {} re-equipped {}", hand == 0 ? "left" : "right", spell->GetName(), sent ? "(sent)" : "(VM not ready)");
			}
		}
	}

	namespace
	{
		// Strange Wards' three picks are told apart by its dome's base color (read off its three meshes)
		enum class StrangePick : std::uint8_t
		{
			kNone,  // not installed
			kBlue,
			kAmber,
			kWhite,  // its Sswaye Special
			kUnknown
		};
		std::atomic<StrangePick> gStrangePick{ StrangePick::kNone };
		constexpr RE::NiColor    kStrangeBlue{ 0.098f, 0.592f, 0.902f };
		constexpr RE::NiColor    kStrangeAmber{ 0.886f, 0.345f, 0.133f };
		constexpr RE::NiColor    kStrangeWhite{ 0.935f, 0.869f, 0.843f };
		constexpr RE::FormID     kWardHandLight = 0x02F3EF;  // Skyrim.esm MagicLightWardHand01

		[[nodiscard]] bool NearColor(const RE::NiColor& a_a, const RE::NiColor& a_b)
		{
			return std::abs(a_a.red - a_b.red) < 0.02f && std::abs(a_a.green - a_b.green) < 0.02f && std::abs(a_a.blue - a_b.blue) < 0.02f;
		}

		[[nodiscard]] StrangePick PickOf(const RE::NiColor& a_base)
		{
			return NearColor(a_base, kStrangeBlue)  ? StrangePick::kBlue :
			       NearColor(a_base, kStrangeAmber) ? StrangePick::kAmber :
			       NearColor(a_base, kStrangeWhite) ? StrangePick::kWhite :
			                                          StrangePick::kUnknown;
		}

		[[nodiscard]] const char* PickName(StrangePick a_pick)
		{
			switch (a_pick) {
			case StrangePick::kNone:
				return "none";
			case StrangePick::kBlue:
				return "Blue";
			case StrangePick::kAmber:
				return "Amber";
			case StrangePick::kWhite:
				return "Sswaye Special";
			default:
				return "unknown";
			}
		}
	}

	// the dome at the game's own path is Strange Wards' when one of its textures lies under effects\po3_strangerunes\. The
	// model loader resolves on VR the way Colors.cpp's masters do
	void NoteStrangeWards()
	{
		RE::NiPointer<RE::NiNode> root;
		if (RE::BSModelDB::Demand("magic\\wardbodyfx.nif", root, RE::BSModelDB::DBTraits::ArgsType{}) != RE::BSResource::ErrorCode::kNone || !root) {
			SKSE::log::info("wards: Strange Wards not looked for (the game's ward dome did not load)");
			return;
		}
		bool                       installed = false;
		auto                       pick = StrangePick::kUnknown;
		std::optional<RE::NiColor> firstBase;  // named in the log when no block names a pick
		EachEffectBlock(root.get(), [&](RE::BSGeometry*, RE::BSEffectShaderProperty*, RE::BSEffectShaderMaterial* a_mat) {
			if (!Lower(a_mat->sourceTexturePath.c_str()).contains("po3_strangerunes") &&
				!Lower(a_mat->greyscaleTexturePath.c_str()).contains("po3_strangerunes")) {
				return;  // only Strange Wards' own blocks tell its pick
			}
			installed = true;
			const RE::NiColor base{ a_mat->baseColor.red, a_mat->baseColor.green, a_mat->baseColor.blue };
			firstBase = firstBase.value_or(base);
			if (pick == StrangePick::kUnknown) {
				pick = PickOf(base);  // the first block that names a pick decides
			}
		});
		if (!installed) {
			gStrangePick = StrangePick::kNone;
			SKSE::log::info("wards: Strange Wards is not installed");
			return;
		}
		gStrangePick = pick;
		const auto* color = pick == StrangePick::kAmber ? &kStrangeAmber : pick == StrangePick::kWhite ? &kStrangeWhite :
		                                                                                                 nullptr;
		auto*       light = color ? RE::TESForm::LookupByID<RE::TESObjectLIGH>(kWardHandLight) : nullptr;
		if (light) {
			const auto byte = [](float a_v) { return static_cast<std::uint8_t>(std::clamp(std::lround(a_v * 255.0f), 0L, 255L)); };
			light->data.color = RE::Color{ byte(color->red), byte(color->green), byte(color->blue), 0 };
		}
		const auto seen = firstBase.value_or(RE::NiColor{});
		SKSE::log::info("wards: Strange Wards is installed, its {} pick{} - the five rank rows step aside for its art; the game's ward hand light {}",
			PickName(pick), pick == StrangePick::kUnknown ? std::format(" (dome color {:.3f} {:.3f} {:.3f})", seen.red, seen.green, seen.blue) : std::string(),
			light ? "takes its color" : color ? "was not found and keeps its own color" :
												"keeps its own color");
	}

	bool StrangeWardsInstalled()
	{
		return gStrangePick.load() != StrangePick::kNone;
	}

	void MakeArt()
	{
		std::scoped_lock l{ gLock };
		auto*            dh = RE::TESDataHandler::GetSingleton();
		const auto*      tmpl = dh ? FindFlashTemplate(dh) : nullptr;
		gFlashTemplate = tmpl ? std::format("{} (art {}, flags {})", Where(tmpl), ModelOf(tmpl->data.artObject), tmpl->data.flags.underlying()) :
		                        std::string("none, FaceTarget default");
		auto makeFlash = [&](RE::BGSArtObject* a_art) -> RE::BGSReferenceEffect* {
			auto* fx = NewForm<RE::BGSReferenceEffect>();
			if (fx) {
				if (tmpl) {
					fx->data = tmpl->data;
				} else {
					fx->data.flags = RE::BGSReferenceEffect::Flag::kFaceTarget;
				}
				fx->data.artObject = a_art;
				fx->data.effectShader = nullptr;
			}
			return fx;
		};
		gEmpty = MakeArtObject(kEmptyModel);
		gNoFlash = makeFlash(gEmpty);
		gHas360Plugin = Loaded(dh, k360Plugin);
		gHas360 = gHas360Plugin && REX::W32::GetModuleHandleA(k360Patch) != nullptr;
		gLightPlacer = REX::W32::GetModuleHandleA("po3_LightPlacer.dll") != nullptr;
		gLightsGlobal = MakeGlobal(kLightsGlobal);
		gHandGlobal = MakeGlobal(kHandGlobal);
		gPresentGlobal = MakeGlobal(kPresentGlobal);
		std::size_t made = 0, missing = 0, worn = 0, looks = 0;
		for (std::size_t row = 0; row < kSlots; ++row) {
			const auto hand = ModelName("wardinhandfx", row);
			const auto dome = ModelName("wardbodyfx", row);
			if (!MeshExists(hand) || !MeshExists(dome)) {
				if (row < kRows) {
					++missing;
				}
				continue;
			}
			Art                      a{ MakeArtObject(hand), MakeArtObject(dome) };
			std::vector<std::string> models{ hand, dome };
			if (const auto d360 = ModelName("wardbodyfx 360", row); MeshExists(d360)) {
				a.dome360 = MakeArtObject(d360);
				models.push_back(d360);
			}
			// both looping copies or neither (each rank row's; the build writes them in pairs), colored with the row (ten small
			// meshes; the devbench preview plays them in the rank rows). Half a pair is named: the row's always-on wards would
			// fall back to the cast meshes and their missing outer layers
			const auto wh = ModelName("wardinhandfx worn", row), wd = ModelName("wardbodyfx worn", row);
			const bool rank = row < kRanks || row >= kRows;  // the build ships them for the five ranks and the looks
			const bool hasHand = rank && MeshExists(wh), hasDome = rank && MeshExists(wd);
			if (hasHand && hasDome) {
				a.wornHand = MakeArtObject(wh);
				a.wornDome = MakeArtObject(wd);
				models.push_back(wh);
				models.push_back(wd);
				++worn;
			} else if (hasHand != hasDome) {
				SKSE::log::warn("art: {} has only one of its worn copies ({} is missing) - its always-on wards wear the cast meshes", SlotToken(row),
					hasHand ? wd : wh);
			}
			if (const auto hit = ModelName("wardshieldhitfx", row); MeshExists(hit)) {
				a.flash = makeFlash(MakeArtObject(hit));
				models.push_back(hit);
			}
			gArt[row] = a;
			NoteSlotArt(row, a.hand, a.dome, a.dome360, a.wornHand, a.wornDome);
			RegisterRowModels(row, std::move(models));
			if (row >= kRows) {
				++looks;
				continue;
			}
			gHas360Meshes = gHas360Meshes || a.dome360;
			++made;
		}
		SKSE::log::info("art: {} row(s) made, {} missing, {} with worn copies, {} other casters' look(s); flash template {}; 360 Ward {}; Light Placer {}; globals {} {} {}", made,
			missing, worn, looks, gFlashTemplate,
			Can360()      ? "loaded" :
			gHas360       ? "loaded, but our 360 domes are not installed (installed only with 360 Ward.esp in the plugin list - 360 dome off)" :
			gHas360Plugin ? "loaded without its Universal Patch SKSE (360 dome off)" :
							"not loaded",
			gLightPlacer ? "loaded" : "not loaded", gLightsGlobal ? kLightsGlobal : "NOT made",
			gHandGlobal ? kHandGlobal : "NOT made", gPresentGlobal ? kPresentGlobal : "NOT made");
	}

	std::vector<std::pair<std::string, std::size_t>> FoundMods()
	{
		std::scoped_lock                   l{ gLock };
		std::map<std::string, std::size_t> n;
		for (const auto& t : gTargets) {
			if (t.how == How::kFound) {
				++n[t.plugin];
			}
		}
		return { n.begin(), n.end() };
	}

	void FindWards()
	{
		auto* dh = RE::TESDataHandler::GetSingleton();
		if (!dh) {
			return;
		}
		std::unordered_map<RE::FormID, std::size_t> table;
		for (const auto& r : kTable) {
			if (const auto id = dh->LookupFormID(r.id, r.file)) {
				table[id] = RowOfToken(r.token);
			}
		}
		const auto          silentId = dh->LookupFormID(kSilent.id, kSilent.file);
		std::vector<Target> found;
		std::size_t         skipped = 0;
		// each always-on Ward Power effect's strongest magnitude on any spell or enchantment (its rank - PowerRow)
		std::unordered_map<const RE::EffectSetting*, float> carriers;
		auto                                                note = [&](RE::MagicItem* a_item) {
			for (auto* e : a_item->effects) {
				if (e && e->baseEffect && AlwaysOnPower(*e->baseEffect)) {
					auto& m = carriers[e->baseEffect];
					m = (std::max)(m, e->effectItem.magnitude);
				}
			}
		};
		for (auto* s : dh->GetFormArray<RE::SpellItem>()) {
			if (s) {
				note(s);
			}
		}
		for (auto* en : dh->GetFormArray<RE::EnchantmentItem>()) {
			if (en) {
				note(en);
			}
		}
		for (auto* eff : dh->GetFormArray<RE::EffectSetting>()) {
			if (!eff) {
				continue;
			}
			auto&      d = eff->data;
			Target     t{ eff, kRows, How::kFound, Contains(ModelOf(d.castingArt), kWardHand), Contains(ModelOf(d.hitEffectArt), kWardBody),
				Contains(ModelOf(d.enchantEffectArt), kWardBody), d.castingArt, d.hitEffectArt, d.enchantEffectArt, d.light, nullptr, {}, {} };
			const bool power = d.primaryAV == RE::ActorValue::kWardPower;
			t.constant = d.castingType == RE::MagicSystem::CastingType::kConstantEffect;
			if (eff->GetFormID() == silentId) {
				t.how = How::kSilent;
				t.why = "ShieldConcSelf, silent";
			} else if (auto it = table.find(eff->GetFormID()); it != table.end() && !t.castWard && !t.hitWard && !t.enchWard && !power) {
				// a table ward another mod made into something else - no ward art left, no Ward Power (Reliquary of Myth turns
				// vanilla Spellbreaker's ward into a script effect and raises the ward from its own ability, in the table too):
				// dressed, it would add a second dome over the real one (3.4)
				++skipped;
				SKSE::log::info("[WARD] left alone: {} (in the table, but another mod took its art and Ward Power away)", Where(eff));
				continue;
			} else if (it != table.end()) {
				t.how = How::kTable;
				t.row = it->second;
				t.why = std::format("1.0 table: {}", kTokens[t.row]);
			} else if (!t.castWard && !t.hitWard && !t.enchWard && !power) {
				continue;
			} else if (!power && d.archetype == RE::EffectSetting::Archetype::kScript) {
				++skipped;
				SKSE::log::info("[WARD] left alone: {} (script effect wearing ward art, no Ward Power)", Where(eff));
				continue;
			} else if (!t.hitWard && !t.enchWard && !power) {
				++skipped;
				SKSE::log::info("[WARD] left alone: {} (casting art only, no dome, no Ward Power)", Where(eff));
				continue;
			} else {
				// kept whatever its Compatibility tick says, so the tick works without a restart (ApplyAll reads it)
				if (const auto* file = eff->GetFile(0)) {
					t.plugin = std::string(file->GetFilename());
				}
				// an ALWAYS-ON ward (a constant effect: an item's ward while it is held or worn) is ranked by its Ward Power on the
				// vanilla wards' scale (PowerRow), never by its minimum skill - an item's effect says 0 there: Reliquary of Myth's
				// Staff of Magnus (999 Ward Power) wore the Novice row's white (3.4, a user report)
				const char* by = (t.castWard || t.hitWard || t.enchWard) ? (power ? "art + Ward Power" : "art") : "Ward Power";
				// (its magnitude is Ward Power only when that is its value; one effect shared by a weak and a strong item takes the
				// strong one's rank - nothing says which carrier is up)
				const bool  always = AlwaysOnPower(*eff);
				const float most = always ? StrongestWard(eff, carriers) : 0.0f;
				t.row = always ? PowerRow(most) : RankRow(d.minimumSkill);
				t.why = always ? std::format("found by {}; always on, {:.0f} Ward Power = {}", by, most, kTokens[t.row]) :
				                 std::format("found by {}; minimum skill {} = {}", by, d.minimumSkill, kTokens[t.row]);
			}
			found.push_back(std::move(t));
		}
		std::vector<std::pair<RE::TESObjectARMO*, RE::EnchantmentItem*>> shields;
		RE::EnchantmentItem*                                             ench = nullptr;
		if (Loaded(dh, kCrusaderHub) && Loaded(dh, kKnights)) {
			if ((ench = dh->LookupForm<RE::EnchantmentItem>(kCrusaderEnch, kKnights))) {
				for (const auto id : kCrusaderShields) {
					if (auto* armo = dh->LookupForm<RE::TESObjectARMO>(id, kLegacy)) {
						shields.emplace_back(armo, armo->formEnchanting);
					}
				}
			}
		}
		// every entry through which a worn shield raises a shield-row ward (its enchantment), and every ability holding one
		// (Reman's shield adds its ward as an ability from its equip script)
		std::unordered_map<RE::EffectSetting*, std::size_t> shieldWard;
		for (const auto& t : found) {
			if (t.how == How::kTable && std::ranges::find(kShieldRows, t.row) != kShieldRows.end()) {
				shieldWard.emplace(t.effect, t.row);
			}
		}
		std::vector<ShieldEntry>                                   entries;
		std::unordered_set<const RE::Effect*>                      seen;
		std::unordered_map<RE::EffectSetting*, RE::EffectSetting*> inert;
		auto                                                       collect = [&](RE::MagicItem* a_carrier, bool a_ability) {
			for (auto* e : a_carrier->effects) {
				const auto it = e ? shieldWard.find(e->baseEffect) : shieldWard.end();
				if (it == shieldWard.end() || !seen.insert(e).second) {
					continue;
				}
				auto& copy = inert[it->first];
				if (!copy) {
					copy = InertCopy(it->first);
					if (!copy) {
						SKSE::log::warn("[SHIELD] no inert copy of {} could be made - its shield's Off switch leaves that ward on", Where(it->first));
					}
				}
				entries.push_back({ e, it->first, copy, it->second, a_carrier, a_ability });
			}
		};
		if (!shieldWard.empty()) {
			for (auto* armo : dh->GetFormArray<RE::TESObjectARMO>()) {
				if (armo && armo->IsShield() && armo->formEnchanting) {
					collect(armo->formEnchanting, false);
				}
			}
			if (ench) {  // the Crusader ward the Legacy shields can be given (the switch above), also on the Knights shield
				collect(ench, false);
			}
			for (auto* spell : dh->GetFormArray<RE::SpellItem>()) {
				if (spell && spell->GetSpellType() == RE::MagicSystem::SpellType::kAbility) {
					collect(spell, true);
				}
			}
		}
		for (const auto& s : entries) {
			SKSE::log::info("[SHIELD] {} raises the {} ward through {} ({})", Where(s.carrier), kTokens[s.row], Where(s.ward),
				s.ability ? "an ability" : "an enchantment");
		}
		// a shield's enchantment record worn by something that is not a shield follows that shield's switch too
		bool shared = false;
		if (!entries.empty()) {
			std::unordered_set<const RE::MagicItem*> shieldEnchants;
			for (const auto& s : entries) {
				if (!s.ability) {
					shieldEnchants.insert(s.carrier);
				}
			}
			for (auto* armo : dh->GetFormArray<RE::TESObjectARMO>()) {
				if (armo && !armo->IsShield() && armo->formEnchanting && shieldEnchants.contains(armo->formEnchanting)) {
					SKSE::log::warn("[SHIELD] {} shares its enchantment with a shield: that shield's switch takes its ward too", Where(armo));
					shared = true;
				}
			}
		}
		std::scoped_lock l{ gLock };
		gShieldEntries = std::move(entries);
		gSharedEnchant = shared;
		gTargets = std::move(found);
		gTargetOf.clear();
		for (std::size_t i = 0; i < gTargets.size(); ++i) {
			gTargetOf.emplace(gTargets[i].effect, i);
		}
		gSkipped = skipped;
		gShields = std::move(shields);
		gCrusaderEnch = ench;
		// (counted before ApplyAll decides who is dressed: a ward whose mod is unticked or whose row is Vanilla wears its own art;
		// one in a row without worn copies wears the cast meshes - MakeArt's line says how many rows have them)
		const auto always = std::ranges::count_if(gTargets, WearsWorn);
		const auto noCopy = std::ranges::count_if(gTargets, [](const Target& a) { return WearsWorn(a) && gArt[a.row].hand && !gArt[a.row].wornDome; });
		SKSE::log::info("wards: {} found ({} from the 1.0 table), {} left alone, {} always on with ward art ({} in a row without worn copies); Legacy Crusader shields {}",
			gTargets.size(), std::ranges::count_if(gTargets, [](const Target& a) { return a.how == How::kTable; }), gSkipped, always, noCopy,
			gShields.empty() ? "not in this load order" : std::format("{} found", gShields.size()));
	}

	bool StrangeRunesLoaded()
	{
		static const bool loaded = [] {
			auto* dh = RE::TESDataHandler::GetSingleton();
			return dh && Loaded(dh, "StrangeRunes.esp");
		}();
		return loaded;
	}

	// another mod's script can swap a dressed ward's art after we dressed it (Strange Runes does at every load and on its
	// MCM): checked every 2 s on the main thread, and a swapped ward is dressed again; the same round re-reads the 360 unlock. Wards left to another mod (Worn null)
	// are never touched here, so two mods never fight over one.
	void WatchArt()
	{
		// one check waiting at a time: a long load would otherwise queue one every 2 s
		Every(std::chrono::seconds(2), "art watch", []() {
			std::size_t swapped = 0;
			{
				std::scoped_lock l{ gLock };
				for (const auto& t : gTargets) {
					const Art* a = Worn(t);
					if (!a) {
						continue;
					}
					const auto  w = DressOf(t, a);  // every slot ApplyAll dresses
					const auto& e = t.effect->data;
					if ((w.cast && e.castingArt != w.cast) || (w.hit && e.hitEffectArt != w.hit) || (w.ench && e.enchantEffectArt != w.ench)) {
						++swapped;
					}
				}
			}
			if (swapped) {
				SKSE::log::info("[WARD] another mod swapped the art of {} dressed ward(s); dressing them again", swapped);
				ApplyAll(kWhyArtSwapped);
			} else {
				// the 360 unlock follows the skill (or the perk) within 2 s: until 3.5 it was read again only when a
				// menu closed, so a Restoration level-up mid-fight changed nothing (the outside review). Cheap: one
				// actor value read; ApplyAll runs only when the answer changed
				CheckUnlock(kWhyUnlock);
			}
			WatchBlocking();       // a transformation or a 3D reset gives her a new graph: the block watch goes onto it
			ReleaseOldTextures();  // a color drag's retired palettes, without waiting for the next recolor
		});
	}

	namespace
	{
		// casting art a background apply moved (another mod's swap, the unlock), which takes no hand looks: the next menu or
		// devbench apply re-equips those hands, as it would have found nothing moved (gLock)
		std::unordered_set<const RE::EffectSetting*> gCastPending;

		struct Applied
		{
			bool                  artChanged = false;
			std::vector<HandCall> hands;
			std::string           why;
			std::size_t           targets = 0, dressed = 0;
		};

		// gLock held. What calls out of this plugin (other plugins' art listeners, Papyrus, DevBench) is handed back to ApplyAll,
		// which makes those calls once the lock is released: a listener that took gLock again would deadlock
		Applied ApplyLocked(const char* a_why)
		{
			Applied out;
			gUnlocked = Unlocked();
			for (std::size_t r = 0; r < kRows; ++r) {
				gRowArt[r] = ArtFor(r);
			}
			const bool                                   light = WardLightOn();
			const bool                                   use360 = Can360() && Dome360();
			const bool                                   every = EveryWard();
			Changes                                      c;
			std::size_t                                  dressed = 0;
			std::unordered_set<const RE::EffectSetting*> castChanged;  // the effects whose casting art moved in this apply
			for (auto& t : gTargets) {
				auto&       d = t.effect->data;
				auto* const castBefore = d.castingArt;
				if (t.how == How::kSilent) {
					if (gEmpty) {
						Put(d.hitEffectArt, gEmpty, c.art);
						Put(d.castingArt, gEmpty, c.art);
					}
				} else if (const Art* a = Worn(t); !a) {
					Restore(d.castingArt, t.ownCast, c.art);
					Restore(d.hitEffectArt, t.ownHit, c.art);
					Restore(d.enchantEffectArt, t.ownEnch, c.art);
					// a row the player set to Vanilla: its own art stays reachable, so another caster's look with its own color
					// still dresses it (a Strange Wards rank, a mod unticked or Strange Runes' own stay theirs)
					if (t.row < kRows && !RowColor(t.row) && !(t.row < kRanks && StrangeWardsInstalled()) && (t.how != How::kFound || ModOn(t.plugin)) && !RunesOwn(t)) {
						const bool looping = WearsWorn(t) || std::ranges::find(kShieldRows, t.row) != kShieldRows.end();
						NoteGameArt(t.effect, t.ownCast, t.row, true, looping, false);
						NoteGameArt(t.effect, t.ownHit, t.row, false, looping, use360);
						NoteGameArt(t.effect, t.ownEnch, t.row, false, looping, use360);
					}
				} else {
					// another caster on the Vanilla look wears what the effect itself had: the empty art where it had none
					const auto w = DressOf(t, a);
					if (w.cast) {
						Put(d.castingArt, w.cast, c.art);
						NoteTheirArt(t.effect, w.cast, t.ownCast, gEmpty);
					}
					if (w.hit) {
						Put(d.hitEffectArt, w.hit, c.art);
						NoteTheirArt(t.effect, w.hit, t.ownHit, gEmpty);
					}
					if (w.ench) {
						Put(d.enchantEffectArt, w.ench, c.art);
						NoteTheirArt(t.effect, w.ench, t.ownEnch, gEmpty);
					}
					if (w.cast || w.hit || w.ench) {
						++dressed;
					}
				}
				if (d.castingArt != castBefore) {
					castChanged.insert(t.effect);
				}
			}
			// 3.0: the art is the same objects whatever the color - Colors.cpp recolors it (palettes live, glow on the cached model)
			ApplyColors();
			// the hand looks are taken on every apply but the two background ones (the 2 s re-dress after another mod swapped the art,
			// the 360 unlock check): one of those landing during a color drag would mark the dragged color as seen, and the menu's
			// apply when the drag ends would then re-equip nothing (the 3.5 code review)
			const std::string_view why = a_why ? a_why : "";
			const bool             background = why == kWhyArtSwapped || why == kWhyUnlock;
			const auto             handRows = background ? std::array<bool, kRows>{} : TakeHandLookChanges();
			out.artChanged = c.art != 0;
			// a ward already in the hand takes its new art or color now, not at the next equip - only for a change made in the menu
			// (or devbench's setter; a load or a loading screen equips everything afresh anyway), and only the hand whose ward changed
			if (background) {
				gCastPending.insert(castChanged.begin(), castChanged.end());
			} else {
				castChanged.insert(gCastPending.begin(), gCastPending.end());
				gCastPending.clear();  // a load or a loading screen equips everything afresh
			}
			if (why == "menu" || why == "devbench") {
				out.hands = HandsToRefresh(handRows, castChanged);
			}
			for (auto& t : gTargets) {
				// HIS REPORT, 2026-09-24: a white flash on the first cast, a light that "starts strong then weakens", a hand light
				// too big beside his other casting art. The silent row fires with EVERY vanilla ward and kept the game's white
				// ward light, a second light on the hand; it never has one now. Under Light Placer a dressed ward's own light goes
				// too - our config lights the hand in the ward's color, behind the Ward casting light switch (kHandGlobal).
				// Under RE::Light the effect's light IS the colored hand light (RELight - Spell Addon points it), so the switch
				// decides it. A Vanilla row only gets back a light the switch took.
				// The lighting (Lighting.cpp): where this plugin makes the hand light itself (Vanilla, or
				// no light framework loaded) a colored ward casts with the game's ward light in its own color. Since 2026-10-06
				// every pick (ENB too) gets this plugin's hand light, so a dressed ward's own light always goes.
				auto&       slot = t.effect->data.light;
				auto* const before = slot;
				std::size_t quiet = 0;
				if (t.how == How::kSilent) {
					ApplyLight(t, false, false, quiet);
				} else {
					// only a ward whose casting art is ours has its light decided here; one wearing its own hand art keeps its own light
					const bool ourHand = Worn(t) && (t.how == How::kTable || t.castWard);
					if (ourHand) {
						if (slot) {
							t.takenLight = slot;  // back when the row turns Vanilla
						}
						// another caster on the Vanilla look gets a hand light in this light's color (DomeLights.cpp)
						NoteTheirLight(t.effect, t.takenLight ? t.takenLight : t.ownLight);
						// 2026-10-05 (his report: the ward lit the caster but not the ground): the game's casting light never reaches
						// the land, so DomeLights.cpp hangs the hand light itself on every caster (behind the same switch) and the
						// effect carries none - two lights on one hand would double the caster
						slot = nullptr;
					} else {
						// a Vanilla row, or a ward wearing another mod's hand art, keeps (or gets back) its own light
						ApplyLight(t, true, Worn(t) != nullptr, quiet);
					}
				}
				if (slot != before) {
					++c.light;
				}
			}
			PublishTheirs();
			for (auto& [armo, own] : gShields) {
				auto* want = (CrusaderOn() && gCrusaderEnch) ? gCrusaderEnch : own;
				if (armo->formEnchanting != want) {
					armo->formEnchanting = want;
					++c.shield;
				}
			}
			std::vector<const ShieldEntry*> switched;
			for (const auto& s : gShieldEntries) {
				if (s.entry->baseEffect != ShieldWant(s)) {
					switched.push_back(&s);
				}
			}
			auto refresh = TakeShieldEffects(switched);  // before the switch: each effect ends on the base it started on
			for (const auto* s : switched) {
				s->entry->baseEffect = ShieldWant(*s);
			}
			std::size_t givenBack = 0;
			for (const auto& [actor, spell] : refresh.giveBack) {
				givenBack += actor->AddSpell(spell) ? 1 : 0;
			}
			// a shield's enchantment ward switched back ON comes back at once on everyone wearing that shield, as switching it off
			// took it at once (measured 2026-10-10 on an NPC wearing Spellbreaker: on again left him without his ward until the
			// shield was equipped again) - the game's own equip step for an armor's enchantment, on the worn copy
			std::unordered_set<const RE::MagicItem*> backOn;
			for (const auto* s : switched) {
				if (!s->ability && s->entry->baseEffect == s->ward) {
					backOn.insert(s->carrier);
				}
			}
			std::size_t reapplied = 0;
			for (auto* actor : backOn.empty() ? std::vector<RE::Actor*>{} : ShieldActors()) {
				// unless another armor shares one of those enchantments (FindWards warns), only an actor wearing such a shield needs
				// its inventory read
				const auto* shield = actor->GetWornArmor(RE::BGSBipedObjectForm::BipedObjectSlot::kShield);
				if (!gSharedEnchant && (!shield || !shield->formEnchanting || !backOn.contains(shield->formEnchanting))) {
					continue;
				}
				for (auto& [obj, item] : actor->GetInventory()) {
					auto* armo = obj ? obj->As<RE::TESObjectARMO>() : nullptr;
					auto* entry = item.second.get();
					if (!armo || !armo->formEnchanting || !backOn.contains(armo->formEnchanting) || !entry || !entry->extraLists) {
						continue;
					}
					for (auto* xl : *entry->extraLists) {
						if (xl && (xl->HasType<RE::ExtraWorn>() || xl->HasType<RE::ExtraWornLeft>())) {
							actor->UpdateArmorAbility(armo, xl);
							++reapplied;
						}
					}
				}
			}
			if (!switched.empty()) {
				const auto pick = [](std::size_t a_row) {
					const auto d = ShieldDomeOf(a_row);
					return d == ShieldDome::kOff ? "off" : d == ShieldDome::k360 ? "360 dome" :
					                                                               "normal dome";
				};
				SKSE::log::info(
					"shield wards: {} entr(ies) switched (Spellbreaker {}, Crusader {}, Reman {}); {} enchantment effect(s) dispelled, {} of {} "
					"ability(ies) given back, {} worn shield(s) given their ward again",
					switched.size(), pick(kShieldRows[0]), pick(kShieldRows[1]), pick(kShieldRows[2]), refresh.dispelled, givenBack,
					refresh.giveBack.size(), reapplied);
			}
			if (gLightsGlobal) {
				gLightsGlobal->value = ColoredLightsOn() ? 1.0f : 0.0f;
			}
			if (gHandGlobal) {
				gHandGlobal->value = light ? 1.0f : 0.0f;
			}
			gDressed = dressed;
			gLastApply = a_why ? a_why : "?";
			SKSE::log::info("apply ({}): {} dressed; changed {} art, {} light, {} shield; {} hand(s) to re-equip; ward light {}, colored lights {}; dome {}; unlisted mods {}",
				gLastApply, dressed, c.art, c.light, c.shield, out.hands.size(), light ? "on" : "off", ColoredLightsOn() ? "on" : "off",
				!use360 ? "normal" : gUnlocked ? "360 (unlocked)" :
												 "360 (locked)",
				every ? "on" : "off");
			out.why = gLastApply;
			out.targets = gTargets.size();
			out.dressed = dressed;
			return out;
		}
	}

	void ApplyAll(const char* a_why)
	{
		Applied done;
		{
			std::scoped_lock l{ gLock };
			done = ApplyLocked(a_why);
		}
		if (done.artChanged) {
			SKSE::GetMessagingInterface()->Dispatch(kArtChanged, nullptr, 0, nullptr);
		}
		SendHands(done.hands);
		WardsApplied(done.why.c_str(), done.targets, done.dressed);
	}

	void CheckUnlock(const char* a_why)
	{
		{
			std::scoped_lock l{ gLock };
			if (!Can360() || !Any360Wanted() || Unlocked() == gUnlocked) {
				return;
			}
		}
		ApplyAll(a_why);
	}

	bool Has360Ward()
	{
		return Can360();  // two flags set once at data load: no lock, so ApplyColors may ask it under the wards lock
	}

	bool Missing360Meshes()
	{
		std::scoped_lock l{ gLock };
		return gHas360 && !gHas360Meshes;
	}

	bool Missing360Patch()
	{
		std::scoped_lock l{ gLock };
		return gHas360Plugin && !gHas360;
	}

	bool Unlocked360()
	{
		std::scoped_lock l{ gLock };
		return gUnlocked;
	}

	bool CrusaderAvailable()
	{
		std::scoped_lock l{ gLock };
		return !gShields.empty() && gCrusaderEnch;
	}

	bool ShieldFound(std::size_t a_row)
	{
		std::scoped_lock l{ gLock };
		return std::ranges::any_of(gShieldEntries, [a_row](const ShieldEntry& s) { return s.row == a_row; });
	}

	std::size_t DressedCount()
	{
		std::scoped_lock l{ gLock };
		return gDressed;
	}

	std::size_t FoundCount()
	{
		std::scoped_lock l{ gLock };
		return gTargets.size();
	}

	RE::BGSReferenceEffect* FlashFor(RE::EffectSetting* a_effect, RE::Actor* a_caster)
	{
		if (!a_effect) {
			return nullptr;
		}
		std::size_t             row = kRows;
		RE::BGSReferenceEffect* own = nullptr;
		{
			std::scoped_lock l{ gLock };
			const Target*    t = TargetOf(a_effect);
			const Art*       a = t ? Worn(*t) : nullptr;
			if (!a) {
				return nullptr;  // 360 Ward's own flash plays
			}
			if (!a->dome360 || DomeOf(t->row, a) != a->dome360) {
				return gNoFlash;  // the vanilla-shaped dome never flashed
			}
			row = t->row;
			own = a->flash;
		}
		// another caster's ward flashes in his look, asked outside the lock (it reads his factions); the art objects never change
		// after data load. The game's art plays 360 Ward's own flash, a look's own color its own
		const auto slot = TheirSlot(a_caster, row);
		if (slot >= kSlots) {
			return nullptr;
		}
		auto* flash = slot >= kRows ? gArt[slot].flash : own;
		return flash ? flash : gNoFlash;
	}

	bool PreviewRow(std::size_t a_row, float a_seconds)
	{
		auto*            player = RE::PlayerCharacter::GetSingleton();
		std::scoped_lock l{ gLock };
		const Art*       a = a_row < kRows ? ArtFor(a_row) : nullptr;
		if (!player || !a) {
			return false;
		}
		// the looping copy where the row has one: put on as plain art, the cast dome waits in its idle state for a cast and
		// shows next to nothing (the 2026-10-14 preview pictures had no dome while its light was lit)
		auto* dome = WornDomeOf(a_row, a_row < kRanks, a);
		return player->ApplyArtObject(dome, a_seconds) != nullptr;
	}

	// The row of an always-on ward of ours that is up on the actor - what a drawn staff's hand light wears (DomeLights.cpp
	// CastingRow; Reliquary of Myth's Staff of Magnus raises its ward as an ability while held, so no cast names it). The
	// strongest of them; only the five rank rows (a shield's or the vampire ward never lights a staff hand) and only a ward that
	// wears ward art (one found by Ward Power alone shows nothing). Nothing links an ability to the staff that gave it, so a
	// ring's always-on ward lights a staff hand too.
	// LOCK ORDER: called with DomeLights.cpp's lock held; takes this file's lock, then Settings' (Worn -> ModOn). Nothing here
	// may call into DomeLights.cpp while holding this lock.
	std::size_t AlwaysOnWardRow(RE::Actor* a_actor)
	{
		auto* list = a_actor ? a_actor->AsMagicTarget()->GetActiveEffectList() : nullptr;
		if (!list) {
			return kRows;
		}
		std::scoped_lock l{ gLock };
		std::size_t      best = kRows;
		for (auto* ae : *list) {
			const auto* base = ae && ae->effect && !ae->flags.any(RE::ActiveEffect::Flag::kInactive, RE::ActiveEffect::Flag::kDispelled) ?
			                       ae->GetBaseObject() :
			                       nullptr;
			const auto  it = base && base->data.castingType == RE::MagicSystem::CastingType::kConstantEffect ?
			                     gTargetOf.find(base) :
			                     gTargetOf.end();
			if (it == gTargetOf.end()) {
				continue;
			}
			const auto& t = gTargets[it->second];
			if (WearsWorn(t) && Worn(t)) {
				best = best == kRows ? t.row : (std::max)(best, t.row);  // the strongest, whatever the list's order
				if (best == kRanks - 1) {
					break;
				}
			}
		}
		return best;
	}

	std::size_t WardRowOf(const RE::EffectSetting* a_effect)
	{
		std::scoped_lock l{ gLock };
		const Target*    t = TargetOf(a_effect);
		return t && Worn(*t) ? t->row : kRows;  // a ward left to another mod wears none of our rows
	}

	std::size_t RowOfModel(std::string_view a_model)
	{
		const auto key = ModelKey(a_model);
		if (!Contains(key, "dynamic wards\\")) {
			return kSlots;
		}
		const auto dash = key.rfind(" - ");
		const auto dot = key.rfind(".nif");
		if (dash == std::string::npos || dot == std::string::npos || dot < dash) {
			return kSlots;
		}
		const auto token = key.substr(dash + 3, dot - dash - 3);
		for (std::size_t i = 0; i < kSlots; ++i) {
			if (SameText(SlotToken(i), token)) {
				return i;
			}
		}
		return kSlots;
	}

	std::vector<std::pair<std::string, std::string>> WardPerks()
	{
		std::vector<std::pair<std::string, std::string>> out;
		if (auto* dh = RE::TESDataHandler::GetSingleton()) {
			for (auto* perk : dh->GetFormArray<RE::BGSPerk>()) {
				const char* n = perk ? perk->GetFullName() : nullptr;
				if (n && *n && Contains(Lower(n), "ward")) {
					if (auto id = FormText(perk); !id.empty()) {  // a perk made at run time has no plugin to name it by
						out.emplace_back(std::move(id), n);
					}
				}
			}
		}
		return out;
	}

	std::string WardsReport()
	{
		std::scoped_lock l{ gLock };
		auto             lightId = [](const RE::TESObjectLIGH* a_l) { return a_l ? std::format("{:08X}", a_l->GetFormID()) : std::string(); };
		std::string      wards;
		for (const auto& t : gTargets) {
			const auto& d = t.effect->data;
			const char* name = t.effect->GetFullName();
			wards += std::format(
				R"({}{{"effect":"{}","name":"{}","row":"{}","how":"{}","why":"{}","castingArt":"{}","hitEffectArt":"{}","enchantEffectArt":"{}",)"
				R"("ownCastingArt":"{}","ownHitEffectArt":"{}","light":"{}","ownLight":"{}","takenLight":"{}","dressed":{}}})",
				wards.empty() ? "" : ",", JsonEscape(Where(t.effect)), JsonEscape(name ? name : ""), t.row < kRows ? kTokens[t.row] : "silent",
				t.how == How::kTable ? "table" : t.how == How::kFound ? "found" :
																		"silent",
				JsonEscape(t.why), JsonEscape(ModelOf(d.castingArt)),
				JsonEscape(ModelOf(d.hitEffectArt)), JsonEscape(ModelOf(d.enchantEffectArt)), JsonEscape(ModelOf(t.ownCast)),
				JsonEscape(ModelOf(t.ownHit)), lightId(d.light), lightId(t.ownLight), lightId(t.takenLight), Worn(t) != nullptr);
		}
		std::string rows;
		for (std::size_t i = 0; i < kRows; ++i) {
			const auto col = RowColor(i);
			rows += std::format(R"({}"{}":{{"mode":{},"color":"{}","installed":{}}})", rows.empty() ? "" : ",", kTokens[i],
				static_cast<int>(RowModeOf(i)), col ? HexColor(*col) : std::string("vanilla"), gArt[i].hand != nullptr);
		}
		std::string shields;
		for (const auto& [armo, own] : gShields) {
			shields += std::format(R"({}{{"shield":"{}","enchantment":"{}","own":"{}"}})", shields.empty() ? "" : ",", JsonEscape(Where(armo)),
				JsonEscape(Where(armo->formEnchanting)), JsonEscape(Where(own)));
		}
		std::string shieldWards;
		for (const auto& s : gShieldEntries) {
			shieldWards += std::format(R"({}{{"row":"{}","carrier":"{}","ability":{},"ward":"{}","on":{},"dome":{},"raisesWard":{}}})",
				shieldWards.empty() ? "" : ",", kTokens[s.row], JsonEscape(Where(s.carrier)), s.ability, JsonEscape(Where(s.ward)),
				ShieldWardOn(s.row), static_cast<int>(ShieldDomeOf(s.row)), s.entry->baseEffect == s.ward);
		}
		return std::format(
			R"({{"found":{},"dressed":{},"leftAlone":{},"lastApply":"{}","looks":{},"flashTemplate":"{}","has360Ward":{},"dome360Installed":{},"dome360":{},)"
			R"("unlocked360":{},"unlockRule":{},"unlockPerk":"{}","ladder":"{}","wardLight":{},"coloredLights":{},"lightsGlobal":{},)"
			R"("lightPlacer":{},"everyWard":{},"crusader":{},"strangeWards":"{}","rows":{{{}}},"shields":[{}],"shieldWards":[{}],"wards":[{}]}})",
			gTargets.size(), gDressed, gSkipped, JsonEscape(gLastApply), std::ranges::count_if(gArt, [](const Art& a) { return a.hand != nullptr; }),
			JsonEscape(gFlashTemplate), gHas360.load(), gHas360Meshes.load(), Dome360(), gUnlocked, static_cast<int>(UnlockRule()), JsonEscape(UnlockPerk()),
			std::format("{} {}{} opacity {}", HexColor(LadderColor()), LadderStages(), LadderReversed() ? " reversed" : "", Opacity()), WardLightOn(), ColoredLightsOn(),
			gLightsGlobal ? std::format("{}", gLightsGlobal->value) : std::string("null"), gLightPlacer, EveryWard(), CrusaderOn(), PickName(gStrangePick.load()), rows,
			shields, shieldWards, wards);
	}

	// ------------------------------------------------------------------ the shield's ward at once
	// A shield raises its ward through its enchantment, whose effects ask IsBlocking; the game re-checks an active effect's
	// conditions only on its own timer, so the dome came up to a second after the block (and stayed as long after it). When the
	// player's block starts, those effects are checked at once and again a moment later in case she is not marked blocking yet,
	// then her block is followed and they are checked the moment it ends. Nothing else changes.
	namespace
	{
		// the event her graph sends as a block begins ("blockStart" is the input event and never reaches the graph)
		constexpr std::string_view kBlockTag = "blockStartOut";
		constexpr auto             kSecondCheck = std::chrono::milliseconds(50);
		constexpr auto             kBlockLook = std::chrono::milliseconds(100);

		[[nodiscard]] bool AsksBlocking(const RE::TESCondition& a_conditions)
		{
			for (const auto* item = a_conditions.head; item; item = item->next) {
				if (item->data.functionData.function.get() == RE::FUNCTION_DATA::FunctionID::kIsBlocking) {
					return true;
				}
			}
			return false;
		}

		// an effect that asks IsBlocking, on the effect or on its magic effect: an enchantment's (the shield wards, the game's and
		// any mod's) or an ability's that raises Ward Power (a block perk's ward)
		[[nodiscard]] bool WaitsForBlock(const RE::ActiveEffect* a_effect)
		{
			if (!a_effect || !a_effect->effect || !a_effect->spell || a_effect->flags.any(RE::ActiveEffect::Flag::kDispelled)) {
				return false;
			}
			using Type = RE::MagicSystem::SpellType;
			const auto* base = a_effect->GetBaseObject();
			const auto  type = a_effect->spell->GetSpellType();
			const bool  ward = base && base->data.primaryAV == RE::ActorValue::kWardPower;
			if (type != Type::kEnchantment && !(type == Type::kAbility && ward)) {
				return false;
			}
			return AsksBlocking(a_effect->effect->conditions) || (base && AsksBlocking(base->conditions));
		}

		class ShieldWards : public RE::MagicTarget::ForEachActiveEffectVisitor
		{
		public:
			RE::BSContainer::ForEachResult Accept(RE::ActiveEffect* a_effect) override
			{
				if (WaitsForBlock(a_effect)) {
					found.push_back(a_effect);
				}
				return RE::BSContainer::ForEachResult::kContinue;
			}

			std::vector<RE::ActiveEffect*> found;
		};

		// main thread. Collected first, then checked, so the list is never walked while an effect changes state
		void RecheckShieldWards()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* target = player ? player->AsMagicTarget() : nullptr;
			if (!target) {
				return;
			}
			ShieldWards wards;
			target->VisitEffects(wards);
			auto*      list = target->GetActiveEffectList();
			const auto inList = [list](RE::ActiveEffect* a_effect) { return list && std::find(list->begin(), list->end(), a_effect) != list->end(); };
			for (auto* effect : wards.found) {
				// one effect's change can end another: only one still in her list is checked
				if (inList(effect) && effect->flags.none(RE::ActiveEffect::Flag::kDispelled)) {
					effect->EvaluateConditions(0.0f, true);
				}
			}
		}

		// both graphs send each block start, so the checks are merged: one queued check at a time, and one helper thread (made
		// at the first block, asleep between blocks) for the check a moment later and the look for the block's end. A task
		// queued from a task runs in the same frame, hence a thread for the waits
		std::atomic_bool           gCheckQueued{ false };
		std::atomic_bool           gLookQueued{ false };
		std::atomic<bool>          gBlockEnded{ false };
		std::atomic<std::uint32_t> gBlockStarts{ 0 };
		// the first look ~150 ms after a block starts can come before the game sets her blocking: a block ends only once she was
		// seen blocking, or after kBlockLooksBlind looks that never saw it (a tap too short to read)
		std::atomic<bool> gSeenBlocking{ false };
		std::atomic<int>  gLooks{ 0 };
		constexpr int     kBlockLooksBlind = 10;

		void QueueCheck(const char* a_why)
		{
			(void)LaterOnce(gCheckQueued, a_why, []() { RecheckShieldWards(); });
		}

		// main thread: once she no longer blocks, the shield wards are checked so they end at once
		// a_block: the block it was queued for - a check still waiting when the next block starts does nothing
		void LookForBlockEnd(std::uint32_t a_block)
		{
			const auto* player = RE::PlayerCharacter::GetSingleton();
			if (gBlockEnded || gBlockStarts.load() != a_block) {
				return;
			}
			if (player && player->IsBlocking()) {
				gSeenBlocking = true;
				return;
			}
			if (!player || gSeenBlocking || ++gLooks >= kBlockLooksBlind) {
				gBlockEnded = true;
				RecheckShieldWards();
			}
		}

		void FollowBlocks()
		{
			std::uint32_t seen = 0;
			for (;;) {
				gBlockStarts.wait(seen);
				seen = gBlockStarts.load();
				std::this_thread::sleep_for(kSecondCheck);
				QueueCheck("shield ward check (a moment later)");
				gSeenBlocking = false;
				gLooks = 0;
				gBlockEnded = false;
				while (!gBlockEnded && gBlockStarts.load() == seen) {
					std::this_thread::sleep_for(kBlockLook);
					(void)LaterOnce(gLookQueued, "block end check", [seen]() { LookForBlockEnd(seen); });
				}
			}
		}

		class BlockWatch : public RE::BSTEventSink<RE::BSAnimationGraphEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* a_event, RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override
			{
				Guarded("block watch", [a_event]() {
					static const RE::BSFixedString blockTag{ kBlockTag };  // the game's strings are pooled: one pointer compare per event
					if (!a_event || a_event->tag != blockTag) {
						return;
					}
					static std::atomic<bool> told{ false };
					if (!told.exchange(true)) {
						SKSE::log::info("[WARD] a block started - the shield wards are checked at once");
						std::thread(FollowBlocks).detach();
					}
					// the graph's events may come off the main thread: the effects are read on it
					QueueCheck("shield ward check");
					++gBlockStarts;
					gBlockStarts.notify_one();
				});
				return RE::BSEventNotifyControl::kContinue;
			}
		};
		BlockWatch gBlockWatch;
	}

	// every graph she has (third and first person) gets the watch; a source keeps a sink only once, so a rebuilt graph (a
	// transformation, a 3D reset) simply gets it on the next round, and nothing of the game is held
	void WatchBlocking()
	{
		auto*                                            player = RE::PlayerCharacter::GetSingleton();
		RE::BSTSmartPointer<RE::BSAnimationGraphManager> manager;
		if (!player || !player->GetAnimationGraphManager(manager) || !manager) {
			return;
		}
		std::size_t graphs = 0;
		for (const auto& graph : manager->graphs) {
			if (graph) {
				graph->GetEventSource<RE::BSAnimationGraphEvent>()->AddEventSink(&gBlockWatch);
				++graphs;
			}
		}
		static std::size_t told = 0;  // main thread; said again only when the count changes
		if (graphs != told) {
			told = graphs;
			SKSE::log::info("[WARD] the block watch is on the player's {} animation graph(s)", graphs);
		}
	}
}
