// Dynamic Wards - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// 3.0: ONE neutral set of meshes per row, and the color made here, in memory.
//
// A ward keeps its color in three places, and each is reached once per color change, never per frame:
//   1. its PALETTES (the greyscale-to-palette blocks). Each row's meshes name palette files of their own, so the texture
//      object behind a palette is that row's alone: its pixels are rebuilt from the neutral source in the ward's color (the
//      same curve the 2.x build baked - wardgen.pal_retint) and swapped in on the graphics card. Every live ward of the row
//      changes at once.
//   2. the GLOW of its other blocks (their emissive), on the cached MODEL every ward is cloned from (BSModelDB): a block
//      whose emissive is a color takes the ward's hue at its own brightness (wardgen.retint); a palette block's emissive is a
//      coordinate into its palette and keeps its number. The ENB light sprite takes the ward's color at full saturation.
//   3. its OPACITY: every block's alpha, by the menu's slider (the lights dim with it, Lighting.cpp / DomeLights.cpp); a
//      palette block that reads no alpha from its emissive is dimmed through its glow strength instead (Block::alphaDead).
//   5. its BRIGHTNESS: every block's glow strength (emissive multiple), by the menu's all-in-one slider - the lights follow it.
//   4. its TRANSPARENCY (domes only): a dome's FILL - a falloff block more opaque facing you than at its rim (the 360 dome's
//      cloud layer, 90% facing you) - loses that much of its facing opacity; the rim and the glow stay (his ask 2026-10-03,
//      tested on ENB, Vanilla and Community Shaders: `Temp\DW orb + transparency test\round 2*`).
// The dome's colour controllers would write the neutral color back every frame (probe 2, 2026-10-02: clearing kActive
// does not stop the sequence that drives them), so they are taken off the cached model once; every clone made after
// that has none.

#include "Plugin.h"

namespace Plugin
{
	namespace
	{
		constexpr const char* kPaletteDir = "Data/SKSE/Plugins/Dynamic Wards/Palettes/";
		constexpr float       kSatFloor = 0.25f;  // wardgen SAT_FLOOR: a ramp with less color of its own is tinted flat
		constexpr float       kHueHold = 0.50f;   // wardgen HUE_HOLD: green and blue keep at least this much hue
		constexpr float       kSpriteSat = 0.75f; // the ENB light: the ward's hue at least this saturated (a pale light washes it out)
		// the ward IN THE HAND: every texel at least this far toward the ward's colour, so its centre is not a white-hot core
		// (his call 2026-10-03, "fix that" - the 2.x ramp kept each gradient's white end; the domes keep theirs)
		constexpr float       kHandCore = 1.0f;
		// a DOME on the Vanilla lighting pick: at least this far toward the ward's colour (his report 2026-10-05: on the vanilla
		// profiles the domes are "too washed out" beside the casting art, most of all blue). 0.5 was still washed out beside the
		// hand (his screenshot, same day, LTBG - Vanilla, normal dome): tinted as fully as the hand.
		constexpr float       kDomeCoreVanilla = 1.0f;
		// the NORMAL dome (not the 360 sphere) on EVERY lighting pick (his report 2026-10-06: "the vanilla wards are still too
		// washed out and need more saturation to match the casting art"; his pick: the normal dome, every lighting)
		constexpr float       kNormalDomeCore = 1.0f;

		struct Source
		{
			int                       w = 0, h = 0;
			std::vector<std::uint8_t> bgra;  // mip 0 only
			float                     sref = 0.0f;
			bool                      ok = false;
		};

		struct Palette
		{
			std::string                       path;  // as the material names it
			std::string                       stem;
			RE::NiPointer<RE::NiSourceTexture> tex;
			Color                             applied = 0xFFFFFFFF;
			bool                              hand = false;  // named by the hand art (wardinhandfx): tinted to its core
			bool                              normalDome = false;  // named by the normal dome (wardbodyfx, not 360): tinted to its core
		};

