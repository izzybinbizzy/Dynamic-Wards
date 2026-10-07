// Dynamic Wards - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// DevBench, when it is in the load order (it is optional; nothing here runs without it):
//   inspect kind=dynamicwards    every ward found, what each row wears, the colors, the lights and the settings.
//   dynamicwards.control         action = set (setting + value / color: one menu setting changed the way the menu does -
//                                saved, then applied on the main thread; the reply is the wards' state after it) | apply
//                                (dress every ward again) | preview (row + value = seconds: plays that row's dome on the
//                                player). The two test switches (domeLights, lighting) are never saved.
//   menu invoke name=dynamicwards the older setter (set = <setting>, value, color), kept for scripts that already use it.
//   event dynamicwards.applied   {why, found, dressed} each time the wards are dressed (loads, menus, settings).

#include "Plugin.h"

#include "DevBenchGlue.h"

namespace Plugin
{
	namespace
	{
		using DevBenchGlue::json;

		constexpr const char* kKey = "dynamicwards";

		constexpr const char* kDescriptor =
			R"json({"description":"Dynamic Wards 3.0 - every ward effect found, what each row wears now (its color), how the colors were applied (palettes swapped on the graphics card, glow blocks set), the lights, and the settings. Read only.","inputSchema":{"type":"object","properties":{}},"readOnly":true})json";

		constexpr const char* kTool =
			R"json({"description":"Dynamic Wards (every ward gets its own color and dome): change its settings and drive its preview. action=set changes one menu setting the way the menu does (saved, then applied) and replies with the wards' state after it; action=apply dresses every ward again; action=preview plays the dome a row wears now on the player for value seconds. Settings: ladder (color RRGGBB), stages (3-5), reversed (0/1), row:<Row> (color RRGGBB, default or vanilla), opacity (25-100), transparency (0-90), brightness (25-175), castingglow (25-175, the menu's Hand Brightness), dome (0 = 360, 1 = normal), unlock (0-4), wardLight, lights, crusader, every (0/1), mod:<Plugin.esp> (0/1), perk:<0xID~Plugin>, and two test switches never saved: domeLights (0 auto, 1 on, 2 off), lighting (0 Community Shaders, 1 ENB, 2 Vanilla). Rows are the inspect report's rows keys. Read state with inspect kind=dynamicwards.","inputSchema":{"type":"object","properties":{"action":{"type":"string","enum":["set","apply","preview"]},"setting":{"type":"string","description":"set: the setting name, e.g. opacity, ladder, row:Adept"},"value":{"type":"number","description":"set: the number; preview: seconds"},"color":{"type":"string","description":"set: RRGGBB for ladder and row:<Row>; row:<Row> also takes default or vanilla"},"row":{"type":"string","description":"preview: the row, e.g. Adept"}},"required":["action"]}})json";

