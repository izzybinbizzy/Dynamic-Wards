// Dynamic Wards - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// The ward's strength as an add-on bar for Reticle Arcs - only its published interface (ReticleArcsAPI.h, Reticle Arcs by
// Chiktabba, https://codeberg.org/chiktabba/ReticleArcs, MIT, copied unchanged). Reticle Arcs draws the bar in the player's
// own look, asks for its state every frame and shows or hides it on its own settings page; this plugin has no setting for it.
// While a ward is up on the player (an active effect that raises Ward Power - the game's wards and every mod's), the bar is
// what the ward can still take: the Ward Power left of what it gave. It wears the ward's row color, or the player's when the
// row is Vanilla or the ward is not one of ours.

#include "Plugin.h"

// Reticle Arcs' own header (theirs, MIT) needs GetModuleHandleW / GetProcAddress; its warnings are not ours
#include <Windows.h>
#pragma warning(push, 0)
#if defined(__clang__)
#	pragma clang diagnostic push
#	pragma clang diagnostic ignored "-Weverything"
#endif
#include "ReticleArcsAPI.h"
#if defined(__clang__)
#	pragma clang diagnostic pop
#endif
#pragma warning(pop)

namespace
{
	using ReticleArcsAPI::BarState;

	// the ward up on the player now (its base effect), or nullptr
	[[nodiscard]] const RE::EffectSetting* WardUp(RE::PlayerCharacter& a_player)
	{
		auto* list = a_player.AsMagicTarget()->GetActiveEffectList();
		if (!list) {
			return nullptr;
		}
		using Flag = RE::ActiveEffect::Flag;
		for (auto* ae : *list) {
			const auto* base = ae && ae->effect && ae->flags.none(Flag::kInactive, Flag::kDispelled) ? ae->GetBaseObject() : nullptr;
			if (base && base->data.primaryAV == RE::ActorValue::kWardPower && !base->IsDetrimental()) {
				return base;
			}
		}
		return nullptr;
	}

	void Fill(BarState& a_state, float a_percent)
	{
		a_state.active = true;
		a_state.drain = true;  // empties toward its start and follows the value as it falls
		a_state.percent = std::clamp(a_percent, 0.0f, 1.0f);
		a_state.low = a_state.percent < 0.25f;
		a_state.rim = a_state.low;
		a_state.alarm = a_state.percent <= 0.0f;
	}

	// the ward's row color, read again only when the ward changes or once a second: the frame waits for Update (main thread)
	struct RowColorSeen
	{
		const RE::EffectSetting*     ward{ nullptr };
		float                        age{ 0.0f };
		std::optional<Plugin::Color> color;
	};
	RowColorSeen gSeen;

	// Reticle Arcs calls this on the main thread every frame
	void Update(float a_delta, BarState& a_state)
	{
		auto*       player = RE::PlayerCharacter::GetSingleton();
		const auto* ward = player ? WardUp(*player) : nullptr;
		if (!ward) {
			return;
		}
		const float left = player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kWardPower);
		// hits come off the ward as a damage modifier (negative): the ward gave what is left plus what was taken
		const float taken = -player->GetActorValueModifier(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kWardPower);
		const float gave = left + std::max(taken, 0.0f);
		Fill(a_state, gave > 0.0f ? left / gave : 0.0f);
		gSeen.age += std::max(a_delta, 0.0f);
		if (ward != gSeen.ward || gSeen.age > 1.0f) {
			const auto row = Plugin::WardRowOf(ward);
			gSeen = { ward, 0.0f, row < Plugin::kRows ? Plugin::RowColor(row) : std::nullopt };
		}
		if (gSeen.color) {
			a_state.useColor = true;
			a_state.color = *gSeen.color & 0xFFFFFF;
		}
	}

	// Reticle Arcs' settings page: a ward taking five hits, broken a moment, then raised again
	void Preview(float a_time, BarState& a_state)
	{
		const float t = std::fmod(std::max(a_time, 0.0f) / 3.0f, 1.0f);
		Fill(a_state, t < 0.7f ? 1.0f - std::floor(t / 0.14f) * 0.2f : 0.0f);
		a_state.active = t < 0.9f;
	}
}

namespace Plugin
{
	void RegisterArcBar()
	{
		if (!GetModuleHandleW(L"ReticleArcs.dll")) {
			return;
		}
		// the bar needs interface 1 only: asked for that, it keeps working with older Reticle Arcs releases
		auto* arcs = ReticleArcsAPI::Request(1);
		if (!arcs) {
			SKSE::log::info("Reticle Arcs is too old for add-on bars; the ward bar is not offered to it");
			return;
		}
		ReticleArcsAPI::BarDefinition bar;
		bar.id = "DynamicWardsStrength";
		bar.name = "Ward strength";
		bar.tooltip = "How much your ward can still take before it breaks.";
		bar.placement = "bottom";
		bar.update = Update;
		bar.preview = Preview;
		const bool taken = arcs->RegisterBar(bar);
		SKSE::log::info("Reticle Arcs (interface {}) {} the ward bar", arcs->GetVersion(), taken ? "took" : "refused");
	}
}