		struct Block
		{
			RE::NiPointer<RE::BSEffectShaderProperty> prop;
			RE::NiColorA                              base;  // as the neutral mesh has it
			bool                                      palette = false;
			bool                                      sprite = false;
			bool                                      fill = false;     // a dome's fill: thinned by the transparency slider
			bool                                      startFaces = false;  // the start angle is the one facing you
			float                                     facing = 0.0f;    // the fill's own opacity facing you
			float                                     scale = 1.0f;     // the block's own glow strength (emissive multiple)
			// a palette block whose emissive alpha is 0: the shader takes its alpha from the palette and the texture, never from
			// the emissive, so the opacity slider cannot reach it through alpha (the normal dome's vapour layers and flare - his
			// report 2026-10-05, "the actual flares and second layer both are the same throughout the entire spectrum").
			// Blended additively (every ward block is SrcAlpha + One), its glow strength scaled is the same thing as its alpha.
			bool                                      alphaDead = false;
			bool                                      additive = false;
			// the normal dome's vapour layer 1 (palette row 0.39, both greyscale bits): on Community Shaders a gold or orange ward
			// wears the baked plain-glow copy instead (kGoldVapour) - Community Shaders shades that block's spikes cream on those hues
			bool                                      vapourOne = false;
			bool                                      paletteAlpha = false;
			RE::NiPointer<RE::NiSourceTexture>        source;
			RE::BSFixedString                         sourcePath;
		};

		struct Master
		{
			std::string                 model;
			RE::NiPointer<RE::NiNode>   root;
			std::vector<Block>          blocks;
			std::size_t                 controllersOff = 0;
			bool                        hand = false;  // the casting art (wardinhandfx): the casting glow slider, never opacity or ward brightness
		};

		struct Row
		{
			std::vector<std::string> models;
			std::vector<Master>      masters;
			std::vector<Palette>     palettes;
			bool                     loaded = false;
			Color                    applied = 0xFFFFFFFF;
			int                      appliedOpacity = -1;
			int                      appliedTransparency = -1;
			int                      appliedBrightness = -1;
			int                      appliedCastingGlow = -1;
		};

		struct Grave
		{
			REX::W32::ID3D11Resource*           tex;
			REX::W32::ID3D11ShaderResourceView* srv;
			std::chrono::steady_clock::time_point at;
		};

		std::mutex                     gLock;
		std::array<Row, kRows>         gRows;
		std::map<std::string, Source>  gSources;
		std::vector<Grave>             gGrave;
		std::size_t                    gSwaps = 0, gSwapFails = 0, gEdits = 0, gMissing = 0;
		std::string                    gLastProblem = "none";
		// vapour layer 1's palette lookup baked into a plain texture (value in rgb, the palette's alpha in alpha), shipped with
		// every install; worn only on Community Shaders and only by a gold or orange ward (GoldVapour)
		constexpr const char*          kGoldVapour = "textures\\effects\\VaporTDWrdG.dds";
		RE::NiPointer<RE::NiSourceTexture> gGoldVapour;
		bool                           gGoldVapourTried = false;
		std::size_t                    gGoldVapourBlocks = 0;

		float Sat(float a_b, float a_g, float a_r)
		{
			const float m = (std::max)({ a_b, a_g, a_r });
			return m <= 0.0f ? 0.0f : (m - (std::min)({ a_b, a_g, a_r })) / m;
		}

		// a gold or orange ward (hue 15-60 degrees, at least 35% saturated): the hues whose vapour spikes Community Shaders
		// shades cream (Aedric FFC420 and Ember FF6A10, his report 2026-10-06; violet, frost, green, pink were fine)
		bool GoldVapour(Color a_c)
		{
			if (LightingPick() != Lighting::kShaders) {
				return false;  // ENB and Vanilla show the palette layer in its true color (measured 2026-10-06)
			}
			const float r = ((a_c >> 16) & 0xFF) / 255.0f, g = ((a_c >> 8) & 0xFF) / 255.0f, b = (a_c & 0xFF) / 255.0f;
			const float mx = (std::max)({ r, g, b }), mn = (std::min)({ r, g, b }), d = mx - mn;
			if (mx <= 0.0f || d / mx < 0.35f || mx != r) {
				return false;
			}
			const float hue = 60.0f * (g - b) / d;  // red is the largest channel: the hue sits between -60 and 60
			return hue >= 15.0f && hue <= 60.0f;
		}