		constexpr const char* kSetDescriptor =
			R"json({"description":"Dynamic Wards 3.0 - change one menu setting the way the menu does (saved, then applied on the main thread). The same as the dynamicwards.control tool's action=set. args: set = ladder (color = RRGGBB) | stages (3-5) | reversed (0/1) | row:<Row> (color = RRGGBB, default or vanilla) | opacity (25-100) | transparency (0-90, the domes' facing fill) | brightness (25-175, the wards' glow and lights; 100 = as built) | castingglow (25-175, the menu's Hand Brightness: the casting art and its light alone) | dome (0 360, 1 normal) | unlock (0-4) | wardLight | lights | crusader | every (0/1) | mod:<Plugin.esp> (0/1) | perk:<0xID~Plugin> | domeLights (0 auto, 1 on, 2 off; not saved) | lighting (0 Community Shaders, 1 ENB, 2 Vanilla; a test, not saved) | preview:<Row> (value = seconds; plays the dome that row wears now on the player), value = number.","inputSchema":{"type":"object","properties":{"set":{"type":"string"},"value":{"type":"number"},"color":{"type":"string"}}}})json";

		void Handler(void*, const char*, void* a_sink, DevBenchAPI::WriteFn a_write)
		{
			if (a_write) {
				// DevBench listener thread: both reports only read, each under its own lock, taken one after the other
				auto report = WardsReport();
				report.pop_back();
				report += R"(,"domeLights":)" + DomeLightsReport() + R"(,"lighting":)" + LightingReport() + R"(,"colors":)" + ColorsReport() + "}";
				a_write(a_sink, report.c_str());
			}
		}

		json SettingNames()
		{
			return json::array({ "ladder", "stages", "reversed", "row:<Row>", "opacity", "transparency", "brightness", "castingglow", "dome", "unlock",
				"wardLight", "lights", "crusader", "every", "mod:<Plugin.esp>", "perk:<0xID~Plugin>", "domeLights", "lighting" });
		}

		json RowNames()
		{
			json rows = json::array();
			for (std::size_t i = 0; i < kRows; ++i) {
				rows.push_back(kTokens[i]);
			}
			return rows;
		}

		std::optional<std::size_t> RowIndex(std::string_view a_token)
		{
			for (std::size_t i = 0; i < kRows; ++i) {
				if (kTokens[i] == a_token) {
					return i;
				}
			}
			return std::nullopt;
		}

		// the part of the ward report a setting can move, read after the change was applied
		json StateNow()
		{
			const auto r = json::parse(WardsReport(), nullptr, false);
			json       out = json::object();
			if (r.is_object()) {
				for (const char* k : { "found", "dressed", "lastApply", "ladder", "dome360", "unlocked360", "unlockRule", "unlockPerk", "wardLight",
						 "coloredLights", "everyWard", "crusader", "rows" }) {
					if (r.contains(k)) {
						out[k] = r[k];
					}
				}
			}
			return out;
		}

		// one setting, exactly as the menu changes it; nullopt = unknown or a bad value. a_saved tells the caller whether
		// it is a saved setting (the two test switches are not)
		std::optional<std::string> SetOne(const std::string& a_key, double a_number, const std::string& a_color, bool& a_saved)
		{
			const int value = static_cast<int>(a_number);
			a_saved = true;
			if (a_key == "wardLight") {
				SetWardLightOn(value != 0);
			} else if (a_key == "lights") {
				SetColoredLightsOn(value != 0);
			} else if (a_key == "ladder") {
				const auto c = ParseColor(a_color);
				if (!c) {
					return std::nullopt;
				}
				SetLadderColor(*c);
			} else if (a_key == "opacity") {
				SetOpacity(value);
			} else if (a_key == "transparency") {
				SetTransparency(value);
			} else if (a_key == "brightness") {
				SetBrightness(value);
			} else if (a_key == "castingglow") {
				SetCastingGlow(value);
			} else if (a_key == "stages") {
				SetLadderStages(value);
			} else if (a_key == "reversed") {
				SetLadderReversed(value != 0);
			} else if (a_key == "dome") {
				SetDome360(value == 0);
			} else if (a_key == "unlock") {
				SetUnlockRule(static_cast<Unlock>(std::clamp(value, 0, 4)));
			} else if (a_key == "crusader") {
				SetCrusaderOn(value != 0);
			} else if (a_key == "every") {
				SetEveryWard(value != 0);
			} else if (a_key.starts_with("mod:")) {
				SetModOn(a_key.substr(4), value != 0);  // one Compatibility-page tick, by plugin file name
			} else if (a_key.starts_with("perk:")) {
				SetUnlockPerk(a_key.substr(5));
			} else if (a_key == "lighting") {
				SetLightingPick(static_cast<Lighting>(std::clamp(value, 0, 2)));  // a test switch, never saved
				a_saved = false;
			} else if (a_key == "domeLights") {
				SetDomeMode(static_cast<DomeMode>(std::clamp(value, 0, 2)));  // a test switch, never saved
				a_saved = false;
			} else if (a_key.starts_with("row:")) {
				const auto row = RowIndex(std::string_view(a_key).substr(4));
				const auto want = Lower(a_color);
				if (!row) {
					return std::nullopt;
				}
				if (want == "vanilla") {
					SetRow(*row, RowMode::kVanilla, RowCustom(*row));
				} else if (want == "default") {
					SetRow(*row, RowMode::kDefault, RowCustom(*row));
				} else if (const auto c = ParseColor(want)) {
					SetRow(*row, RowMode::kCustom, *c);
				} else {
					return std::nullopt;
				}
			} else {
				return std::nullopt;
			}
			return a_key;
		}

		json ApplyAndRead(bool a_save)
		{
			if (a_save) {
				SaveSettings();
			}
			auto now = DevBenchGlue::OnMainThread([]() { ApplyAll("devbench"); return StateNow(); });
			if (!now) {
				return DevBenchGlue::NotRunYet();
			}
			return json{ { "ok", true }, { "now", *now } };
		}

		json PreviewNow(const std::string& a_row, double a_seconds)
		{
			const auto row = RowIndex(a_row);
			if (!row) {
				return DevBenchGlue::Refusal("unknown row '" + a_row + "'", RowNames());
			}
			Later([i = *row, a_seconds]() { PreviewRow(i, static_cast<float>(a_seconds)); });
			return json{ { "ok", true }, { "preview", a_row }, { "seconds", a_seconds } };
		}

		json Act(const json& a_args)
		{
			const auto action = DevBenchGlue::Text(a_args, "action");
			if (action == "apply") {
				return ApplyAndRead(false);
			}
			if (action == "preview") {
				return PreviewNow(DevBenchGlue::Text(a_args, "row"), DevBenchGlue::Number(a_args, "value").value_or(10.0));
			}
			if (action == "set") {
				const auto key = DevBenchGlue::Text(a_args, "setting");
				bool       saved = true;
				if (!SetOne(key, DevBenchGlue::Number(a_args, "value").value_or(0.0), DevBenchGlue::Text(a_args, "color"), saved)) {
					return DevBenchGlue::Refusal("unknown setting or bad value for '" + key + "' (ladder and row:<Row> need color)", SettingNames());
				}
				auto out = ApplyAndRead(saved);
				out["setting"] = key;
				out["saved"] = saved;
				return out;
			}
			return DevBenchGlue::Refusal("unknown action '" + action + "'", json::array({ "set", "apply", "preview" }));
		}

		void Control(void*, const char* a_args, void* a_sink, DevBenchAPI::WriteFn a_write) noexcept
		{
			try {
				DevBenchGlue::Reply(a_sink, a_write, Act(DevBenchGlue::Args(a_args)));
			} catch (...) {
				DevBenchGlue::Reply(a_sink, a_write, DevBenchGlue::Refusal("Dynamic Wards could not handle that"));
			}
		}

		// the older `menu invoke name=dynamicwards set=<setting>` route, unchanged in what it accepts
		void SetHandler(void*, const char* a_args, void* a_sink, DevBenchAPI::WriteFn a_write) noexcept
		{
			try {
				const auto args = DevBenchGlue::Args(a_args);
				const auto key = DevBenchGlue::Text(args, "set");
				const auto number = DevBenchGlue::Number(args, "value").value_or(0.0);
				if (key.starts_with("preview:")) {
					DevBenchGlue::Reply(a_sink, a_write, PreviewNow(key.substr(8), number));
					return;
				}
				bool saved = true;
				if (!SetOne(key, number, DevBenchGlue::Text(args, "color"), saved)) {
					DevBenchGlue::Reply(a_sink, a_write, DevBenchGlue::Refusal("unknown setting", SettingNames()));
					return;
				}
				if (saved) {
					SaveSettings();
				}
				Later([]() { ApplyAll("devbench"); });
				DevBenchGlue::Reply(a_sink, a_write, json{ { "queued", true } });
			} catch (...) {
				DevBenchGlue::Reply(a_sink, a_write, DevBenchGlue::Refusal("Dynamic Wards could not handle that"));
			}
		}
	}

	void WardsApplied(const char* a_why, std::size_t a_found, std::size_t a_dressed)
	{
		DevBenchGlue::Emit("dynamicwards.applied", json{ { "why", a_why ? a_why : "" }, { "found", a_found }, { "dressed", a_dressed } });
	}

	void OfferToDevBench()
	{
		auto* devbench = DevBenchAPI::GetDevBenchInterface001();
		if (!devbench) {
			SKSE::log::info("[DEVBENCH] DevBench is not in this load order - the report stays in the log");
			return;
		}

		const auto build = devbench->GetBuildNumber();
		if (build < DevBenchGlue::kNeedsBuild) {
			SKSE::log::warn("[DEVBENCH] DevBench build {} is older than {} and cannot take an inspect extension", build, DevBenchGlue::kNeedsBuild);
			return;
		}

		const bool fresh = devbench->RegisterToolExtension("inspect", kKey, kDescriptor, Handler, nullptr);
		devbench->RegisterTool("dynamicwards.control", kTool, Control, nullptr);
		devbench->RegisterMenuHandler(kKey, kSetDescriptor, SetHandler, nullptr);
		SKSE::log::info("[DEVBENCH] offered to DevBench build {}: inspect kind={} ({}), the tool dynamicwards.control, menu invoke name={}",
			build, kKey, fresh ? "a new entry" : "replacing one already registered", kKey);
	}
}
