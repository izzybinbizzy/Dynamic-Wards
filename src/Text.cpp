// Dynamic Wards - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// The helpers that need the game: a form's name for the log and reports, JSON text for DevBench, a form by its
// "0xID~Plugin" text. The game-free text helpers are in WardMath.h (tested off the game).

#include "Plugin.h"

namespace Plugin
{
	std::string Where(const RE::TESForm* a_form)
	{
		if (!a_form) {
			return "(none)";
		}
		const char* id = a_form->GetFormEditorID();
		const auto* file = a_form->GetFile(0);
		return std::format("{} | {:08X} | {}", id && *id ? id : "(no editor ID)", a_form->GetFormID(),
			file ? file->GetFilename() : "(created)");
	}

	namespace
	{
		// the length of a well-formed UTF-8 sequence starting at a_at, 0 when it is not one
		std::size_t Utf8Length(std::string_view a_text, std::size_t a_at)
		{
			const auto        c = static_cast<unsigned char>(a_text[a_at]);
			const std::size_t n = c >= 0xF0 && c < 0xF5 ? 4 : c >= 0xE0 ? 3 :
			                                              c >= 0xC2     ? 2 :
			                                                              0;
			if (n == 0 || a_at + n > a_text.size()) {
				return 0;
			}
			for (std::size_t k = 1; k < n; ++k) {
				if ((static_cast<unsigned char>(a_text[a_at + k]) & 0xC0) != 0x80) {
					return 0;
				}
			}
			return n;
		}
	}

	std::string JsonEscape(std::string_view a_text)
	{
		std::string out;
		out.reserve(a_text.size() + 8);
		for (std::size_t i = 0; i < a_text.size(); ++i) {
			const auto c = static_cast<unsigned char>(a_text[i]);
			if (c >= 0x80) {
				// a UTF-8 name goes as it is; a byte of the game's own code page (a localized form name) would make the reply
				// invalid UTF-8, so it goes as \u00XX
				if (const auto n = Utf8Length(a_text, i)) {
					out += a_text.substr(i, n);
					i += n - 1;
				} else {
					out += std::format("\\u{:04x}", static_cast<unsigned>(c));
				}
				continue;
			}
			switch (c) {
			case '"':
				out += "\\\"";
				break;
			case '\\':
				out += "\\\\";
				break;
			case '\n':
				out += "\\n";
				break;
			case '\r':
				out += "\\r";
				break;
			case '\t':
				out += "\\t";
				break;
			default:
				if (c < 0x20) {
					out += std::format("\\u{:04x}", static_cast<unsigned>(c));
				} else {
					out += static_cast<char>(c);
				}
				break;
			}
		}
		return out;
	}

	RE::TESForm* ResolveForm(std::string_view a_text)
	{
		const auto tilde = a_text.find('~');
		if (tilde == std::string_view::npos || tilde == 0) {
			return nullptr;
		}
		std::uint32_t local = 0;
		auto          hex = a_text.substr(0, tilde);
		if (hex.starts_with("0x") || hex.starts_with("0X")) {
			hex.remove_prefix(2);
		}
		const auto* end = hex.data() + hex.size();
		if (const auto [stop, ec] = std::from_chars(hex.data(), end, local, 16); ec != std::errc{} || stop != end) {
			return nullptr;  // all of it a number: "0x1G~X.esp" is no form
		}
		auto* dh = RE::TESDataHandler::GetSingleton();
		return dh ? dh->LookupForm(local, a_text.substr(tilde + 1)) : nullptr;
	}

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