		RE::NiSourceTexture* GoldVapourTexture()
		{
			if (!gGoldVapourTried) {
				gGoldVapourTried = true;
				RE::NiPointer<RE::NiTexture> t;
				RE::BSShaderManager::GetTexture(kGoldVapour, true, t, false);
				gGoldVapour.reset(t ? netimmerse_cast<RE::NiSourceTexture*>(t.get()) : nullptr);
				if (!gGoldVapour) {
					gLastProblem = std::format("{} is missing - gold and orange domes keep the palette layer", kGoldVapour);
				}
			}
			return gGoldVapour.get();
		}

		bool HoldsHue(Color a_c)
		{
			const int r = (a_c >> 16) & 0xFF, g = (a_c >> 8) & 0xFF, b = a_c & 0xFF;
			if ((std::max)({ r, g, b }) - (std::min)({ r, g, b }) <= 8) {
				return false;  // white: no hue to hold
			}
			return (std::max)(g, b) > r;
		}

		Source& LoadSource(const std::string& a_stem)
		{
			auto& s = gSources[Lower(a_stem)];
			if (s.ok || s.w < 0) {
				return s;
			}
			std::ifstream in(std::string(kPaletteDir) + a_stem + ".dds", std::ios::binary);
			std::vector<std::uint8_t> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			if (raw.size() < 128 || std::memcmp(raw.data(), "DDS ", 4) != 0) {
				s.w = -1;
				gLastProblem = std::format("palette source {} is missing or not a DDS", a_stem);
				return s;
			}
			std::uint32_t h = 0, w = 0, bits = 0;
			std::memcpy(&h, raw.data() + 12, 4);
			std::memcpy(&w, raw.data() + 16, 4);
			std::memcpy(&bits, raw.data() + 88, 4);
			if (bits != 32 || raw.size() < 128 + static_cast<std::size_t>(w) * h * 4) {
				s.w = -1;
				gLastProblem = std::format("palette source {} is not uncompressed 32-bit", a_stem);
				return s;
			}
			s.w = static_cast<int>(w);
			s.h = static_cast<int>(h);
			s.bgra.assign(raw.begin() + 128, raw.begin() + 128 + static_cast<std::ptrdiff_t>(w) * h * 4);
			std::vector<float> sats;
			for (std::size_t i = 0; i + 3 < s.bgra.size(); i += 4) {
				const auto b = s.bgra[i], g = s.bgra[i + 1], r = s.bgra[i + 2];
				if ((std::max)({ b, g, r }) >= 32) {
					sats.push_back(Sat(b, g, r));
				}
			}
			std::ranges::sort(sats);
			s.sref = sats.empty() ? 0.0f : sats[static_cast<std::size_t>(sats.size() * 0.95)];
			s.ok = true;
			return s;
		}

