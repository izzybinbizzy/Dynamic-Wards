// Dynamic Wards - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE and the notice at the top of main.cpp.
//
// 3.0: ONE neutral set of meshes per row, and the color made here, in memory.
//
// A ward keeps its look in five places, and each is reached once per color change, never per frame:
//   1. its PALETTES (the greyscale-to-palette blocks). Each row's meshes name palette files of their own, so the texture
//      object behind a palette is that row's alone: its pixels are rebuilt from the neutral source in the ward's color (the
//      same curve the 2.x build baked - wardgen.pal_retint) and swapped in on the graphics card. Every live ward of the row
//      changes at once.
//   2. the GLOW of its other blocks (their emissive), on the cached MODEL every ward is cloned from (BSModelDB): a block
//      whose emissive is a color takes the ward's hue at its own brightness (wardgen.retint); a palette block's emissive is a
//      coordinate into its palette and keeps its number. The ENB light sprite takes the ward's color at full saturation.
//   3. its OPACITY: every block's alpha, by the menu's slider (the lights dim with it, Lighting.cpp / DomeLights.cpp); a
//      palette block that reads no alpha from its emissive is dimmed through its glow strength instead (Block::alphaDead).
//   4. its BRIGHTNESS: every block's glow strength (emissive multiple), by Ward brightness (the ward in the hands: Hand
//      Brightness) - the lights follow it.
//   5. its TRANSPARENCY (domes only): a dome's FILL - a falloff block more opaque facing you than at its rim (the 360 dome's
//      cloud layer, 90% facing you) - loses that much of its facing opacity; the rim and the glow stay (his ask 2026-10-03,
//      tested on ENB, Vanilla and Community Shaders: `Temp\DW orb + transparency test\round 2*`).
// The dome's colour controllers would write the neutral color back every frame (probe 2, 2026-10-02: clearing kActive
// does not stop the sequence that drives them), so they are taken off the cached model once; every clone made after
// that has none.

#include "Plugin.h"

#include <cmath>
#include <tuple>

namespace Plugin
{
	namespace
	{
		// (the palette recolor itself - wardgen.pal_retint's curve, the file checks - is game-free, in WardMath.h, and tested)
		constexpr const char* kPaletteDir = "Data/SKSE/Plugins/Dynamic Wards/Palettes/";
		constexpr float       kSpriteSat = 0.75f;  // the ENB light: the ward's hue at least this saturated (a pale light washes it out)
		// the ward IN THE HAND: every texel at least this far toward the ward's colour, so its centre is not a white-hot core
		// (his call 2026-10-03, "fix that" - the 2.x ramp kept each gradient's white end; the domes keep theirs)
		constexpr float kHandCore = 1.0f;
		// a DOME on the Vanilla lighting pick: at least this far toward the ward's colour (his report 2026-10-05: on the vanilla
		// profiles the domes are "too washed out" beside the casting art, most of all blue). 0.5 was still washed out beside the
		// hand (his screenshot, same day, LTBG - Vanilla, normal dome): tinted as fully as the hand.
		constexpr float kDomeCoreVanilla = 1.0f;
		// the NORMAL dome (not the 360 sphere) on EVERY lighting pick (his report 2026-10-06: "the vanilla wards are still too
		// washed out and need more saturation to match the casting art"; his pick: the normal dome, every lighting)
		constexpr float kNormalDomeCore = 1.0f;

		struct Source
		{
			PaletteImage image;  // mip 0 only
			bool         ok = false;
			bool         failed = false;  // read once and refused: not read again
		};

		struct Palette
		{
			std::string                        path;  // as the material names it
			std::string                        stem;
			RE::NiPointer<RE::NiSourceTexture> tex;
			Color                              applied = 0xFFFFFFFF;
			float                              kept = -1.0f;        // the screen correction it was made with (DomeKept)
			bool                               hand = false;        // named by the hand art (wardinhandfx): tinted to its core
			bool                               normalDome = false;  // named by the normal dome (wardbodyfx, not 360): tinted to its core
		};

