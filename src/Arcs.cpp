// Dynamic Wards - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// The ward's strength as an add-on bar for Reticle Arcs - only its published interface (ReticleArcsAPI.h, Reticle Arcs by
// Chiktabba, https://codeberg.org/chiktabba/ReticleArcs, MIT, copied unchanged). Reticle Arcs draws the bar in the player's
// own look, asks for its state every frame and shows or hides it on its own settings page. While a ward is up on the player
// (an active effect that raises Ward Power - the game's wards and every mod's, not a passive bonus), the bar is what the ward
// can still take: the Ward Power left of what it gave. It wears the ward's row color, or the player's when the row is
// Vanilla or the ward is not one of ours. The plugin's one setting for it (its Settings page) shows it for an always-on ward
// that wears a dome of ours too.

#include "Plugin.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>

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

	// a constant effect that asks nothing (a ring's or a perk's Ward Power bonus) is always on: no ward that is raised and breaks
	[[nodiscard]] bool Passive(const RE::ActiveEffect& a_ae, const RE::EffectSetting& a_base)
	{
		return a_ae.spell && a_ae.spell->GetCastingType() == RE::MagicSystem::CastingType::kConstantEffect && !a_ae.effect->conditions.head &&
		       !a_base.conditions.head;
	}

	// the ward up on the player now (its base effect), or nullptr. An always-on ward counts only with the menu's switch on and
	// only when it wears a dome of ours (Reliquary of Myth's Staff of Magnus while held, its always-on Spellbreaker)
	[[nodiscard]] const RE::EffectSetting* WardUp(RE::PlayerCharacter& a_player)
	{
		auto* list = a_player.AsMagicTarget()->GetActiveEffectList();
		if (!list) {
			return nullptr;
		}
		using Flag = RE::ActiveEffect::Flag;
		const bool               alwaysOn = Plugin::AlwaysOnBar();
		const RE::EffectSetting* dome = nullptr;
		for (auto* ae : *list) {
			const auto* base = ae && ae->effect && ae->flags.none(Flag::kInactive, Flag::kDispelled) ? ae->GetBaseObject() : nullptr;
			if (!base || base->data.primaryAV != RE::ActorValue::kWardPower || base->IsDetrimental()) {
				continue;
			}
			if (!Passive(*ae, *base)) {
				return base;  // a cast ward wins over an always-on dome listed before it
			}
			if (!dome && alwaysOn && Plugin::AlwaysOnDome(base)) {
				dome = base;
			}
		}
		return dome;
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

	// the ward up and its row color, read again only when what her wards give changes, or the colors or the bar's switch do
	const RE::EffectSetting*     gWard{ nullptr };
	std::optional<Plugin::Color> gColor;
	float                        gGave{ 0.0f };
	std::atomic_bool             gStale{ true };

	// what the bar showed last frame, for devbench's report
	std::atomic_bool           gRegistered{ false };
	std::atomic_bool           gShown{ false };
	std::atomic<float>         gPercent{ 0.0f };
	std::atomic<std::uint64_t> gAsked{ 0 };  // how often Reticle Arcs asked (it skips kill moves, killcams and paused menus)

	// Reticle Arcs calls this on the main thread every frame and its interface has no way to stop it (ReticleArcsAPI.h): two values are read here
	void Update(float, BarState& a_state)
	{
		Plugin::Guarded("reticle arcs bar", [&]() {
			auto* player = RE::PlayerCharacter::GetSingleton();
			++gAsked;
			gShown = false;
			const float left = player ? player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kWardPower) : 0.0f;
			// hits come off the ward as a damage modifier (negative): the ward gave what is left plus what was taken
			const float taken = player ? -player->GetActorValueModifier(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kWardPower) : 0.0f;
			const float gave = left + std::max(taken, 0.0f);
			// her effect list is walked only when a ward starts or ends: a hit moves what is left and what was taken, never their sum
			const bool stale = gStale.exchange(false);
			// no ward up and nothing changed: the idle path, two values read and nothing timed (the meter counts real work only)
			if (!stale && gave == gGave && !gWard) {
				return;
			}
			const FrameCost::Scope cost{ 6, "Reticle Arcs' ward bar (each frame)" };
			if (stale || gave != gGave) {
				gGave = gave;
				gWard = gave > 0.0f ? WardUp(*player) : nullptr;
				const auto row = gWard ? Plugin::WardRowOf(gWard) : Plugin::kRows;
				gColor = row < Plugin::kRows ? Plugin::RowColor(row) : std::nullopt;
			}
			if (!gWard) {
				return;
			}
			Fill(a_state, left / gave);
			if (gColor) {
				a_state.useColor = true;
				a_state.color = *gColor & 0xFFFFFF;
			}
			gShown = a_state.active;
			gPercent = a_state.percent;
		});
	}

	// Reticle Arcs' settings page: a ward taking five hits, broken a moment, then raised again
	void Preview(float a_time, BarState& a_state)
	{
		Plugin::Guarded("reticle arcs preview", [&]() {
			const float t = std::fmod(std::max(a_time, 0.0f) / 3.0f, 1.0f);
			Fill(a_state, t < 0.7f ? 1.0f - std::floor(t / 0.14f) * 0.2f : 0.0f);
			a_state.active = t < 0.9f;
		});
	}
}

namespace Plugin
{
	void RegisterArcBar()
	{
		// the bar needs interface 1 only: asked for that, it keeps working with older Reticle Arcs releases
		auto* arcs = ReticleArcsAPI::Request(1);
		if (!arcs) {
			SKSE::log::info("Reticle Arcs is not installed, or too old for add-on bars: no ward bar");
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
		gRegistered = taken;
		SKSE::log::info("Reticle Arcs (interface {}) {} the ward bar", arcs->GetVersion(), taken ? "took" : "refused");
	}

	void ArcBarStale() noexcept
	{
		gStale = true;
	}

	bool ArcBarRegistered()
	{
		return gRegistered;
	}

	nlohmann::json ArcBarReport()
	{
		return { { "registered", gRegistered.load() }, { "shown", gShown.load() }, { "percent", gPercent.load() }, { "alwaysOnBar", AlwaysOnBar() },
			{ "asked", gAsked.load() } };
	}
}