		// wardgen.pal_retint, texel for texel: the ramp keeps its own saturation and only its hue moves; alpha is the source's
		std::vector<std::uint8_t> Retint(const Source& a_s, Color a_c, float a_floor)
		{
			const float m = static_cast<float>((std::max)({ (a_c >> 16) & 0xFF, (a_c >> 8) & 0xFF, a_c & 0xFF, 1u }));
			const float tr = ((a_c >> 16) & 0xFF) / m, tg = ((a_c >> 8) & 0xFF) / m, tb = (a_c & 0xFF) / m;
			const bool  curve = a_s.sref >= kSatFloor;
			const float tsat = Sat(static_cast<float>(a_c & 0xFF), static_cast<float>((a_c >> 8) & 0xFF), static_cast<float>((a_c >> 16) & 0xFF));
			const bool  hold = HoldsHue(a_c);
			std::vector<std::uint8_t> out(a_s.bgra.size());
			for (std::size_t i = 0; i + 3 < a_s.bgra.size(); i += 4) {
				const float b = a_s.bgra[i], g = a_s.bgra[i + 1], r = a_s.bgra[i + 2];
				const float v = (std::max)({ b, g, r });
				float       cb = tb, cg = tg, cr = tr;
				const float pb = cb, pg = cg, pr = cr;  // the target this texel tints toward
				if (curve) {
					float k = tsat <= 0.0f ? 1.0f : (std::min)(1.0f, Sat(b, g, r) / tsat);
					if (hold) {
						k = (std::max)(k, kHueHold);
					}
					k = (std::max)(k, a_floor);
					cb = 1.0f + k * (pb - 1.0f);
					cg = 1.0f + k * (pg - 1.0f);
					cr = 1.0f + k * (pr - 1.0f);
				}
				out[i] = static_cast<std::uint8_t>(std::clamp(std::lround(cb * v), 0L, 255L));
				out[i + 1] = static_cast<std::uint8_t>(std::clamp(std::lround(cg * v), 0L, 255L));
				out[i + 2] = static_cast<std::uint8_t>(std::clamp(std::lround(cr * v), 0L, 255L));
				out[i + 3] = a_s.bgra[i + 3];
			}
			return out;
		}

		// the palette's texture on the graphics card, swapped for one in the ward's color (old objects released later)
		bool Swap(Palette& a_p, const Source& a_s, Color a_c)
		{
			auto* rt = a_p.tex ? a_p.tex->rendererTexture : nullptr;
			auto* dev = RE::BSGraphics::Renderer::GetDevice();
			if (!rt || !dev) {
				gLastProblem = std::format("{}: {}", a_p.path, rt ? "no graphics device" : "the texture is not loaded yet");
				return false;
			}
			// the mip chain, box-filtered down to 1x1
			const float core = a_p.hand ? kHandCore : a_p.normalDome ? kNormalDomeCore : LightingPick() == Lighting::kVanilla ? kDomeCoreVanilla : 0.0f;
			std::vector<std::vector<std::uint8_t>> mips{ Retint(a_s, a_c, core) };
			std::vector<std::pair<int, int>>       sizes{ { a_s.w, a_s.h } };
			while (sizes.back().first > 1 || sizes.back().second > 1) {
				const auto [pw, ph] = sizes.back();
				const int  w = (std::max)(1, pw / 2), h = (std::max)(1, ph / 2);
				const auto& prev = mips.back();
				std::vector<std::uint8_t> next(static_cast<std::size_t>(w) * h * 4);
				for (int y = 0; y < h; ++y) {
					for (int x = 0; x < w; ++x) {
						for (int c = 0; c < 4; ++c) {
							int sum = 0, n = 0;
							for (int dy = 0; dy < 2; ++dy) {
								for (int dx = 0; dx < 2; ++dx) {
									const int sx = (std::min)(pw - 1, x * 2 + dx), sy = (std::min)(ph - 1, y * 2 + dy);
									sum += prev[(static_cast<std::size_t>(sy) * pw + sx) * 4 + c];
									++n;
								}
							}
							next[(static_cast<std::size_t>(y) * w + x) * 4 + c] = static_cast<std::uint8_t>(sum / n);
						}
					}
				}
				mips.push_back(std::move(next));
				sizes.emplace_back(w, h);
			}
			std::vector<REX::W32::D3D11_SUBRESOURCE_DATA> init(mips.size());
			for (std::size_t i = 0; i < mips.size(); ++i) {
				init[i].sysMem = mips[i].data();
				init[i].sysMemPitch = static_cast<std::uint32_t>(sizes[i].first) * 4;
				init[i].sysMemSlicePitch = 0;
			}
			REX::W32::D3D11_TEXTURE2D_DESC d{};
			d.width = static_cast<std::uint32_t>(a_s.w);
			d.height = static_cast<std::uint32_t>(a_s.h);
			d.mipLevels = static_cast<std::uint32_t>(mips.size());
			d.arraySize = 1;
			d.format = REX::W32::DXGI_FORMAT_B8G8R8A8_UNORM;
			d.sampleDesc.count = 1;
			d.sampleDesc.quality = 0;
			d.usage = REX::W32::D3D11_USAGE_IMMUTABLE;
			d.bindFlags = REX::W32::D3D11_BIND_SHADER_RESOURCE;
			REX::W32::ID3D11Texture2D* tex = nullptr;
			if (dev->CreateTexture2D(&d, init.data(), &tex) < 0 || !tex) {
				gLastProblem = std::format("{}: the graphics card refused the texture", a_p.path);
				return false;
			}
			REX::W32::ID3D11ShaderResourceView* srv = nullptr;
			if (dev->CreateShaderResourceView(tex, nullptr, &srv) < 0 || !srv) {
				tex->Release();
				gLastProblem = std::format("{}: the graphics card refused the view", a_p.path);
				return false;
			}
			gGrave.push_back({ rt->texture, rt->resourceView, std::chrono::steady_clock::now() });
			rt->texture = tex;
			rt->resourceView = srv;
			rt->mips = static_cast<std::uint8_t>(mips.size());
			return true;
		}