		struct Block
		{
			RE::NiPointer<RE::BSEffectShaderProperty> prop;
			RE::NiColorA                              base;  // as the neutral mesh has it
			bool                                      palette = false;
			bool                                      sprite = false;
			bool                                      fill = false;        // a dome's fill: thinned by the transparency slider
			bool                                      startFaces = false;  // the start angle is the one facing you
			float                                     facing = 0.0f;       // the fill's own opacity facing you
			float                                     scale = 1.0f;        // the block's own glow strength (emissive multiple)
			float                                     loadStart = 1.0f;    // the falloff opacities as loaded: a repaint starts from them,
			float                                     loadStop = 1.0f;     // never from the last repaint's
			// a palette block whose emissive alpha is 0: the shader takes its alpha from the palette and the texture, never from
			// the emissive, so the opacity slider cannot reach it through alpha (the normal dome's vapour layers and flare - his
			// report 2026-10-05, "the actual flares and second layer both are the same throughout the entire spectrum").
			// Blended additively (every ward block is SrcAlpha + One), its glow strength scaled is the same thing as its alpha.
			bool alphaDead = false;
			bool additive = false;
			// the normal dome's vapour layer 1 (palette row 0.39, both greyscale bits): on Community Shaders a gold or orange ward
			// wears the baked plain-glow copy instead (kGoldVapour) - Community Shaders shades that block's spikes cream on those hues
			bool                               vapourOne = false;
			bool                               paletteAlpha = false;
			RE::NiPointer<RE::NiSourceTexture> source{};
			RE::BSFixedString                  sourcePath{};
		};

		struct Master
		{
			std::string               model;
			RE::NiPointer<RE::NiNode> root;
			std::vector<Block>        blocks;
			std::size_t               controllersOff = 0;
			bool                      hand = false;  // the casting art (wardinhandfx): the casting glow slider, never opacity or ward brightness
		};

		// the settings one look of a row was made from
		struct Setting
		{
			Color c = 0xFFFFFFFF;
			int   opacity = -1, transparency = -1, brightness = -1, castingGlow = -1;
			float kept = -1.0f;
			bool  operator==(const Setting&) const = default;
		};

		struct Row
		{
			std::vector<std::string> models;
			std::vector<Master>      masters;
			std::vector<Palette>     palettes;
			bool                     loaded = false;
			Setting                  applied{};  // what its cached models wear
			Setting                  live{};     // what its live domes show: a color drag paints them, never the cached models
			// the hand look the hands were last seen in (TakeHandLookChanges): a ward in the hand is re-equipped only when its row's
			// own look moved - never for a dome-only setting or another row
			Color handSeen = 0xFFFFFFFF;
			int   handGlowSeen = -1;
		};

		struct Grave
		{
			REX::W32::ID3D11Resource*             tex{ nullptr };
			REX::W32::ID3D11ShaderResourceView*   srv{ nullptr };
			std::chrono::steady_clock::time_point at;
		};

		std::mutex                    gLock;
		std::array<Row, kRows>        gRows;
		std::map<std::string, Source> gSources;
		std::vector<Grave>            gGrave;
		std::size_t                   gSwaps = 0, gSwapFails = 0, gEdits = 0, gMissing = 0, gLiveDomes = 0;
		std::string                   gLastProblem = "none";
		// vapour layer 1's palette lookup baked into a plain texture (value in rgb, the palette's alpha in alpha), shipped with
		// every install; worn only on Community Shaders and only by a gold or orange ward (GoldVapour)
		constexpr const char*              kGoldVapour = "textures\\effects\\VaporTDWrdG.dds";
		RE::NiPointer<RE::NiSourceTexture> gGoldVapour;
		bool                               gGoldVapourTried = false;
		std::size_t                        gGoldVapourBlocks = 0;

		// the share of a color's saturation the game keeps when it draws a dome (WardMath.h TrueOnScreen); 0 = no correction.
		// The devbench test switch domeKept (percent, -1 = this default) overrides it, never saved
		std::atomic<int> gKeptTest{ -1 };

		float DomeKept()
		{
			const int test = gKeptTest.load();
			return test >= 0 ? test / 100.0f : 0.0f;
		}

