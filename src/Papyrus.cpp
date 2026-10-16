// Dynamic Wards - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.

#include "Plugin.h"

namespace Plugin
{
	namespace
	{
		// called by the Papyrus machine (360 Ward's SphereWard script): nothing thrown may leave into it - none = 360 Ward's own flash
		RE::BGSReferenceEffect* PapyrusFlashFor(RE::StaticFunctionTag*, RE::EffectSetting* a_effect)
		{
			RE::BGSReferenceEffect* flash = nullptr;
			Guarded("DynamicWards.FlashFor", [&]() { flash = FlashFor(a_effect); });
			return flash;
		}
	}

	bool RegisterPapyrus(RE::BSScript::IVirtualMachine* a_vm)
	{
		if (!a_vm) {
			return false;
		}
		a_vm->RegisterFunction("FlashFor", "DynamicWards", PapyrusFlashFor);
		SKSE::log::info("Papyrus: DynamicWards.FlashFor registered");
		return true;
	}
}