		// the textures replaced a few seconds ago: nothing draws with them any more
		void Bury()
		{
			const auto now = std::chrono::steady_clock::now();
			std::erase_if(gGrave, [&](const Grave& a_g) {
				if (now - a_g.at < std::chrono::seconds(5)) {
					return false;
				}
				if (a_g.srv) {
					a_g.srv->Release();
				}
				if (a_g.tex) {
					a_g.tex->Release();
				}
				return true;
			});
		}

		void LoadRow(Row& a_row)
		{
			a_row.loaded = true;
			std::set<std::string> paths, handPaths, normalDomePaths;
			for (const auto& model : a_row.models) {
				Master m{ model, nullptr, {}, 0 };
				m.hand = Lower(model).find("wardinhandfx") != std::string::npos;
				RE::BSModelDB::DBTraits::ArgsType args{};
				if (RE::BSModelDB::Demand(model.c_str(), m.root, args) != RE::BSResource::ErrorCode::kNone || !m.root) {
					++gMissing;
					continue;  // a model this install does not carry (the 360 dome without 360 Ward's meshes, say)
				}
				RE::BSVisit::TraverseScenegraphGeometries(m.root.get(), [&](RE::BSGeometry* a_geometry) {
					auto* raw = a_geometry ? a_geometry->GetGeometryRuntimeData().shaderProperty.get() : nullptr;
					auto* prop = raw ? netimmerse_cast<RE::BSEffectShaderProperty*>(raw) : nullptr;
					auto* mat = prop ? static_cast<RE::BSEffectShaderMaterial*>(prop->GetMaterial()) : nullptr;
					if (!mat) {
						return RE::BSVisit::BSVisitControl::kContinue;
					}
					// a palette block both says so AND names a palette: the ENB build's hand wisps keep the flag with no palette
					// (Particle Patch's fix, so ENB does not make each wisp a light) and their color is still their emissive
					const bool named = mat->greyscaleTexturePath.c_str() && *mat->greyscaleTexturePath.c_str();
					Block b{ RE::NiPointer<RE::BSEffectShaderProperty>(prop), mat->baseColor,
						named && prop->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kGrayscaleToPaletteColor), false };
					const std::string src = Lower(mat->sourceTexturePath.c_str() ? mat->sourceTexturePath.c_str() : "");
					b.sprite = src.find("dwardglowenb") != std::string::npos;
					b.scale = mat->baseColorScale;
					{
						const auto lm = Lower(model);
						const bool normalDome = lm.find("wardbodyfx") != std::string::npos && lm.find("360") == std::string::npos;
						// layer 1 = the vapour block on the lower palette row (0.39; layer 2 sits at 0.74)
						b.vapourOne = normalDome && b.palette && src.find("vapor") != std::string::npos && mat->baseColor.red < 0.5f;
						if (b.vapourOne) {
							b.paletteAlpha = prop->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kGrayscaleToPaletteAlpha);
							b.source = mat->sourceTexture;
							b.sourcePath = mat->sourceTexturePath;
							++gGoldVapourBlocks;
						}
					}
					b.alphaDead = b.palette && mat->baseColor.alpha < 0.01f;
					if (const auto& ap = a_geometry->GetGeometryRuntimeData().alphaProperty) {
						b.additive = ap->GetAlphaBlending() && ap->GetDestBlendMode() == RE::NiAlphaProperty::AlphaFunction::kOne;
					}
					// a dome's fill: falloff on, and more opaque facing you (the larger cos angle) than at the rim
					if (prop->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kFalloff) &&
						Lower(model).find("wardbodyfx") != std::string::npos) {
						b.startFaces = mat->falloffStartAngle >= mat->falloffStopAngle;
						b.facing = b.startFaces ? mat->falloffStartOpacity : mat->falloffStopOpacity;
						const float rim = b.startFaces ? mat->falloffStopOpacity : mat->falloffStartOpacity;
						b.fill = b.facing > rim;
					}
					if (b.palette && mat->greyscaleTexturePath.c_str() && *mat->greyscaleTexturePath.c_str()) {
						paths.insert(mat->greyscaleTexturePath.c_str());
						const auto lm = Lower(model);
						if (lm.find("wardinhandfx") != std::string::npos) {
							handPaths.insert(mat->greyscaleTexturePath.c_str());
						} else if (lm.find("wardbodyfx") != std::string::npos && lm.find("360") == std::string::npos) {
							normalDomePaths.insert(mat->greyscaleTexturePath.c_str());
						}
					}
					// the colour controllers come off the cached model once: a clone of it has none
					std::vector<RE::NiTimeController*> colour;
					for (auto* c = prop->GetControllers(); c; c = c->GetNext()) {
						const auto* rtti = c->GetRTTI();
						if (rtti && rtti->name && std::string_view(rtti->name).find("ColorController") != std::string_view::npos) {
							colour.push_back(c);
						}
					}
					for (auto* c : colour) {
						c->IncRefCount();  // a sequence may still name it
						prop->RemoveController(c);
						++m.controllersOff;
					}
					m.blocks.push_back(std::move(b));
					return RE::BSVisit::BSVisitControl::kContinue;
				});
				a_row.masters.push_back(std::move(m));
			}
			for (const auto& path : paths) {
				Palette p{ path, {}, nullptr };
				p.hand = handPaths.contains(path);
				p.normalDome = !p.hand && normalDomePaths.contains(path);
				const auto name = std::filesystem::path(path).filename().string();
				// "<stem><look code, 4><row digit>.dds": the stem names the neutral source the DLL recolors from
				p.stem = name.size() > 9 ? name.substr(0, name.size() - 9) : name;
				RE::NiPointer<RE::NiTexture> t;
				RE::BSShaderManager::GetTexture(path.c_str(), true, t, false);
				p.tex.reset(t ? netimmerse_cast<RE::NiSourceTexture*>(t.get()) : nullptr);
				a_row.palettes.push_back(std::move(p));
			}
		}

		void PaintRow(Row& a_row, Color a_c, int a_opacity, int a_transparency, int a_brightness, int a_castingGlow)
		{
			const float wardGlow = a_brightness / 100.0f;
			const float wardAlpha = a_opacity / 100.0f;
			const float clear = 1.0f - a_transparency / 100.0f;
			for (auto& p : a_row.palettes) {
				if (p.applied == a_c) {
					continue;
				}
				auto& s = LoadSource(p.stem);
				if (!s.ok) {
					++gSwapFails;
					continue;
				}
				if (Swap(p, s, a_c)) {
					p.applied = a_c;
					++gSwaps;
				} else {
					++gSwapFails;
				}
			}
			const float r = ((a_c >> 16) & 0xFF) / 255.0f, g = ((a_c >> 8) & 0xFF) / 255.0f, b = (a_c & 0xFF) / 255.0f;
			const float m = (std::max)({ r, g, b, 0.001f });
			// the sprite: the hue at least kSpriteSat saturated, full value - a pale light reads white and washes the ward out
			float sh = 0, ss = 0, sv = 0;
			{
				const float mx = (std::max)({ r, g, b }), mn = (std::min)({ r, g, b }), dd = mx - mn;
				sv = mx;
				ss = mx <= 0 ? 0 : dd / mx;
				if (dd > 0) {
					sh = mx == r ? std::fmod((g - b) / dd, 6.0f) : mx == g ? (b - r) / dd + 2.0f : (r - g) / dd + 4.0f;
					sh = sh < 0 ? sh + 6.0f : sh;
				}
			}
			ss = ss > 0.02f ? (std::max)(ss, kSpriteSat) : 0.0f;
			auto hsv = [&](float a_h, float a_s) {
				const float c = a_s, x = c * (1 - std::fabs(std::fmod(a_h, 2.0f) - 1)), mm = 1 - c;
				float       rr = 0, gg = 0, bb = 0;
				switch (static_cast<int>(a_h) % 6) {
				case 0: rr = c, gg = x; break;
				case 1: rr = x, gg = c; break;
				case 2: gg = c, bb = x; break;
				case 3: gg = x, bb = c; break;
				case 4: rr = x, bb = c; break;
				default: rr = c, bb = x; break;
				}
				return RE::NiColor{ rr + mm, gg + mm, bb + mm };
			};
			const auto sprite = hsv(sh, ss);
			const bool goldVapour = GoldVapour(a_c) && GoldVapourTexture();
			for (auto& master : a_row.masters) {
				// the ward in the hands answers to the casting glow slider alone; opacity and ward brightness are the wards' own
				// (his rule 2026-10-05)
				const float glow = master.hand ? a_castingGlow / 100.0f : wardGlow;
				const float alpha = master.hand ? 1.0f : wardAlpha;
				for (auto& blk : master.blocks) {
					auto* mat = static_cast<RE::BSEffectShaderMaterial*>(blk.prop->GetMaterial());
					if (!mat) {
						continue;
					}
					auto* fresh = static_cast<RE::BSEffectShaderMaterial*>(mat->Create());
					if (!fresh) {
						continue;
					}
					fresh->CopyMembers(mat);
					const float bright = (std::max)({ blk.base.red, blk.base.green, blk.base.blue });
					const bool  plainVapour = blk.vapourOne && goldVapour;
					if (blk.vapourOne) {
						// the baked copy carries the palette's value and alpha itself, so the block becomes a plain glow in
						// the ward's color; any other hue (or lighting) gets the palette layer back exactly as loaded
						using F = RE::BSShaderProperty::EShaderPropertyFlag;
						fresh->sourceTexture = plainVapour ? RE::NiPointer<RE::NiSourceTexture>(gGoldVapour) : blk.source;
						fresh->sourceTexturePath = plainVapour ? RE::BSFixedString(kGoldVapour) : blk.sourcePath;
						if (plainVapour) {
							blk.prop->flags.reset(F::kGrayscaleToPaletteColor, F::kGrayscaleToPaletteAlpha);
						} else {
							blk.prop->flags.set(F::kGrayscaleToPaletteColor);
							if (blk.paletteAlpha) {
								blk.prop->flags.set(F::kGrayscaleToPaletteAlpha);
							}
						}
					}
					if (blk.sprite) {
						fresh->baseColor.red = sprite.red * alpha;
						fresh->baseColor.green = sprite.green * alpha;
						fresh->baseColor.blue = sprite.blue * alpha;
					} else if (plainVapour) {
						fresh->baseColor.red = r / m;
						fresh->baseColor.green = g / m;
						fresh->baseColor.blue = b / m;
					} else if (blk.vapourOne) {
						fresh->baseColor.red = blk.base.red;  // the palette row coordinate, as loaded
						fresh->baseColor.green = blk.base.green;
						fresh->baseColor.blue = blk.base.blue;
					} else if (!blk.palette && bright > 0.0001f) {
						fresh->baseColor.red = r / m * bright;
						fresh->baseColor.green = g / m * bright;
						fresh->baseColor.blue = b / m * bright;
					}
					fresh->baseColor.alpha = plainVapour ? alpha : blk.base.alpha * (blk.sprite ? 1.0f : alpha);
					fresh->baseColorScale = blk.scale * glow * (blk.alphaDead && blk.additive && !plainVapour ? alpha : 1.0f);
					if (blk.fill) {
						(blk.startFaces ? fresh->falloffStartOpacity : fresh->falloffStopOpacity) = blk.facing * clear;
					}
					if (blk.alphaDead && !blk.additive) {  // not seen in our meshes; the falloff is the alpha the shader does read
						fresh->falloffStartOpacity *= alpha;
						fresh->falloffStopOpacity *= alpha;
					}
					blk.prop->SetMaterial(fresh, true);
					if (blk.prop->GetMaterial() != fresh) {
						fresh->~BSEffectShaderMaterial();
						RE::free(fresh);
					}
					++gEdits;
				}
			}
			a_row.applied = a_c;
			a_row.appliedOpacity = a_opacity;
			a_row.appliedTransparency = a_transparency;
			a_row.appliedBrightness = a_brightness;
			a_row.appliedCastingGlow = a_castingGlow;
		}
	}

	void RegisterRowModels(std::size_t a_row, std::vector<std::string> a_models)
	{
		std::scoped_lock l{ gLock };
		if (a_row < kRows) {
			gRows[a_row].models = std::move(a_models);
		}
	}

	bool ApplyColors()
	{
		const int opacity = Opacity();
		const int transparency = Transparency();
		const int brightness = Brightness();
		const int castingGlow = CastingGlow();
		bool      changed = false;
		std::scoped_lock l{ gLock };
		Bury();
		for (std::size_t i = 0; i < kRows; ++i) {
			auto&      row = gRows[i];
			const auto c = RowColor(i);
			if (!c || row.models.empty()) {
				continue;  // a Vanilla row wears no art of ours
			}
			if (!row.loaded) {
				LoadRow(row);
			}
			if (row.applied != *c || row.appliedOpacity != opacity || row.appliedTransparency != transparency || row.appliedBrightness != brightness || row.appliedCastingGlow != castingGlow ||
				std::ranges::any_of(row.palettes, [&](const Palette& p) { return p.applied != *c; })) {
				PaintRow(row, *c, opacity, transparency, brightness, castingGlow);
				changed = true;
			}
		}
		ColorHandLights();
		SKSE::log::info("colors: {} palette(s) swapped, {} failed, {} glow block(s) set{}", gSwaps, gSwapFails, gEdits,
			gSwapFails ? " - " + gLastProblem : "");
		return changed;
	}

	std::string ColorsReport()
	{
		std::scoped_lock l{ gLock };
		std::string      rows;
		for (std::size_t i = 0; i < kRows; ++i) {
			const auto& r = gRows[i];
			std::size_t blocks = 0, off = 0, fills = 0;
			for (const auto& m : r.masters) {
				blocks += m.blocks.size();
				off += m.controllersOff;
				fills += std::ranges::count_if(m.blocks, [](const Block& a_b) { return a_b.fill; });
			}
			std::string pals;
			for (const auto& p : r.palettes) {
				pals += std::format(R"({}{{"path":"{}","stem":"{}","loaded":{},"applied":"{}"}})", pals.empty() ? "" : ",", JsonEscape(p.path),
					JsonEscape(p.stem), p.tex && p.tex->rendererTexture, p.applied == 0xFFFFFFFF ? std::string() : HexColor(p.applied));
			}
			rows += std::format(R"({}"{}":{{"models":{},"masters":{},"blocks":{},"fills":{},"colourControllersOff":{},"applied":"{}","palettes":[{}]}})",
				rows.empty() ? "" : ",", kTokens[i], r.models.size(), r.masters.size(), blocks, fills, off,
				r.applied == 0xFFFFFFFF ? std::string() : HexColor(r.applied), pals);
		}
		return std::format(R"({{"swaps":{},"swapFails":{},"glowEdits":{},"missingModels":{},"goldVapour":{{"blocks":{},"texture":{}}},"lastProblem":"{}","rows":{{{}}}}})",
			gSwaps, gSwapFails, gEdits, gMissing, gGoldVapourBlocks, gGoldVapour != nullptr, JsonEscape(gLastProblem), rows);
	}
}
