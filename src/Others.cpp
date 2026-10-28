// Dynamic Wards - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// Other casters' own wards. A ward's art is set on its effect record, so every caster wears the same art. An art effect is
// made per caster (ModelReferenceEffect), and its Init loads the art's model - so a caster who is not the player is given
// the art of his look there, before the model is loaded. Nothing is swapped on screen.
// His look: a follower always the Followers look; else the first faction group he is in that has a pick; else Enemies
// (hostile to the player) or Neutral. A look keeps the player's art, wears its own color, or the game's own art.

#include "Plugin.h"

#include <nlohmann/json.hpp>

namespace Plugin
{
	namespace
	{
		enum class Kind : std::size_t
		{
			kHand,
			kDome,
			kDome360,
			kWornHand,
			kWornDome,
			kCount
		};
		constexpr auto kKinds = static_cast<std::size_t>(Kind::kCount);

		struct Ours
		{
			std::size_t row{ kRows };
			Kind        kind{ Kind::kHand };
		};

		struct FactionGroup
		{
			RE::FormID       id{ 0 };
			std::string_view plugin;
		};
		// the faction looks (kLookTokens from kGroups on, their names in Menu.cpp), in tie-break order: a caster in two follows the
		// first one with a pick
		constexpr std::array<FactionGroup, kLooks - kGroups> kFactionGroups{ {
			{ 0x01F259, "Skyrim.esm" },
			{ 0x039F26, "Skyrim.esm" },
			{ 0x02584B, "Skyrim.esm" },
			{ 0x0B3292, "Skyrim.esm" },
			{ 0x02BF9A, "Skyrim.esm" },
			{ 0x02BF9B, "Skyrim.esm" },
			{ 0x034FBC, "Dragonborn.esm" },
			{ 0x106643, "Skyrim.esm" },
			{ 0x043AB7, "Skyrim.esm" },
			{ 0x02EB13, "Skyrim.esm" },
			{ 0x043599, "Skyrim.esm" },
			{ 0x04359E, "Skyrim.esm" },
			{ 0x034B74, "Skyrim.esm" },
			{ 0x043597, "Skyrim.esm" },
			{ 0x02997E, "Skyrim.esm" },
			{ 0x02430D, "Skyrim.esm" },
			{ 0x01BCC0, "Skyrim.esm" },
			{ 0x026724, "Skyrim.esm" },
		} };

		using ArtPair = std::pair<const RE::BGSArtObject*, RE::BGSArtObject*>;

		// built at data load (MakeArt), read-only after: no lock per art effect
		std::unordered_map<const RE::BGSArtObject*, Ours>         gOurs;
		std::array<std::array<RE::BGSArtObject*, kKinds>, kLooks> gLookArt{};
		std::array<RE::TESFaction*, kLooks>                       gFactions{};
		// what the game had: every apply (Wards.cpp ApplyLocked, one writer) fills gNext and swaps it in whole under gLock, a leaf
		// lock - an art effect made meanwhile reads the last apply's, never a half-filled one
		struct Theirs
		{
			std::unordered_map<const RE::EffectSetting*, std::vector<ArtPair>>                                  of;     // our art -> the effect's own (the empty art for none)
			std::unordered_map<const RE::BGSArtObject*, RE::BGSArtObject*>                                      first;  // the same, the first effect's: no effect known
			std::unordered_map<const RE::EffectSetting*, std::vector<std::pair<const RE::BGSArtObject*, Ours>>> game;   // a Vanilla row's own art, per ward
			std::unordered_map<const RE::EffectSetting*, Color>                                                 light;  // the light the effect's own record carried
		};
		std::mutex               gLock;
		Theirs                   gTheirs;
		Theirs                   gNext;
		std::atomic_bool         gAnyGame{ false };
		std::atomic_int          gTest{ 0 };  // devbench's others: 1 = every other caster wears the game's art
		std::atomic<std::size_t> gSwapped{ 0 };
		std::atomic<std::size_t> gHandSwapped{ 0 };
		std::atomic_bool         gHandLate{ false };

		// the ward an art effect belongs to: a dome through its active effect; the hand's casting art has none, so the spell the
		// caster holds whose effect wears that art
		const RE::EffectSetting* EffectOf(const RE::ModelReferenceEffect* a_self, RE::TESObjectREFR* a_ref)
		{
			if (const auto* c = skyrim_cast<RE::ActiveEffectReferenceEffectController*>(a_self->controller); c && c->effect && c->effect->effect) {
				return c->effect->effect->baseEffect;
			}
			auto* actor = a_ref ? a_ref->As<RE::Actor>() : nullptr;
			if (!actor) {
				return nullptr;
			}
			for (auto* caster : actor->GetActorRuntimeData().magicCasters) {
				const auto* spell = caster ? caster->currentSpell : nullptr;
				if (!spell) {
					continue;
				}
				for (auto* e : spell->effects) {
					if (e && e->baseEffect && e->baseEffect->data.castingArt == a_self->artObject) {
						return e->baseEffect;
					}
				}
			}
			return nullptr;
		}