		// a gold or orange ward on Community Shaders (WardMath.h GoldHue): ENB and Vanilla show the palette layer in its true
		// color (measured 2026-10-06)
		bool GoldVapour(Color a_c)
		{
			return LightingPick() == Lighting::kShaders && GoldHue(a_c);
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

		Source& LoadSource(const std::string& a_stem)
		{
			auto& s = gSources[Lower(a_stem)];
			if (s.ok || s.failed) {
				return s;
			}
			std::ifstream             in(std::string(kPaletteDir) + a_stem + ".dds", std::ios::binary);
			std::vector<std::uint8_t> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			std::string               why;
			if (auto image = PaletteFromDds(raw, why)) {
				s.image = std::move(*image);
				s.ok = true;
			} else {
				s.failed = true;
				gLastProblem = std::format("palette source {}: {}", a_stem, why);
			}
			return s;
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
			const float                            core = a_p.hand ? kHandCore : a_p.normalDome                   ? kNormalDomeCore :
			                                                                 LightingPick() == Lighting::kVanilla ? kDomeCoreVanilla :
			                                                                                                        0.0f;
			std::vector<std::vector<std::uint8_t>> mips{ Retint(a_s.image, a_c, core) };
			std::vector<std::pair<int, int>>       sizes{ { a_s.image.w, a_s.image.h } };
			while (sizes.back().first > 1 || sizes.back().second > 1) {
				const auto [pw, ph] = sizes.back();
				const int                 w = (std::max)(1, pw / 2), h = (std::max)(1, ph / 2);
				const auto&               prev = mips.back();
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
			d.width = static_cast<std::uint32_t>(a_s.image.w);
			d.height = static_cast<std::uint32_t>(a_s.image.h);
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
				EachEffectBlock(m.root.get(), [&](RE::BSGeometry* a_geometry, RE::BSEffectShaderProperty* a_prop, RE::BSEffectShaderMaterial* a_mat) {
					// a palette block both says so AND names a palette: the ENB build's hand wisps keep the flag with no palette
					// (Particle Patch's fix, so ENB does not make each wisp a light) and their color is still their emissive
					const bool        named = a_mat->greyscaleTexturePath.c_str() && *a_mat->greyscaleTexturePath.c_str();
					Block             b{ RE::NiPointer<RE::BSEffectShaderProperty>(a_prop), a_mat->baseColor,
						named && a_prop->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kGrayscaleToPaletteColor), false };
					const std::string src = Lower(a_mat->sourceTexturePath.c_str() ? a_mat->sourceTexturePath.c_str() : "");
					b.sprite = src.find("dwardglowenb") != std::string::npos;
					b.scale = a_mat->baseColorScale;
					b.loadStart = a_mat->falloffStartOpacity;
					b.loadStop = a_mat->falloffStopOpacity;
					{
						const auto lm = Lower(model);
						const bool normalDome = lm.find("wardbodyfx") != std::string::npos && lm.find("360") == std::string::npos;
						// layer 1 = the vapour block on the lower palette row (0.39; layer 2 sits at 0.74)
						b.vapourOne = normalDome && b.palette && src.find("vapor") != std::string::npos && a_mat->baseColor.red < 0.5f;
						if (b.vapourOne) {
							b.paletteAlpha = a_prop->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kGrayscaleToPaletteAlpha);
							b.source = a_mat->sourceTexture;
							b.sourcePath = a_mat->sourceTexturePath;
							++gGoldVapourBlocks;
						}
					}
					b.alphaDead = b.palette && a_mat->baseColor.alpha < 0.01f;
					if (const auto& ap = a_geometry->GetGeometryRuntimeData().alphaProperty) {
						b.additive = ap->GetAlphaBlending() && ap->GetDestBlendMode() == RE::NiAlphaProperty::AlphaFunction::kOne;
					}
					// a dome's fill: falloff on, and more opaque facing you (the larger cos angle) than at the rim
					if (a_prop->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kFalloff) &&
						Lower(model).find("wardbodyfx") != std::string::npos) {
						b.startFaces = a_mat->falloffStartAngle >= a_mat->falloffStopAngle;
						b.facing = b.startFaces ? a_mat->falloffStartOpacity : a_mat->falloffStopOpacity;
						const float rim = b.startFaces ? a_mat->falloffStopOpacity : a_mat->falloffStartOpacity;
						b.fill = b.facing > rim;
					}
					if (b.palette && a_mat->greyscaleTexturePath.c_str() && *a_mat->greyscaleTexturePath.c_str()) {
						paths.insert(a_mat->greyscaleTexturePath.c_str());
						const auto lm = Lower(model);
						if (lm.find("wardinhandfx") != std::string::npos) {
							handPaths.insert(a_mat->greyscaleTexturePath.c_str());
						} else if (lm.find("wardbodyfx") != std::string::npos && lm.find("360") == std::string::npos) {
							normalDomePaths.insert(a_mat->greyscaleTexturePath.c_str());
						}
					}
					// the colour controllers come off the cached model once: a clone of it has none
					std::vector<RE::NiTimeController*> colour;
					for (auto* c = a_prop->GetControllers(); c; c = c->GetNext()) {
						const auto* rtti = c->GetRTTI();
						if (rtti && rtti->name && std::string_view(rtti->name).find("ColorController") != std::string_view::npos) {
							colour.push_back(c);
						}
					}
					for (auto* c : colour) {
						c->IncRefCount();  // a sequence may still name it
						a_prop->RemoveController(c);
						++m.controllersOff;
					}
					m.blocks.push_back(std::move(b));
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

		// one row's look: what PaintRow puts on the cached models, and what a color drag puts on the live domes alone
		struct Look
		{
			Color                c = 0;
			float                wardGlow = 1.0f, wardAlpha = 1.0f, clear = 1.0f, castingGlow = 1.0f, kept = 0.0f;
			std::array<float, 3> sprite{};
			bool                 goldVapour = false;
		};

		Look LookOf(const Setting& a_s)
		{
			return { a_s.c, a_s.brightness / 100.0f, a_s.opacity / 100.0f, 1.0f - a_s.transparency / 100.0f, a_s.castingGlow / 100.0f, a_s.kept,
				SpriteRgb(a_s.c, kSpriteSat), GoldVapour(a_s.c) && GoldVapourTexture() };
		}

		void PaintPalettes(Row& a_row, const Look& a_look)
		{
			for (auto& p : a_row.palettes) {
				const float want = p.hand ? 0.0f : a_look.kept;  // the hand art is the casting art's look, not a dome's
				if (p.applied == a_look.c && p.kept == want) {
					continue;
				}
				auto& s = LoadSource(p.stem);
				if (!s.ok) {
					++gSwapFails;
					continue;
				}
				if (Swap(p, s, TrueOnScreen(a_look.c, want))) {
					p.applied = a_look.c;
					p.kept = want;
					++gSwaps;
				} else {
					++gSwapFails;
				}
			}
		}

		// a block's material in a look, built aside from the block's own (read only, never changed here); a_plainVapour says
		// whether vapour layer 1 wears the baked gold copy, so the caller sets that block's palette bits. nullptr: none made
		RE::BSEffectShaderMaterial* Fresh(const Block& a_blk, bool a_hand, const Look& a_look, bool& a_plainVapour)
		{
			auto* mat = static_cast<RE::BSEffectShaderMaterial*>(a_blk.prop->GetMaterial());
			auto* fresh = mat ? static_cast<RE::BSEffectShaderMaterial*>(mat->Create()) : nullptr;
			if (!fresh) {
				return nullptr;
			}
			fresh->CopyMembers(mat);
			// the ward in the hands answers to the casting glow slider alone; opacity and ward brightness are the wards' own
			// (his rule 2026-10-05)
			const float glow = a_hand ? a_look.castingGlow : a_look.wardGlow;
			const float alpha = a_hand ? 1.0f : a_look.wardAlpha;
			const auto [r, g, b] = LightRgb(TrueOnScreen(a_look.c, a_hand ? 0.0f : a_look.kept), false);
			const float m = (std::max)({ r, g, b, 0.001f });
			const float bright = (std::max)({ a_blk.base.red, a_blk.base.green, a_blk.base.blue });
			a_plainVapour = a_blk.vapourOne && a_look.goldVapour;
			if (a_blk.vapourOne) {
				// the baked copy carries the palette's value and alpha itself, so the block becomes a plain glow in the ward's
				// color; any other hue (or lighting) gets the palette layer back exactly as loaded
				fresh->sourceTexture = a_plainVapour ? RE::NiPointer<RE::NiSourceTexture>(gGoldVapour) : a_blk.source;
				fresh->sourceTexturePath = a_plainVapour ? RE::BSFixedString(kGoldVapour) : a_blk.sourcePath;
			}
			if (a_blk.sprite) {
				fresh->baseColor.red = a_look.sprite[0] * alpha;
				fresh->baseColor.green = a_look.sprite[1] * alpha;
				fresh->baseColor.blue = a_look.sprite[2] * alpha;
			} else if (a_plainVapour) {
				fresh->baseColor.red = r / m;
				fresh->baseColor.green = g / m;
				fresh->baseColor.blue = b / m;
			} else if (a_blk.vapourOne) {
				fresh->baseColor.red = a_blk.base.red;  // the palette row coordinate, as loaded
				fresh->baseColor.green = a_blk.base.green;
				fresh->baseColor.blue = a_blk.base.blue;
			} else if (!a_blk.palette && bright > 0.0001f) {
				fresh->baseColor.red = r / m * bright;
				fresh->baseColor.green = g / m * bright;
				fresh->baseColor.blue = b / m * bright;
			}
			fresh->baseColor.alpha = a_plainVapour ? alpha : a_blk.base.alpha * (a_blk.sprite ? 1.0f : alpha);
			fresh->baseColorScale = a_blk.scale * glow * (a_blk.alphaDead && a_blk.additive && !a_plainVapour ? alpha : 1.0f);
			fresh->falloffStartOpacity = a_blk.loadStart;
			fresh->falloffStopOpacity = a_blk.loadStop;
			if (a_blk.fill) {
				(a_blk.startFaces ? fresh->falloffStartOpacity : fresh->falloffStopOpacity) = a_blk.facing * a_look.clear;
			}
			if (a_blk.alphaDead && !a_blk.additive) {  // not seen in our meshes; the falloff is the alpha the shader does read
				fresh->falloffStartOpacity *= alpha;
				fresh->falloffStopOpacity *= alpha;
			}
			return fresh;
		}

		// vapour layer 1's palette bits: off while it wears the baked gold copy, as loaded otherwise
		void VapourBits(RE::BSEffectShaderProperty* a_prop, const Block& a_blk, bool a_plain)
		{
			using F = RE::BSShaderProperty::EShaderPropertyFlag;
			if (a_plain) {
				a_prop->flags.reset(F::kGrayscaleToPaletteColor, F::kGrayscaleToPaletteAlpha);
				return;
			}
			a_prop->flags.set(F::kGrayscaleToPaletteColor);
			if (a_blk.paletteAlpha) {
				a_prop->flags.set(F::kGrayscaleToPaletteAlpha);
			}
		}

		void PutMaterial(RE::BSEffectShaderProperty* a_prop, RE::BSEffectShaderMaterial* a_fresh)
		{
			a_prop->SetMaterial(a_fresh, true);
			if (a_prop->GetMaterial() != a_fresh) {
				a_fresh->~BSEffectShaderMaterial();
				RE::free(a_fresh);
			}
		}

		void PaintRow(Row& a_row, const Setting& a_s)
		{
			const auto look = LookOf(a_s);
			PaintPalettes(a_row, look);
			for (auto& master : a_row.masters) {
				for (auto& blk : master.blocks) {
					bool  plain = false;
					auto* fresh = Fresh(blk, master.hand, look, plain);
					if (!fresh) {
						continue;
					}
					if (blk.vapourOne) {
						VapourBits(blk.prop.get(), blk, plain);
					}
					PutMaterial(blk.prop.get(), fresh);
					++gEdits;
				}
			}
			a_row.applied = a_s;
		}

		// A dome already on screen (a held ward's, the menu's preview) is a CLONE of the cached model, made before the change:
		// it shares the palette textures (so those follow at once) but carries its own copy of every block's glow, so its
		// emissive blocks kept the old color until the next cast (his ask 2026-10-06: drag the color bar and watch the ward
		// "skim through the color spectrum"). Every live dome of a row is given its row's look again, block for block - a clone
		// walks its geometry in the same order as the model it was cloned from: a_looks[row] built aside during a color drag,
		// else the cached model's materials. The cached models change only outside a drag: a loader thread copying one while
		// its materials were replaced ~15 times a second could read one being freed. Main thread.
		std::size_t RepaintLive(const std::array<std::optional<Look>, kRows>* a_looks)
		{
			auto* lists = RE::ProcessLists::GetSingleton();
			if (!lists) {
				return 0;
			}
			std::map<std::string, std::pair<const Master*, std::size_t>> byModel;
			for (std::size_t i = 0; i < kRows; ++i) {
				if (a_looks && !(*a_looks)[i]) {
					continue;
				}
				for (const auto& m : gRows[i].masters) {
					byModel.emplace(ModelKey(m.model), std::pair{ &m, i });
				}
			}
			std::vector<std::tuple<RE::NiPointer<RE::NiAVObject>, const Master*, std::size_t>> live;
			{
				RE::BSSpinLockGuard guard{ lists->magicEffectsLock };
				for (auto& temp : lists->magicEffects) {
					auto* e = temp.get();
					if (!e || e->GetType() != RE::TEMP_EFFECT_TYPE::kRefModel) {
						continue;
					}
					auto* m = static_cast<RE::ModelReferenceEffect*>(e);
					if (m->finished || !m->artObject || !m->artObject3D) {
						continue;
					}
					const auto* model = m->artObject->GetModel();
					if (const auto it = byModel.find(ModelKey(model ? model : "")); it != byModel.end()) {
						live.emplace_back(m->artObject3D, it->second.first, it->second.second);
					}
				}
			}
			std::size_t painted = 0;
			for (const auto& [root, master, row] : live) {
				std::vector<RE::BSEffectShaderProperty*> props;
				EachEffectBlock(root.get(), [&](RE::BSGeometry*, RE::BSEffectShaderProperty* a_prop, RE::BSEffectShaderMaterial*) { props.push_back(a_prop); });
				if (props.size() != master->blocks.size()) {
					continue;  // not the shape the master was read from: leave it to the next cast
				}
				using F = RE::BSShaderProperty::EShaderPropertyFlag;
				for (std::size_t i = 0; i < props.size(); ++i) {
					const auto& blk = master->blocks[i];
					if (a_looks) {
						bool  plain = false;
						auto* fresh = Fresh(blk, master->hand, *(*a_looks)[row], plain);
						if (!fresh) {
							continue;
						}
						if (blk.vapourOne) {
							VapourBits(props[i], blk, plain);
						}
						PutMaterial(props[i], fresh);
						continue;
					}
					auto* from = static_cast<RE::BSEffectShaderMaterial*>(blk.prop->GetMaterial());
					auto* fresh = from ? static_cast<RE::BSEffectShaderMaterial*>(from->Create()) : nullptr;
					if (!fresh) {
						continue;
					}
					fresh->CopyMembers(from);
					// the gold vapour switch moves the palette bits on the master's block; the clone follows it
					for (const auto f : { F::kGrayscaleToPaletteColor, F::kGrayscaleToPaletteAlpha }) {
						if (blk.prop->flags.any(f)) {
							props[i]->flags.set(f);
						} else {
							props[i]->flags.reset(f);
						}
					}
					PutMaterial(props[i], fresh);
				}
				++painted;
			}
			return painted;
		}

		std::atomic<bool>                     gLiveQueued{ false };
		std::chrono::steady_clock::time_point gLiveLast{};
	}

	void LiveRecolor()
	{
		// the menu's render thread, once per frame while a color is dragged: at most ~15 recolors a second, one queued at a
		// time - colors only (no save, no re-equip of a ward in the hands; both happen once the mouse is let go)
		const auto now = std::chrono::steady_clock::now();
		if (now - gLiveLast < std::chrono::milliseconds(66) || gLiveQueued.exchange(true)) {
			return;
		}
		gLiveLast = now;
		if (!Later("live recolor", []() {
				gLiveQueued = false;
				ApplyColors(true);
			})) {
			gLiveQueued = false;  // nothing queued: the next frame of the drag tries again
		}
	}

	std::array<bool, kRows> TakeHandLookChanges()
	{
		std::scoped_lock        l{ gLock };
		std::array<bool, kRows> out{};
		for (std::size_t i = 0; i < kRows; ++i) {
			auto& r = gRows[i];
			if (r.applied.c == 0xFFFFFFFF) {
				continue;  // not painted (a Vanilla row, or not loaded): our hand art is not what that row's hands wear
			}
			out[i] = r.applied.c != r.handSeen || r.applied.castingGlow != r.handGlowSeen;
			r.handSeen = r.applied.c;
			r.handGlowSeen = r.applied.castingGlow;
		}
		return out;
	}

	void RegisterRowModels(std::size_t a_row, std::vector<std::string> a_models)
	{
		std::scoped_lock l{ gLock };
		if (a_row < kRows) {
			gRows[a_row].models = std::move(a_models);
		}
	}

	bool ApplyColors(bool a_quiet)
	{
		const int                              opacity = Opacity();
		const int                              transparency = Transparency();
		const int                              brightness = Brightness();
		const int                              castingGlow = CastingGlow();
		bool                                   changed = false, liveStale = false;
		std::array<std::optional<Look>, kRows> drag{};
		std::scoped_lock                       l{ gLock };
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
			const Setting want{ *c, opacity, transparency, brightness, castingGlow, DomeKept() };
			if (a_quiet) {
				// a color drag (~15 a second): the palettes and the live domes; the cached models follow when the mouse is let go.
				// Every live dome of a dragged row is painted each round, so one cast mid-drag (a clone of the old model) follows too,
				// and so does a drag that comes back to the cached models' own look
				const bool moved = row.live != want;
				if (moved) {
					PaintPalettes(row, LookOf(want));
					row.live = want;
					changed = true;
				}
				if (moved || row.applied != want) {
					drag[i] = LookOf(want);
				}
				continue;
			}
			if (row.applied != want || std::ranges::any_of(row.palettes, [&](const Palette& p) { return p.applied != *c; })) {
				PaintRow(row, want);
				changed = true;
			}
			liveStale = liveStale || row.live != want;
			row.live = want;
		}
		if (a_quiet) {
			gLiveDomes = std::ranges::any_of(drag, [](const auto& a_d) { return a_d.has_value(); }) ? RepaintLive(&drag) : 0;
			return changed;  // the log keeps the apply made when the mouse is let go
		}
		gLiveDomes = changed || liveStale ? RepaintLive(nullptr) : 0;  // a call that changed nothing repainted nothing (CodeRabbit, PR #3)
		SKSE::log::info("colors: {} palette(s) swapped, {} failed, {} glow block(s) set, {} live dome(s) repainted{}", gSwaps, gSwapFails,
			gEdits, gLiveDomes, gSwapFails ? " - " + gLastProblem : "");
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
				r.applied.c == 0xFFFFFFFF ? std::string() : HexColor(r.applied.c), pals);
		}
		return std::format(R"({{"swaps":{},"swapFails":{},"glowEdits":{},"liveDomes":{},"missingModels":{},"domeKept":{:.2f},"goldVapour":{{"blocks":{},"texture":{}}},"lastProblem":"{}","rows":{{{}}}}})",
			gSwaps, gSwapFails, gEdits, gLiveDomes, gMissing, DomeKept(), gGoldVapourBlocks, gGoldVapour != nullptr, JsonEscape(gLastProblem), rows);
	}

	void SetDomeKeptTest(int a_percent)
	{
		gKeptTest = std::clamp(a_percent, -1, 100);
	}

	void ReleaseOldTextures()
	{
		std::scoped_lock l{ gLock };
		Bury();
	}
}
