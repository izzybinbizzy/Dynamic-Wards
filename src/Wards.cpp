// Dynamic Wards - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.

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
		constexpr std::uint32_t k360Flash = 0x000803;  // WardShieldHit

		constexpr const char*   kLookDir = "magic\\Dynamic Wards\\";
		constexpr const char*   kLightsGlobal = "DynamicWardsLights";     // the dome lights (Colored ward lights)
		constexpr const char*   kHandGlobal = "DynamicWardsHandLight";    // the hand light (Ward casting light)
		constexpr const char*   kPresentGlobal = "DynamicWardsPresent";   // always 1: the light mods' ward lights step down
		constexpr std::uint32_t kArtChanged = 'DWAC';  // to RELight - Spell Addon: casting art moved, find the hand lights again

		struct Art
		{
			RE::BGSArtObject*       hand = nullptr;
			RE::BGSArtObject*       dome = nullptr;     // the vanilla-shaped dome; also the 360 dome while it is locked
			RE::BGSArtObject*       dome360 = nullptr;
			RE::BGSReferenceEffect* flash = nullptr;
		};

		enum class How
		{
			kTable,
			kFound,  // only the slots that held a ward's art are replaced
			kSilent,
		};

		struct Target
		{
			RE::EffectSetting* effect;
			std::size_t        row;
			How                how;
			bool               castWard, hitWard, enchWard;
			RE::BGSArtObject*  ownCast;
			RE::BGSArtObject*  ownHit;
			RE::BGSArtObject*  ownEnch;
			RE::TESObjectLIGH* ownLight;
			RE::TESObjectLIGH* takenLight = nullptr;  // what was there when the switch took it off (another mod's light survives)
			std::string        why;
		};

		struct Changes
		{
			std::size_t art = 0, light = 0, shield = 0;
		};

		std::mutex              gLock;
		std::map<std::string, Art, std::less<>> gLooks;  // "Row|Look"
		RE::TESGlobal*          gLightsGlobal = nullptr;
		RE::TESGlobal*          gHandGlobal = nullptr;
		RE::TESGlobal*          gPresentGlobal = nullptr;
		bool                    gLightPlacer = false;
		RE::BGSArtObject*       gEmpty = nullptr;
		RE::BGSReferenceEffect* gNoFlash = nullptr;
		std::vector<Target>     gTargets;
		std::size_t             gSkipped = 0;
		bool                    gHas360 = false;
		bool                    gUnlocked = true;
		std::size_t             gDressed = 0;
		std::vector<std::pair<RE::TESObjectARMO*, RE::EnchantmentItem*>> gShields;
		RE::EnchantmentItem*    gCrusaderEnch = nullptr;
		std::string             gLastApply = "never";
		std::string             gFlashTemplate = "none";
		std::array<const Art*, kRows> gRowArt{};  // what each row wears now, resolved once per apply
		std::unordered_map<const RE::EffectSetting*, std::size_t> gTargetOf;  // effect -> gTargets index
		std::unordered_set<const RE::BGSArtObject*> gOurs;  // every art object made here: a slot holding one was dressed by us

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

		std::size_t RowOf(std::string_view a_token)
		{
			for (std::size_t i = 0; i < kRows; ++i) {
				if (kTokens[i] == a_token) {
					return i;
				}
			}
			return kRows;
		}

		std::size_t RankRow(std::int32_t a_skill)
		{
			return a_skill >= 100 ? 4 : a_skill >= 75 ? 3 : a_skill >= 50 ? 2 : a_skill >= 25 ? 1 : 0;
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
			constexpr float kSkill[] = { 0.0f, 50.0f, 75.0f, 100.0f };
			const auto      i = static_cast<std::size_t>(rule);
			return i >= std::size(kSkill) || player->AsActorValueOwner()->GetBaseActorValue(RE::ActorValue::kRestoration) >= kSkill[i];
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
		// the look a ladder stop wears: white, the ladder's color, or "<Ladder> <percent>" between them (wardgen stop_look)
		std::string StopLook(std::string_view a_ladder, int a_percent)
		{
			if (a_percent <= 0) {
				return "White";
			}
			return a_percent >= 100 ? std::string(a_ladder) : std::format("{} {}", a_ladder, a_percent);
		}

		template <std::size_t N>
		int StopOf(const std::array<int, N>& a_stops, std::size_t a_rank, bool a_reversed)
		{
			const auto pos = (std::min)(a_rank, N - 1);  // ranks past the ladder hold its last stop
			return a_stops[a_reversed ? N - 1 - pos : pos];
		}

		int StopPercent(std::size_t a_rank)
		{
			const bool rev = LadderReversed();
			switch (LadderStages()) {
			case 5:
				return StopOf(kStages5, a_rank, rev);
			case 4:
				return StopOf(kStages4, a_rank, rev);
			default:
				return StopOf(kStages3, a_rank, rev);
			}
		}

		std::string LookFor(std::size_t a_row)
		{
			const auto pick = RowPick(a_row);
			if (pick == Pick::kVanilla) {
				return {};
			}
			if (pick == Pick::kPurple) {
				return "Purple";
			}
			if (pick != Pick::kDefault) {
				return std::string(kColorNames[static_cast<int>(pick) - 1]);
			}
			if (a_row >= kRanks) {
				return std::string(kRowDefault[a_row]);
			}
			return StopLook(kLadderNames[Ladder()], StopPercent(a_row));
		}

		const Art* ArtFor(std::size_t a_row)
		{
			const auto look = LookFor(a_row);
			if (look.empty()) {
				return nullptr;
			}
			const auto it = gLooks.find(std::format("{}|{}", kTokens[a_row], look));
			return it == gLooks.end() || !it->second.hand || !it->second.dome ? nullptr : &it->second;
		}

		// what a ward wears now: nothing for the silent effect, a Vanilla row, or a ward found by what it is while
		// "Color every ward found" is off
		const Art* Worn(const Target& a_t, bool a_every)
		{
			if (a_t.how == How::kSilent || (a_t.how == How::kFound && !a_every)) {
				return nullptr;
			}
			return gRowArt[a_t.row];
		}

		RE::TESGlobal* MakeGlobal(const char* a_id)
		{
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
		gHas360 = Loaded(dh, k360Plugin);
		gLightPlacer = REX::W32::GetModuleHandleA("po3_LightPlacer.dll") != nullptr;
		gLightsGlobal = MakeGlobal(kLightsGlobal);
		gHandGlobal = MakeGlobal(kHandGlobal);
		gPresentGlobal = MakeGlobal(kPresentGlobal);
		std::vector<std::string> looks(std::begin(kColorNames), std::end(kColorNames));
		looks.emplace_back("Purple");
		std::set<int> between;  // every stop a ladder can land on between white and its color
		for (const int p : kStages3) {
			between.insert(p);
		}
		for (const int p : kStages4) {
			between.insert(p);
		}
		for (const int p : kStages5) {
			between.insert(p);
		}
		for (const auto& ladder : kLadderNames) {
			for (const int p : between) {
				if (p > 0 && p < 100) {
					looks.push_back(StopLook(ladder, p));
				}
			}
		}
		std::size_t made = 0, missing = 0;
		for (std::size_t row = 0; row < kRows; ++row) {
			const auto token = kTokens[row];
			for (const auto& look : looks) {
				if (row >= kRanks && look.find(' ') != std::string::npos) {
					continue;  // a rankless row has no ladder, so no stops between
				}
				const auto name = std::format("{} {}.nif", token, look);
				const auto hand = std::format("{}wardinhandfx - {}", kLookDir, name);
				const auto dome = std::format("{}wardbodyfx - {}", kLookDir, name);
				if (!MeshExists(hand) || !MeshExists(dome)) {
					++missing;
					continue;
				}
				Art a{ MakeArtObject(hand), MakeArtObject(dome) };
				if (const auto d360 = std::format("{}wardbodyfx 360 - {}", kLookDir, name); MeshExists(d360)) {
					a.dome360 = MakeArtObject(d360);
				}
				if (const auto hit = std::format("{}wardshieldhitfx - {}", kLookDir, name); MeshExists(hit)) {
					a.flash = makeFlash(MakeArtObject(hit));
				}
				gLooks.emplace(std::format("{}|{}", token, look), a);
				++made;
			}
		}
		SKSE::log::info("art: {} look(s) made, {} missing; flash template {}; 360 Ward {}; Light Placer {}; globals {} {} {}", made, missing,
			gFlashTemplate, gHas360 ? "loaded" : "not loaded", gLightPlacer ? "loaded" : "not loaded", gLightsGlobal ? kLightsGlobal : "NOT made",
			gHandGlobal ? kHandGlobal : "NOT made", gPresentGlobal ? kPresentGlobal : "NOT made");
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
				table[id] = RowOf(r.token);
			}
		}
		const auto          silentId = dh->LookupFormID(kSilent.id, kSilent.file);
		std::vector<Target> found;
		std::size_t         skipped = 0;
		for (auto* eff : dh->GetFormArray<RE::EffectSetting>()) {
			if (!eff) {
				continue;
			}
			auto&      d = eff->data;
			Target     t{ eff, kRows, How::kFound, Contains(ModelOf(d.castingArt), kWardHand), Contains(ModelOf(d.hitEffectArt), kWardBody),
					Contains(ModelOf(d.enchantEffectArt), kWardBody), d.castingArt, d.hitEffectArt, d.enchantEffectArt, d.light, nullptr, {} };
			const bool power = d.primaryAV == RE::ActorValue::kWardPower;
			if (eff->GetFormID() == silentId) {
				t.how = How::kSilent;
				t.why = "ShieldConcSelf, silent";
			} else if (auto it = table.find(eff->GetFormID()); it != table.end()) {
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
				// kept whatever "Color every ward found" says, so the switch works without a restart (ApplyAll reads it)
				t.row = RankRow(d.minimumSkill);
				t.why = std::format("found by {}; minimum skill {} = {}", (t.castWard || t.hitWard || t.enchWard) ? (power ? "art + Ward Power" : "art") : "Ward Power",
					d.minimumSkill, kTokens[t.row]);
			}
			found.push_back(std::move(t));
		}
		std::vector<std::pair<RE::TESObjectARMO*, RE::EnchantmentItem*>> shields;
		RE::EnchantmentItem*                                              ench = nullptr;
		if (Loaded(dh, kCrusaderHub) && Loaded(dh, kKnights)) {
			if ((ench = dh->LookupForm<RE::EnchantmentItem>(kCrusaderEnch, kKnights))) {
				for (const auto id : kCrusaderShields) {
					if (auto* armo = dh->LookupForm<RE::TESObjectARMO>(id, kLegacy)) {
						shields.emplace_back(armo, armo->formEnchanting);
					}
				}
			}
		}
		std::scoped_lock l{ gLock };
		gTargets = std::move(found);
		gTargetOf.clear();
		for (std::size_t i = 0; i < gTargets.size(); ++i) {
			gTargetOf.emplace(gTargets[i].effect, i);
		}
		gSkipped = skipped;
		gShields = std::move(shields);
		gCrusaderEnch = ench;
		SKSE::log::info("wards: {} found ({} from the 1.0 table), {} left alone; Legacy Crusader shields {}", gTargets.size(),
			std::ranges::count_if(gTargets, [](const Target& a) { return a.how == How::kTable; }), gSkipped,
			gShields.empty() ? "not in this load order" : std::format("{} found", gShields.size()));
	}

	void ApplyAll(const char* a_why)
	{
		std::scoped_lock l{ gLock };
		gUnlocked = Unlocked();
		for (std::size_t r = 0; r < kRows; ++r) {
			gRowArt[r] = ArtFor(r);
		}
		const bool  light = WardLightOn();
		const bool  use360 = gHas360 && Dome360();
		const bool  every = EveryWard();
		Changes     c;
		std::size_t dressed = 0;
		for (auto& t : gTargets) {
			auto& d = t.effect->data;
			if (t.how == How::kSilent) {
				if (gEmpty) {
					Put(d.hitEffectArt, gEmpty, c.art);
					Put(d.castingArt, gEmpty, c.art);
				}
				continue;
			}
			const Art* a = Worn(t, every);
			if (!a) {
				Restore(d.castingArt, t.ownCast, c.art);
				Restore(d.hitEffectArt, t.ownHit, c.art);
				Restore(d.enchantEffectArt, t.ownEnch, c.art);
				continue;
			}
			RE::BGSArtObject* dome = (use360 && gUnlocked && a->dome360) ? a->dome360 : a->dome;
			const bool        table = t.how == How::kTable;
			if (table || t.castWard) {
				Put(d.castingArt, a->hand, c.art);
			}
			if (table || t.hitWard) {
				Put(d.hitEffectArt, dome, c.art);
			}
			if (!table && t.enchWard) {
				Put(d.enchantEffectArt, dome, c.art);
			}
			++dressed;
		}
		// the Addon points each ward's light at the one for its (new) hand art; the light switch below then has the last word
		if (c.art) {
			SKSE::GetMessagingInterface()->Dispatch(kArtChanged, nullptr, 0, nullptr);
		}
		for (auto& t : gTargets) {
			// HIS REPORT, 2026-09-24: a white flash on the first cast, a light that "starts strong then weakens", a hand light
			// too big beside his other casting art. The silent row fires with EVERY vanilla ward and kept the game's white
			// ward light, a second light on the hand; it never has one now. Under Light Placer a dressed ward's own light goes
			// too - our config lights the hand in the ward's color, behind the Ward casting light switch (kHandGlobal).
			// Under RE::Light the effect's light IS the colored hand light (RELight - Spell Addon points it), so the switch
			// decides it. A Vanilla row only gets back a light the switch took.
			if (t.how == How::kSilent) {
				ApplyLight(t, false, false, c.light);
				continue;
			}
			const bool vanilla = !Worn(t, every);
			ApplyLight(t, (light && !gLightPlacer) || vanilla, !vanilla, c.light);
		}
		for (auto& [armo, own] : gShields) {
			auto* want = (CrusaderOn() && gCrusaderEnch) ? gCrusaderEnch : own;
			if (armo->formEnchanting != want) {
				armo->formEnchanting = want;
				++c.shield;
			}
		}
		if (gLightsGlobal) {
			gLightsGlobal->value = ColoredLightsOn() ? 1.0f : 0.0f;
		}
		if (gHandGlobal) {
			gHandGlobal->value = light ? 1.0f : 0.0f;
		}
		if (gPresentGlobal) {
			gPresentGlobal->value = 1.0f;
		}
		gDressed = dressed;
		gLastApply = a_why ? a_why : "?";
		SKSE::log::info("apply ({}): {} dressed; changed {} art, {} light, {} shield; ward light {}, colored lights {}; dome {}; every ward {}",
			gLastApply, dressed, c.art, c.light, c.shield, light ? "on" : "off", ColoredLightsOn() ? "on" : "off",
			!use360 ? "vanilla" : gUnlocked ? "360 (unlocked)" : "360 (locked)", every ? "on" : "off");
	}

	void CheckUnlock(const char* a_why)
	{
		{
			std::scoped_lock l{ gLock };
			if (!gHas360 || !Dome360() || Unlocked() == gUnlocked) {
				return;
			}
		}
		ApplyAll(a_why);
	}

	bool Has360Ward()
	{
		std::scoped_lock l{ gLock };
		return gHas360;
	}

	bool LightPlacerLoaded()
	{
		std::scoped_lock l{ gLock };
		return gLightPlacer;
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

	RE::BGSReferenceEffect* FlashFor(RE::EffectSetting* a_effect)
	{
		if (!a_effect) {
			return nullptr;
		}
		std::scoped_lock l{ gLock };
		const auto       it = gTargetOf.find(a_effect);
		const Art*       a = it == gTargetOf.end() ? nullptr : Worn(gTargets[it->second], EveryWard());
		if (!a) {
			return nullptr;  // 360 Ward's own flash plays
		}
		return (gHas360 && Dome360() && gUnlocked && a->flash) ? a->flash : gNoFlash;  // the vanilla-shaped dome never flashed
	}

	bool PreviewRow(std::size_t a_row, float a_seconds)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		std::scoped_lock l{ gLock };
		const Art* a = a_row < kRows ? ArtFor(a_row) : nullptr;
		if (!player || !a) {
			return false;
		}
		auto* dome = (gHas360 && Dome360() && gUnlocked && a->dome360) ? a->dome360 : a->dome;
		return player->ApplyArtObject(dome, a_seconds) != nullptr;
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
		const bool       every = EveryWard();
		for (const auto& t : gTargets) {
			const auto& d = t.effect->data;
			const char* name = t.effect->GetFullName();
			wards += std::format(
				R"({}{{"effect":"{}","name":"{}","row":"{}","how":"{}","why":"{}","castingArt":"{}","hitEffectArt":"{}","enchantEffectArt":"{}",)"
				R"("ownCastingArt":"{}","ownHitEffectArt":"{}","light":"{}","ownLight":"{}","takenLight":"{}","dressed":{}}})",
				wards.empty() ? "" : ",", JsonEscape(Where(t.effect)), JsonEscape(name ? name : ""), t.row < kRows ? kTokens[t.row] : "silent",
				t.how == How::kTable ? "table" : t.how == How::kFound ? "found" : "silent", JsonEscape(t.why), JsonEscape(ModelOf(d.castingArt)),
				JsonEscape(ModelOf(d.hitEffectArt)), JsonEscape(ModelOf(d.enchantEffectArt)), JsonEscape(ModelOf(t.ownCast)),
				JsonEscape(ModelOf(t.ownHit)), lightId(d.light), lightId(t.ownLight), lightId(t.takenLight), Worn(t, every) != nullptr);
		}
		std::string rows;
		for (std::size_t i = 0; i < kRows; ++i) {
			rows += std::format(R"({}"{}":{{"pick":{},"look":"{}","installed":{}}})", rows.empty() ? "" : ",", kTokens[i],
				static_cast<int>(RowPick(i)), JsonEscape(LookFor(i)), ArtFor(i) != nullptr);
		}
		std::string shields;
		for (const auto& [armo, own] : gShields) {
			shields += std::format(R"({}{{"shield":"{}","enchantment":"{}","own":"{}"}})", shields.empty() ? "" : ",", JsonEscape(Where(armo)),
				JsonEscape(Where(armo->formEnchanting)), JsonEscape(Where(own)));
		}
		return std::format(
			R"({{"found":{},"dressed":{},"leftAlone":{},"lastApply":"{}","looks":{},"flashTemplate":"{}","has360Ward":{},"dome360":{},)"
			R"("unlocked360":{},"unlockRule":{},"unlockPerk":"{}","ladder":"{}","wardLight":{},"coloredLights":{},"lightsGlobal":{},)"
			R"("lightPlacer":{},"everyWard":{},"crusader":{},"rows":{{{}}},"shields":[{}],"wards":[{}]}})",
			gTargets.size(), gDressed, gSkipped, JsonEscape(gLastApply), gLooks.size(), JsonEscape(gFlashTemplate), gHas360, Dome360(),
			gUnlocked, static_cast<int>(UnlockRule()), JsonEscape(UnlockPerk()), kLadderNames[Ladder()], WardLightOn(), ColoredLightsOn(),
			gLightsGlobal ? std::format("{}", gLightsGlobal->value) : std::string("null"), gLightPlacer, EveryWard(), CrusaderOn(), rows,
			shields, wards);
	}
}
