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

		// built at data load (MakeArt), read-only after: no lock per art effect
		std::unordered_map<const RE::BGSArtObject*, Ours>              gOurs;
		std::array<std::array<RE::BGSArtObject*, kKinds>, kLooks>      gLookArt{};
		std::array<RE::TESFaction*, kLooks>                            gFactions{};
		std::mutex                                                     gLock;
		std::unordered_map<const RE::BGSArtObject*, RE::BGSArtObject*> gTheirs;     // our art -> the game's own art it replaced
		std::atomic_int                                                gTest{ 0 };  // devbench's others: 1 = every other caster wears the game's art
		std::atomic<std::size_t>                                       gSwapped{ 0 };

		RE::BGSArtObject* GameArt(const RE::BGSArtObject* a_art)
		{
			std::scoped_lock l{ gLock };
			const auto       it = gTheirs.find(a_art);
			return it != gTheirs.end() ? it->second : nullptr;
		}

		bool LookInstalled(std::size_t a_look)
		{
			return a_look < kLooks && gLookArt[a_look][static_cast<std::size_t>(Kind::kHand)] && gLookArt[a_look][static_cast<std::size_t>(Kind::kDome)];
		}

		// a look's art of a kind: the 360 dome falls back to its dome, the worn copies to the cast ones
		RE::BGSArtObject* LookArt(std::size_t a_look, Kind a_kind)
		{
			const auto& a = gLookArt[a_look];
			if (auto* art = a[static_cast<std::size_t>(a_kind)]) {
				return art;
			}
			switch (a_kind) {
			case Kind::kDome360:
			case Kind::kWornDome:
				return a[static_cast<std::size_t>(Kind::kDome)];
			case Kind::kWornHand:
				return a[static_cast<std::size_t>(Kind::kHand)];
			default:
				return nullptr;
			}
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
			static void thunk(RE::ModelReferenceEffect* a_self)
			{
				Guarded("other casters' art", [a_self]() {
					if (!a_self->artObject || !gOurs.contains(a_self->artObject)) {
						return;
					}
					const auto        ours = gOurs.find(a_self->artObject)->second;
					const auto        ref = a_self->target.get();
					const auto        slot = TheirSlot(ref.get(), ours.row);
					RE::BGSArtObject* swap = nullptr;
					if (slot >= kSlots) {
						swap = GameArt(a_self->artObject);
					} else if (slot >= kRows) {
						swap = LookArt(slot - kRows, ours.kind);
					}
					if (swap && swap != a_self->artObject) {
						a_self->artObject = swap;
						if (gSwapped.fetch_add(1) < 20) {
							SKSE::log::info("other casters: {:08X} wears {}", ref->GetFormID(), slot >= kSlots ? std::string_view("the game's own ward art") : SlotToken(slot));
						}
					}
				});
				func(a_self);
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

	void NoteTheirArt(const RE::BGSArtObject* a_ours, RE::BGSArtObject* a_theirs)
	{
		if (!a_ours || !a_theirs || a_ours == a_theirs) {
			return;
		}
		std::scoped_lock l{ gLock };
		gTheirs.try_emplace(a_ours, a_theirs);
	}

	void SetOthersTest(int a_value)
	{
		gTest = a_value;
	}

	std::string OthersReport()
	{
		const auto  looks = Looks();
		std::string out;
		for (std::size_t i = 0; i < kLooks; ++i) {
			const auto& p = looks[i];
			const char* mode = p.mode == LookMode::kCustom ? "custom" : p.mode == LookMode::kVanilla ? "vanilla" :
			                                                        p.mode == LookMode::kDefault     ? "default" :
			                                                                                           "mine";
			out += std::format(R"({}"{}":{{"mode":"{}","color":"{}","installed":{},"loaded":{}}})", out.empty() ? "" : ",", kLookTokens[i], mode,
				HexColor(p.color), LookInstalled(i), FactionLoaded(i));
		}
		return std::format(R"({{"test":{},"swapped":{},"looks":{{{}}}}})", gTest.load(), gSwapped.load(), out);
	}
}