		RE::BGSArtObject* GameArt(const RE::EffectSetting* a_effect, const RE::BGSArtObject* a_art)
		{
			std::scoped_lock l{ gLock };
			if (const auto mine = gTheirs.of.find(a_effect); mine != gTheirs.of.end()) {
				if (const auto it = std::ranges::find(mine->second, a_art, &ArtPair::first); it != mine->second.end()) {
					return it->second;
				}
			}
			const auto it = gTheirs.first.find(a_art);
			return it != gTheirs.first.end() ? it->second : nullptr;
		}

		// a Vanilla row's own art of this very ward (the game's ward art is shared: only the effect tells whose it is)
		std::optional<Ours> GameOurs(const RE::EffectSetting* a_effect, const RE::BGSArtObject* a_art)
		{
			std::scoped_lock l{ gLock };
			const auto       mine = gTheirs.game.find(a_effect);
			if (mine == gTheirs.game.end()) {
				return std::nullopt;
			}
			const auto it = std::ranges::find(mine->second, a_art, &std::pair<const RE::BGSArtObject*, Ours>::first);
			return it != mine->second.end() ? std::optional(it->second) : std::nullopt;
		}

		// a look the build shipped whole: its hand, its dome and both looping copies (the always-on wards wear those - the cast
		// meshes in their place show only the idle centre). A part-made look is not worn; ResolveFactions names it
		bool LookInstalled(std::size_t a_look)
		{
			if (a_look >= kLooks) {
				return false;
			}
			const auto& a = gLookArt[a_look];
			return std::ranges::all_of(std::array{ Kind::kHand, Kind::kDome, Kind::kWornHand, Kind::kWornDome },
				[&](Kind a_kind) { return a[static_cast<std::size_t>(a_kind)] != nullptr; });
		}

		// a look's art of a kind: the 360 dome falls back to its dome
		RE::BGSArtObject* LookArt(std::size_t a_look, Kind a_kind)
		{
			const auto& a = gLookArt[a_look];
			if (auto* art = a[static_cast<std::size_t>(a_kind)]) {
				return art;
			}
			return a_kind == Kind::kDome360 ? a[static_cast<std::size_t>(Kind::kDome)] : nullptr;
		}

		std::size_t LookOfCaster(RE::Actor* a_actor, const std::array<LookPick, kLooks>& a_looks)
		{
			std::size_t faction = kLooks;
			for (std::size_t i = kGroups; i < kLooks && faction == kLooks; ++i) {
				if (gFactions[i] && a_looks[i].mode != LookMode::kDefault && a_actor->IsInFaction(gFactions[i])) {
					faction = i;
				}
			}
			auto* player = RE::PlayerCharacter::GetSingleton();
			return LookFor(a_actor->IsPlayerTeammate(), player && a_actor->IsHostileToActor(player), faction);
		}

