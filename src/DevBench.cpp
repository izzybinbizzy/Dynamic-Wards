// Dynamic Wards - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.

#include "Plugin.h"

#include "DevBenchAPI.h"

namespace Plugin
{
	namespace
	{
		constexpr const char* kKey = "dynamicwards";

		// DevBench 1.5.0 (RegisterToolExtension)
		constexpr unsigned int kNeedsBuild = 10500;

		constexpr const char* kDescriptor =
			R"({"description":"Dynamic Wards 3.0 - every ward effect found, what each row wears now (its color), how the colors were applied (palettes swapped on the graphics card, glow blocks set), the lights, and the settings. Read only.","inputSchema":{"type":"object","properties":{}},"readOnly":true})";

		constexpr const char* kSetDescriptor =
			R"({"description":"Dynamic Wards 3.0 - change one menu setting the way the menu does (saved, then applied on the main thread). args: set = ladder (color = RRGGBB) | stages (3-5) | reversed (0/1) | row:<Row> (color = RRGGBB, default or vanilla) | opacity (25-100) | transparency (0-90, the domes' facing fill) | brightness (25-175, glow and lights; 100 = as built) | dome (0 360, 1 normal) | unlock (0-4) | wardLight | lights | crusader | every (0/1) | mod:<Plugin.esp> (0/1) | perk:<0xID~Plugin> | domeLights (0 auto, 1 on, 2 off; not saved) | lighting (0 Community Shaders, 1 ENB, 2 Vanilla; a test, not saved) | preview:<Row> (value = seconds; plays the dome that row wears now on the player), value = number.","inputSchema":{"type":"object","properties":{"set":{"type":"string"},"value":{"type":"number"},"color":{"type":"string"}}}})";
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

		std::string JsonField(std::string_view a_json, std::string_view a_key)
		{
			const auto k = a_json.find(std::format("\"{}\"", a_key));
			if (k == std::string_view::npos) {
				return {};
			}
			auto i = a_json.find(':', k);
			if (i == std::string_view::npos) {
				return {};
			}
			++i;
			while (i < a_json.size() && (a_json[i] == ' ' || a_json[i] == '"')) {
				++i;
			}
			auto e = i;
			while (e < a_json.size() && a_json[e] != '"' && a_json[e] != ',' && a_json[e] != '}') {
				++e;
			}
			return std::string(a_json.substr(i, e - i));
		}

		void SetHandler(void*, const char* a_args, void* a_sink, DevBenchAPI::WriteFn a_write)
		{
			const std::string_view args{ a_args ? a_args : "" };
			const auto             key = JsonField(args, "set");
			const double           number = std::strtod(JsonField(args, "value").c_str(), nullptr);
			const int              value = static_cast<int>(number);
			bool                   ok = true;
			if (key == "wardLight") {
				SetWardLightOn(value != 0);
			} else if (key == "lights") {
				SetColoredLightsOn(value != 0);
			} else if (key == "ladder") {
				const auto c = ParseColor(JsonField(args, "color"));
				ok = c.has_value();
				if (c) {
					SetLadderColor(*c);
				}
			} else if (key == "opacity") {
				SetOpacity(value);
			} else if (key == "transparency") {
				SetTransparency(value);
			} else if (key == "brightness") {
				SetBrightness(value);
			} else if (key == "stages") {
				SetLadderStages(value);
			} else if (key == "reversed") {
				SetLadderReversed(value != 0);
			} else if (key == "dome") {
				SetDome360(value == 0);
			} else if (key == "unlock") {
				SetUnlockRule(static_cast<Unlock>(std::clamp(value, 0, 4)));
			} else if (key == "crusader") {
				SetCrusaderOn(value != 0);
			} else if (key == "every") {
				SetEveryWard(value != 0);
			} else if (key.starts_with("mod:")) {
				SetModOn(key.substr(4), value != 0);  // one Compatibility-page tick, by plugin file name
			} else if (key.starts_with("perk:")) {
				SetUnlockPerk(key.substr(5));
			} else if (key == "lighting") {
				SetLightingPick(static_cast<Lighting>(std::clamp(value, 0, 2)));  // a test switch, never saved
				Later([]() { ApplyAll("devbench"); });
				if (a_write) {
					a_write(a_sink, R"({"queued":true})");
				}
				return;
			} else if (key == "domeLights") {
				SetDomeMode(static_cast<DomeMode>(std::clamp(value, 0, 2)));  // a test switch, never saved
				if (a_write) {
					a_write(a_sink, R"({"queued":true})");
				}
				return;
			} else if (key.starts_with("preview:")) {
				ok = false;
				for (std::size_t i = 0; i < kRows; ++i) {
					if (kTokens[i] == std::string_view(key).substr(8)) {
						Later([i, number]() { PreviewRow(i, static_cast<float>(number)); });
						ok = true;
					}
				}
				if (a_write) {
					a_write(a_sink, ok ? R"({"queued":true})" : R"({"queued":false,"error":"unknown row"})");
				}
				return;
			} else if (key.starts_with("row:")) {
				ok = false;
				const auto want = Lower(JsonField(args, "color"));
				for (std::size_t i = 0; i < kRows; ++i) {
					if (kTokens[i] == std::string_view(key).substr(4)) {
						if (want == "vanilla") {
							SetRow(i, RowMode::kVanilla, RowCustom(i));
							ok = true;
						} else if (want == "default") {
							SetRow(i, RowMode::kDefault, RowCustom(i));
							ok = true;
						} else if (const auto c = ParseColor(want)) {
							SetRow(i, RowMode::kCustom, *c);
							ok = true;
						}
					}
				}			} else {
				ok = false;
			}
			if (ok) {
				SaveSettings();
				Later([]() { ApplyAll("devbench"); });
			}
			if (a_write) {
				a_write(a_sink, ok ? R"({"queued":true})" : R"({"queued":false,"error":"unknown setting"})");
			}
		}
	}

	void OfferToDevBench()
	{
		auto* devbench = DevBenchAPI::GetDevBenchInterface001();
		if (!devbench) {
			SKSE::log::info("[DEVBENCH] DevBench is not in this load order - the report stays in the log");
			return;
		}

		const auto build = devbench->GetBuildNumber();
		if (build < kNeedsBuild) {
			SKSE::log::warn("[DEVBENCH] DevBench build {} is older than {} and cannot take an inspect extension", build, kNeedsBuild);
			return;
		}

		const bool fresh = devbench->RegisterToolExtension("inspect", kKey, kDescriptor, Handler, nullptr);
		devbench->RegisterMenuHandler(kKey, kSetDescriptor, SetHandler, nullptr);
		SKSE::log::info("[DEVBENCH] offered the ward report to DevBench build {} as inspect kind={} ({})",
			build, kKey, fresh ? "a new entry" : "replacing one already registered");
	}
}
