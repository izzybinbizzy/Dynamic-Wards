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
			const auto [r, g, b] = LightRgb(a_c, false);
			const float m = (std::max)({ r, g, b, 0.001f });
			const auto  sprite = SpriteRgb(a_c, kSpriteSat);
			const bool  goldVapour = GoldVapour(a_c) && GoldVapourTexture();
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
						fresh->baseColor.red = sprite[0] * alpha;
						fresh->baseColor.green = sprite[1] * alpha;
						fresh->baseColor.blue = sprite[2] * alpha;
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

		// A dome already on screen (a held ward's, the menu's preview) is a CLONE of the cached model, made before the change:
		// it shares the palette textures (so those follow at once) but carries its own copy of every block's glow, so its
		// emissive blocks kept the old color until the next cast (his ask 2026-10-06: drag the color bar and watch the ward
		// "skim through the color spectrum"). Every live dome of a row is given the master's materials again, block for
		// block - a clone walks its geometry in the same order as the model it was cloned from. Main thread (ApplyColors).
		std::size_t RepaintLive()
		{
			auto* lists = RE::ProcessLists::GetSingleton();
			if (!lists) {
				return 0;
			}
			std::map<std::string, const Master*> byModel;
			for (const auto& row : gRows) {
				for (const auto& m : row.masters) {
					byModel.emplace(ModelKey(m.model), &m);
				}
			}
			std::vector<std::pair<RE::NiPointer<RE::NiAVObject>, const Master*>> live;
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
						live.emplace_back(m->artObject3D, it->second);
					}
				}
			}
			std::size_t painted = 0;
			for (const auto& [root, master] : live) {
				std::vector<RE::BSEffectShaderProperty*> props;
				EachEffectBlock(root.get(), [&](RE::BSGeometry*, RE::BSEffectShaderProperty* a_prop, RE::BSEffectShaderMaterial*) { props.push_back(a_prop); });
				if (props.size() != master->blocks.size()) {
					continue;  // not the shape the master was read from: leave it to the next cast
				}
				using F = RE::BSShaderProperty::EShaderPropertyFlag;
				for (std::size_t i = 0; i < props.size(); ++i) {
					auto* from = static_cast<RE::BSEffectShaderMaterial*>(master->blocks[i].prop->GetMaterial());
					auto* fresh = from ? static_cast<RE::BSEffectShaderMaterial*>(from->Create()) : nullptr;
					if (!fresh) {
						continue;
					}
					fresh->CopyMembers(from);
					// the gold vapour switch moves the palette bits on the master's block; the clone follows it
					for (const auto f : { F::kGrayscaleToPaletteColor, F::kGrayscaleToPaletteAlpha }) {
						if (master->blocks[i].prop->flags.any(f)) {
							props[i]->flags.set(f);
						} else {
							props[i]->flags.reset(f);
						}
					}
					props[i]->SetMaterial(fresh, true);
					if (props[i]->GetMaterial() != fresh) {
						fresh->~BSEffectShaderMaterial();
						RE::free(fresh);
					}
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
			if (r.applied == 0xFFFFFFFF) {
				continue;  // not painted (a Vanilla row, or not loaded): our hand art is not what that row's hands wear
			}
			out[i] = r.applied != r.handSeen || r.appliedCastingGlow != r.handGlowSeen;
			r.handSeen = r.applied;
			r.handGlowSeen = r.appliedCastingGlow;
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
		const int        opacity = Opacity();
		const int        transparency = Transparency();
		const int        brightness = Brightness();
		const int        castingGlow = CastingGlow();
		bool             changed = false;
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
		gLiveDomes = changed ? RepaintLive() : 0;  // a call that changed nothing repainted nothing (CodeRabbit, PR #3)
		ColorHandLights();
		if (a_quiet) {
			return changed;  // a drag in the menu: ~15 a second, the log keeps the one made when the mouse is let go
		}
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
				r.applied == 0xFFFFFFFF ? std::string() : HexColor(r.applied), pals);
		}
		return std::format(R"({{"swaps":{},"swapFails":{},"glowEdits":{},"liveDomes":{},"missingModels":{},"goldVapour":{{"blocks":{},"texture":{}}},"lastProblem":"{}","rows":{{{}}}}})",
			gSwaps, gSwapFails, gEdits, gLiveDomes, gMissing, gGoldVapourBlocks, gGoldVapour != nullptr, JsonEscape(gLastProblem), rows);
	}
}