		struct InitHook
		{
			static bool thunk(RE::ModelReferenceEffect* a_self)
			{
				// the caster's look is read here, on whichever thread the engine starts an art effect
				static std::array<std::atomic_bool, 2> seen{};
				NoteThread(seen, "other casters: an art effect started");
				Guarded("other casters' art", [a_self]() {
					if (!a_self->artObject) {
						return;
					}
					const auto               ref = a_self->target.get();
					const RE::EffectSetting* effect = nullptr;
					std::optional<Ours>      ours;
					if (const auto it = gOurs.find(a_self->artObject); it != gOurs.end()) {
						ours = it->second;
					} else if (gAnyGame && ref && ref.get() != RE::PlayerCharacter::GetSingleton()) {
						effect = EffectOf(a_self, ref.get());
						ours = effect ? GameOurs(effect, a_self->artObject) : std::nullopt;
					}
					if (!ours) {
						return;
					}
					const auto        slot = TheirSlot(ref.get(), ours->row);
					RE::BGSArtObject* swap = nullptr;
					if (slot >= kSlots) {
						swap = GameArt(effect ? effect : EffectOf(a_self, ref.get()), a_self->artObject);
					} else if (slot >= kRows) {
						swap = LookArt(slot - kRows, ours->kind);
					}
					if (swap && swap != a_self->artObject) {
						a_self->artObject = swap;
						if (gSwapped.fetch_add(1) < 20) {
							SKSE::log::info("other casters: {:08X} wears {}", ref->GetFormID(), slot >= kSlots ? std::string_view("the game's own ward art") : SlotToken(slot));
						}
					}
				});
				return func(a_self);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	// The one place a caster's ward is decided: the art hook above and the hand lights (DomeLights.cpp) both ask it, so the two
	// never disagree. The player's row for the player and for a look that keeps the player's look; kRows + the look for a look
	// with its own color (when its meshes are installed); kSlots for the game's own art.
	std::size_t TheirSlot(RE::TESObjectREFR* a_caster, std::size_t a_row)
	{
		if (!a_caster || a_caster == RE::PlayerCharacter::GetSingleton() || a_row >= kRows) {
			return a_row;
		}
		if (gTest.load() == 1) {
			return kSlots;
		}
		auto* actor = a_caster->As<RE::Actor>();
		if (!actor) {
			return a_row;
		}
		const auto looks = Looks();
		const auto look = LookOfCaster(actor, looks);
		switch (looks[look].mode) {
		case LookMode::kVanilla:
			return kSlots;
		case LookMode::kCustom:
			return LookInstalled(look) ? kRows + look : a_row;
		default:
			return a_row;
		}
	}

	// Another caster's ward IN THE HAND wears his look from the start. His magic caster attaches its casting art itself
	// (CheckAttachCastingArt, the first call of its update - never an art effect, so InitHook never sees it): the hand hook calls
	// this before that update, and while nothing is loading or attached yet the caster is handed the art his look wears. A model
	// already loading stays as it is (logged once). Any thread: no allocation; the tables are read-only after data load, GameArt
	// takes gLock
	void LookHandArt(RE::ActorMagicCaster* a_caster)
	{
		auto* art = a_caster ? a_caster->castingArt : nullptr;
		if (!art || a_caster->castingArtData.attachedArt) {
			return;
		}
		const auto  ours = gOurs.find(art);
		auto*       actor = a_caster->actor;
		const auto* spell = a_caster->currentSpell;
		if (ours == gOurs.end() || !actor || actor->IsPlayerRef() || !spell) {
			return;
		}
		// the player's look keeps the art he holds; a look of its own wears its art when installed; Vanilla the game's
		const auto        slot = TheirSlot(actor, ours->second.row);
		RE::BGSArtObject* look = slot >= kRows && slot < kSlots ? LookArt(slot - kRows, ours->second.kind) : nullptr;
		if (slot < kSlots && !look) {
			return;
		}
		if (a_caster->cloneTask) {
			if (!gHandLate.exchange(true)) {
				SKSE::log::info("other casters: a ward in the hand was already loading the player's art - left as it is (logged once)");
			}
			return;
		}
		RE::BGSArtObject* swap = look;
		if (!look) {
			const RE::EffectSetting* effect = nullptr;
			for (const auto* e : spell->effects) {
				if (e && e->baseEffect && e->baseEffect->data.castingArt == art) {
					effect = e->baseEffect;
					break;
				}
			}
			auto* game = effect ? GameArt(effect, art) : nullptr;
			if (!game || game == art) {
				return;
			}
			const char* model = game->GetModel();
			swap = model && *model ? game : nullptr;  // the effect had no art of its own: none, as the game draws it
		}
		a_caster->castingArt = swap;
		if (gHandSwapped.fetch_add(1) < 20) {
			SKSE::log::info("other casters: {:08X}'s ward in the hand wears {}", actor->GetFormID(), look ? "his look's art" : "the game's own casting art");
		}
	}

	void InstallOthers()
	{
		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_ModelReferenceEffect[0] };
		InitHook::func = vtbl.write_vfunc(0x36, InitHook::thunk);
		SKSE::log::info("other casters: the art effect hook is in");
	}

	void NoteSlotArt(std::size_t a_slot, RE::BGSArtObject* a_hand, RE::BGSArtObject* a_dome, RE::BGSArtObject* a_dome360,
		RE::BGSArtObject* a_wornHand, RE::BGSArtObject* a_wornDome)
	{
		if (a_slot >= kRows && a_slot < kSlots) {
			gLookArt[a_slot - kRows] = { a_hand, a_dome, a_dome360, a_wornHand, a_wornDome };
			return;
		}
		if (a_slot >= kRows) {
			return;
		}
		// a shield row's meshes are looping copies already: another caster's shield ward wears his look's worn pair
		const bool                               looping = std::ranges::find(kShieldRows, a_slot) != kShieldRows.end();
		const std::pair<RE::BGSArtObject*, Kind> arts[] = { { a_hand, looping ? Kind::kWornHand : Kind::kHand },
			{ a_dome, looping ? Kind::kWornDome : Kind::kDome }, { a_dome360, Kind::kDome360 }, { a_wornHand, Kind::kWornHand },
			{ a_wornDome, Kind::kWornDome } };
		for (const auto& [art, kind] : arts) {
			if (art) {
				gOurs.try_emplace(art, Ours{ a_slot, kind });
			}
		}
	}

