// Dynamic Wards - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// The helpers that need the game: a form's name for the log and reports, a form by its "0xID~Plugin" text. The game-free text helpers are in WardMath.h (tested off the game).

#include "Plugin.h"
#include "Po3Tweaks.h"

#include <nlohmann/json.hpp>

namespace Plugin
{
	namespace
	{
		// a name in the game's own code page stays distinct in a readout: valid UTF-8 as it is, otherwise every byte above
		// 0x7F as \xNN (the readout's dump would turn each into the same U+FFFD)
		std::string Readable(std::string_view a_text)
		{
			try {
				(void)nlohmann::json(a_text).dump();
				return std::string(a_text);
			} catch (const nlohmann::json::exception&) {
				std::string out;
				for (const char c : a_text) {
					const auto b = static_cast<unsigned char>(c);
					out += b < 0x80 ? std::string(1, c) : std::format("\\x{:02X}", b);
				}
				return out;
			}
		}
	}

	std::string Where(const RE::TESForm* a_form)
	{
		if (!a_form) {
			return "(none)";
		}
		// a ward effect's or a spell's editor ID only with powerofthree's Tweaks keeping them (the game throws those away)
		const auto  id = Po3Tweaks::EditorID(a_form);
		const auto* file = a_form->GetFile(0);
		return std::format("{} | {:08X} | {}", !id.empty() ? Readable(id) : "(no editor ID)", a_form->GetFormID(),
			file ? Readable(file->GetFilename()) : "(created)");
	}

	// "0xID~Plugin" (all of it a number: "0x1G~X.esp" is no form), or an editor ID - most kinds of record only with powerofthree's Tweaks
	RE::TESForm* ResolveForm(std::string_view a_text) { return Po3Tweaks::FormOf(a_text); }

	std::string FormText(const RE::TESForm* a_form)
	{
		const auto* file = a_form ? a_form->GetFile(0) : nullptr;
		if (!file) {
			return {};
		}
		const auto id = a_form->GetFormID();
		const auto local = file->IsLight() ? (id & 0xFFF) : (id & 0xFFFFFF);
		return std::format("0x{:X}~{}", local, file->GetFilename());
	}
}
