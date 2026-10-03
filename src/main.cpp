// Dynamic Wards - SKSE plugin
// Copyright (C) 2026 izzydoingit
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see the
// GNU General Public License in LICENSE for details.
//
// Dynamic Wards 3.0: one neutral set of ward meshes per rank, colored in memory (any color, any opacity). No plugin file; one Papyrus native
// (DynamicWards.FlashFor, for 360 Ward's rebuilt SphereWard script); settings live in DynamicWards.ini, nothing goes in the save.

#include "Plugin.h"
#include "Translation.h"

using namespace Plugin;

namespace
{
	// a loading screen re-applies; any other menu closing re-checks the 360 unlock
	struct MenuWatch : RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			if (a_event && !a_event->opening) {
				const std::string_view name{ a_event->menuName.c_str() };
				if (name == RE::LoadingMenu::MENU_NAME) {
					Later([]() { ApplyAll("loading screen closed"); });
				} else if (name != RE::HUDMenu::MENU_NAME && name != RE::CursorMenu::MENU_NAME) {
					Later([]() { CheckUnlock("a menu closed"); });
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}
		static MenuWatch* Get()
		{
			static MenuWatch w;
			return &w;
		}
	};

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) {
			return;
		}
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kPostLoad:
			OfferToDevBench();
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			{
				const auto started = std::chrono::steady_clock::now();
				LoadSettings();
				ReadLighting();
				MakeHandLights();
				MakeArt();
				FindWards();
				ApplyAll("data loaded");
				StartDomeLights();
				SKSE::log::info("{}", Translation::Load("Data/SKSE/Plugins/DynamicWards/Translation.json"));
				RegisterMenu();
				if (auto* ui = RE::UI::GetSingleton()) {
					ui->AddEventSink<RE::MenuOpenCloseEvent>(MenuWatch::Get());
				}
				SKSE::log::info("ready in {:.1f} ms",
					std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
			}
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
			ApplyAll("game loaded");
			break;
		case SKSE::MessagingInterface::kNewGame:
			ApplyAll("new game");
			break;
		default:
			break;
		}
	}
}

namespace Plugin
{
	std::string Lower(std::string_view a_text)
	{
		std::string out(a_text);
		std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return out;
	}

	bool Contains(std::string_view a_haystack, std::string_view a_needle)
	{
		return !a_needle.empty() && a_haystack.find(a_needle) != std::string_view::npos;
	}

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

	std::string JsonEscape(std::string_view a_text)
	{
		std::string out;
		out.reserve(a_text.size() + 8);
		for (const unsigned char c : a_text) {
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

	void Later(std::function<void()> a_job)
	{
		if (auto* tasks = SKSE::GetTaskInterface()) {
			tasks->AddTask(std::move(a_job));
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	SKSE::log::info("Dynamic Wards 3.0 - any color for any ward: one set of meshes, colored in memory; no plugin of its own");
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	if (auto* papyrus = SKSE::GetPapyrusInterface()) {
		papyrus->Register(RegisterPapyrus);
	}
	return true;
}
