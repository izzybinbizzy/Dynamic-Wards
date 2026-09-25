// Dynamic Wards - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.

#include "Plugin.h"

namespace Plugin
{
	namespace
	{
		RE::BGSReferenceEffect* PapyrusFlashFor(RE::StaticFunctionTag*, RE::EffectSetting* a_effect)
		{
			return FlashFor(a_effect);
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
