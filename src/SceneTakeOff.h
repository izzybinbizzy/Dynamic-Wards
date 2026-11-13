// SceneTakeOff - a light of ours out of the scene, the way that leaves no promoted shadow caster behind
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file and the notice at the top of main.cpp.
//
// Each plugin carries its own copy of this file, kept byte for byte the master's in Tools\Shared Plugin Code\ by
// PC Runner\shared code sync.py. The same removal as LightKit::TakeOff, for the plugins that carry no LightKit.h.

#pragma once

#include "PCH.h"

#include <algorithm>

namespace SceneTakeOff
{
	// Off the scene by its NiLight, the engine's own removal: a light Open Shaders' Promote made a shadow caster stays when removed
	// by its BSLight. One still in the add queue, which the lookup by NiLight does not search, goes by its BSLight. Main thread.
	inline void TakeOff(RE::ShadowSceneNode* a_scene, const RE::NiPointer<RE::BSLight>& a_bs)
	{
		if (!a_scene || !a_bs) {
			return;
		}
		bool queued = false;
		{
			auto&                     data = a_scene->GetRuntimeData();
			const RE::BSSpinLockGuard guard(data.lightQueueLock);
			queued = std::ranges::any_of(data.lightQueueAdd, [&](const RE::NiPointer<RE::BSLight>& a_one) { return a_one.get() == a_bs.get(); });
		}
		if (auto* light = a_bs->light.get(); light && !queued) {
			a_scene->RemoveLight(light);
		} else {
			a_scene->RemoveLight(a_bs);
		}
	}
}