	void ResolveFactions()
	{
		auto*       dh = RE::TESDataHandler::GetSingleton();
		std::size_t found = 0;
		for (std::size_t i = 0; i < kFactionGroups.size() && dh; ++i) {
			gFactions[kGroups + i] = dh->LookupForm<RE::TESFaction>(kFactionGroups[i].id, kFactionGroups[i].plugin);
			if (gFactions[kGroups + i]) {
				++found;
			}
		}
		std::size_t installed = 0;
		for (std::size_t i = 0; i < kLooks; ++i) {
			if (LookInstalled(i)) {
				++installed;
			} else if (std::ranges::any_of(gLookArt[i], [](const auto* a_art) { return a_art != nullptr; })) {
				SKSE::log::warn("other casters: the {} look is missing some of its meshes - its casters wear the player's look", kLookTokens[i]);
			}
		}
		SKSE::log::info("other casters: {} of {} faction group(s) found, {} of {} look(s) installed", found, kFactionGroups.size(), installed, kLooks);
	}

	bool FactionLoaded(std::size_t a_look)
	{
		return a_look < kGroups || (a_look < kLooks && gFactions[a_look]);
	}

	bool LookArtInstalled(std::size_t a_look)
	{
		return LookInstalled(a_look);
	}

	void NoteTheirArt(const RE::EffectSetting* a_effect, const RE::BGSArtObject* a_ours, RE::BGSArtObject* a_theirs, RE::BGSArtObject* a_empty)
	{
		auto* had = a_theirs ? a_theirs : a_empty;
		if (!a_effect || !a_ours || !had || a_ours == had) {
			return;
		}
		auto& mine = gNext.of[a_effect];
		if (std::ranges::find(mine, a_ours, &ArtPair::first) == mine.end()) {
			mine.emplace_back(a_ours, had);
		}
		// no effect known (the hand's casting art): only real art, so one effect that had none never empties every caster's hands
		if (a_theirs) {
			gNext.first.try_emplace(a_ours, a_theirs);
		}
	}

	void NoteGameArt(const RE::EffectSetting* a_effect, const RE::BGSArtObject* a_theirs, std::size_t a_row, bool a_hand, bool a_looping, bool a_360)
	{
		if (a_effect && a_theirs && a_row < kRows && !gOurs.contains(a_theirs)) {
			const Kind kind = a_hand    ? (a_looping ? Kind::kWornHand : Kind::kHand) :
			                  a_looping ? Kind::kWornDome :
			                  a_360     ? Kind::kDome360 :
			                              Kind::kDome;
			gNext.game[a_effect].emplace_back(a_theirs, Ours{ a_row, kind });
		}
	}

	void NoteTheirLight(const RE::EffectSetting* a_effect, const RE::TESObjectLIGH* a_light)
	{
		if (a_effect && a_light) {
			const auto& c = a_light->data.color;
			gNext.light[a_effect] = (static_cast<Color>(c.red) << 16) | (static_cast<Color>(c.green) << 8) | c.blue;
		}
	}

	void PublishTheirs()
	{
		std::scoped_lock l{ gLock };
		gAnyGame = !gNext.game.empty();
		gTheirs = std::exchange(gNext, {});
	}

	std::optional<Color> GameLightColor(const RE::EffectSetting* a_effect)
	{
		std::scoped_lock l{ gLock };
		const auto       it = gTheirs.light.find(a_effect);
		return it != gTheirs.light.end() ? std::optional(it->second) : std::nullopt;
	}

	void SetOthersTest(int a_value)
	{
		gTest = a_value;
	}

	nlohmann::json OthersReport()
	{
		const auto looks = Looks();
		auto       out = nlohmann::json::object();
		for (std::size_t i = 0; i < kLooks; ++i) {
			const auto& p = looks[i];
			const char* mode = p.mode == LookMode::kCustom ? "custom" : p.mode == LookMode::kVanilla ? "vanilla" :
			                                                        p.mode == LookMode::kDefault     ? "default" :
			                                                                                           "mine";
			out[std::string(kLookTokens[i])] = { { "mode", mode }, { "color", HexColor(p.color) }, { "installed", LookInstalled(i) }, { "loaded", FactionLoaded(i) } };
		}
		return { { "test", gTest.load() }, { "swapped", gSwapped.load() }, { "handSwapped", gHandSwapped.load() }, { "handLate", gHandLate.load() },
			{ "looks", std::move(out) } };
	}
}
