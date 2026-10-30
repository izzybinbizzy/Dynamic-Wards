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
//   2. the GLOW of its other blocks (their emissive), on each COPY of a ward's model: a copy the moment it shows (its art
//      effect's first update - CopyHook below) and every copy on screen when the look changes. A block whose emissive is a
//      color takes the ward's hue at its own brightness (wardgen.retint); a palette block's emissive is a coordinate into its
//      palette and keeps its number. The ENB light sprite takes the ward's color at full saturation. The cached MODEL the game
//      copies from (BSModelDB) is painted once, at the first apply before anything copies it, and never again: a loader
//      thread copying it while its materials were replaced could read one being freed.
//   3. its OPACITY: every block's alpha, by the menu's slider (the lights dim with it, Lighting.cpp / DomeLights.cpp); a
//      palette block that reads no alpha from its emissive is dimmed through its glow strength instead (Block::alphaDead).
//   4. its BRIGHTNESS: every block's glow strength (emissive multiple), by Ward brightness (the ward in the hands: Hand
//      Brightness) - the lights follow it.
//   5. its TRANSPARENCY (domes only): a dome's FILL - a falloff block more opaque facing you than at its rim (the 360 dome's
//      cloud layer, 90% facing you) - loses that much of its facing opacity; the rim and the glow stay (his ask 2026-10-03,
//      tested on ENB, Vanilla and Community Shaders).
// A colour controller writes its block's emissive from the keys of the model's sequences every frame (clearing kActive
// does not stop the sequence that drives it). On a palette block that emissive is a row coordinate, so its controllers come
// off the cached model once (a clone made after has none). On a plain block the keys ARE the fade in and the fade out - the
// 360 dome's three outer layers fade only by their color - so its controllers stay and each key is written as the row's hue
// at that key's own brightness.

#include "Plugin.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <thread>
#include <tuple>

namespace Plugin
{
	namespace
	{
		// (the palette recolor itself - wardgen.pal_retint's curve, the file checks - is game-free, in WardMath.h, and tested)
		constexpr const char* kPaletteDir = "Data/SKSE/Plugins/Dynamic Wards/Palettes/";
		constexpr float       kSpriteSat = 0.75f;  // the ENB light: the ward's hue at least this saturated (a pale light washes it out)
		// the ward in the hand, the normal dome, and every dome on the Vanilla pick: every texel tinted fully toward the ward's
		// color, so no white-hot core reads washed out beside the casting art (his calls; the 360 sphere keeps its white core)
		constexpr float kCore = 1.0f;

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
			int                                lighting = -1;       // the lighting it was made for: Swap's core follows it
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
			float                                     bright = 0.0f;       // its loaded emissive's brightest channel: a plain block's glow keeps it
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
			// a plain block's colour tracks: their place in the controller manager (a clone keeps the places) and each key's own
			// brightness as the neutral model has it
			struct Track
			{
				std::uint16_t      seq = 0;  // NiTArray's own index type
				std::uint32_t      item = 0;
				std::vector<float> grey;
			};
			std::vector<Track> tracks{};
			float              full = 0.0f;  // the brightest key of its tracks
		};

		struct Master
		{
			std::string               model;
			RE::NiPointer<RE::NiNode> root;
			std::vector<Block>        blocks;
			std::size_t               controllersOff = 0;
			std::size_t               tracks = 0;
			bool                      hand = false;  // the casting art (wardinhandfx): the casting glow slider, never opacity or ward brightness
		};

		// the settings one look of a row was made from
		struct Setting
		{
			Color c = 0xFFFFFFFF;
			int   opacity = -1, transparency = -1, brightness = -1, castingGlow = -1;
			float kept = -1.0f;
			int   lighting = -1;  // the lighting it was made for: the palettes' core and the gold vapour follow it
			bool  operator==(const Setting&) const = default;
		};

		struct Row
		{
			std::vector<std::string> models;
			std::vector<Master>      masters;
			std::vector<Palette>     palettes;
			bool                     loaded = false;
			Setting                  applied{};  // the look the last apply (not a color drag) settled on
			Setting                  live{};     // the look its copies wear: the applied one, or a color drag's
		};

		struct Grave
		{
			REX::W32::ID3D11Resource*             tex{ nullptr };
			REX::W32::ID3D11ShaderResourceView*   srv{ nullptr };
			std::chrono::steady_clock::time_point at;
		};

		std::mutex                                            gLock;
		std::array<Row, kSlots>                               gRows;
		std::map<std::string, Source>                         gSources;
		std::vector<Grave>                                    gGrave;
		std::map<const RE::BSGraphics::Texture*, std::string> gTextureOf;  // the palette path that reached each texture first
		std::size_t                                           gSwaps = 0, gSwapFails = 0, gEdits = 0, gMissing = 0, gLiveDomes = 0, gNewCopies = 0;
		std::string                                           gLastProblem = "none";

		// each art object of ours and the slot and model it shows: written at data load (RegisterRowArt), read-only once gReady
		struct ArtOf
		{
			std::size_t slot{ kSlots };
			std::string model{};
		};
		std::unordered_map<const RE::BGSArtObject*, ArtOf> gArtOf;
		std::atomic_bool                                   gReady{ false };  // the first apply is done: the cached models are never painted again
		// a copy of one of our models on screen and the look it wears (gLock). Its reference is taken by hand, so its address is
		// never reused while it is listed and no static destructor releases it at exit, after the engine is gone; ReleaseOld
		// gives it back once nothing else holds it
		struct Copy
		{
			RE::NiAVObject* root{ nullptr };
			Setting         with{};
		};
		std::vector<Copy> gCopies;
		std::size_t       gCopiesLogged = 0;  // gNewCopies at the last apply's log line
		// vapour layer 1's palette lookup baked into a plain texture (value in rgb, the palette's alpha in alpha), shipped with
		// every install; worn only on Community Shaders and only by a gold or orange ward (GoldVapour)
		constexpr const char*              kGoldVapour = "textures\\effects\\VaporTDWrdG.dds";
		RE::NiPointer<RE::NiSourceTexture> gGoldVapour;
		bool                               gGoldVapourTried = false;
		std::size_t                        gGoldVapourBlocks = 0;

		// the share of a color's saturation the domes are corrected for (WardMath.h TrueOnScreen); 0 = no correction: the menu's
		// Dome color boost, or the devbench test switch domeKept (percent, -1 = the boost), never saved
		std::atomic<int> gKeptTest{ -1 };

		float DomeKept()
		{
			const int test = gKeptTest.load();
			return test >= 0 ? test / 100.0f : KeptForBoost(DomeBoost());
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
			const float                            core = a_p.hand || a_p.normalDome || LightingPick() == Lighting::kVanilla ? kCore : 0.0f;
			std::vector<std::vector<std::uint8_t>> mips{ Retint(a_s.image, a_c, core) };
			std::vector<std::pair<int, int>>       sizes{ { a_s.image.w, a_s.image.h } };
			while (sizes.back().first > 1 || sizes.back().second > 1) {
				const auto [pw, ph] = sizes.back();
				mips.push_back(NextMip(mips.back(), pw, ph));
				sizes.emplace_back((std::max)(1, pw / 2), (std::max)(1, ph / 2));
			}
			std::vector<REX::W32::D3D11_SUBRESOURCE_DATA> init(mips.size());
			for (std::size_t i = 0; i < mips.size(); ++i) {
				init[i].sysMem = mips[i].data();
				init[i].sysMemPitch = static_cast<std::uint32_t>(sizes[i].first) * 4;
				init[i].sysMemSlicePitch = 0;
			}
			const REX::W32::D3D11_TEXTURE2D_DESC d{ .width = static_cast<std::uint32_t>(a_s.image.w),
				.height = static_cast<std::uint32_t>(a_s.image.h),
				.mipLevels = static_cast<std::uint32_t>(mips.size()),
				.arraySize = 1,
				.format = REX::W32::DXGI_FORMAT_B8G8R8A8_UNORM,
				.sampleDesc = { .count = 1, .quality = 0 },
				.usage = REX::W32::D3D11_USAGE_IMMUTABLE,
				.bindFlags = REX::W32::D3D11_BIND_SHADER_RESOURCE,
				.cpuAccessFlags = 0,
				.miscFlags = 0 };
			REX::W32::ID3D11Texture2D*           tex = nullptr;
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

		bool IsColour(const RE::NiTimeController* a_c)
		{
			const auto* rtti = a_c ? a_c->GetRTTI() : nullptr;
			return rtti && rtti->name && std::string_view(rtti->name).find("ColorController") != std::string_view::npos;
		}

		std::size_t RemoveColour(RE::BSEffectShaderProperty* a_prop, const std::vector<RE::NiTimeController*>& a_keep = {})
		{
			std::vector<RE::NiTimeController*> colour;
			for (auto* c = a_prop->GetControllers(); c; c = c->GetNext()) {
				if (IsColour(c) && std::ranges::find(a_keep, c) == a_keep.end()) {
					colour.push_back(c);
				}
			}
			for (auto* c : colour) {
				c->IncRefCount();  // a sequence may still name it
				a_prop->RemoveController(c);
			}
			return colour.size();
		}

		RE::NiControllerManager* ManagerOf(RE::NiAVObject* a_root)
		{
			for (auto* c = a_root ? a_root->GetControllers() : nullptr; c; c = c->GetNext()) {
				if (auto* m = netimmerse_cast<RE::NiControllerManager*>(c)) {
					return m;
				}
			}
			return nullptr;
		}

		// the colour controllers a plain block's recorded tracks drive - the ones TintTracks keeps in the row's hue
		std::vector<RE::NiTimeController*> Tinted(const Master& a_m, const Block& a_b)
		{
			std::vector<RE::NiTimeController*> out;
			auto*                              mgr = ManagerOf(a_m.root.get());
			for (const auto& t : a_b.tracks) {
				if (auto* seq = mgr ? mgr->sequenceArray[t.seq].get() : nullptr; seq && t.item < seq->arraySize) {
					out.push_back(seq->interpArray[t.item].interpCtlr.get());
				}
			}
			return out;
		}

		RE::NiKeyBasedInterpolator* ColourKeys(RE::NiInterpolator* a_interp)
		{
			auto* k = netimmerse_cast<RE::NiKeyBasedInterpolator*>(a_interp);
			if (!k || k->GetKeyChannelCount() != 1 || k->GetKeyContent(0) != RE::NiKeyBasedInterpolator::KeyContent::kPos || !k->GetKeyArray(0) ||
				k->GetKeyStride(0) < sizeof(float) + sizeof(RE::NiPoint3)) {
				return nullptr;
			}
			return k;
		}

		// a key is its time, then its value
		RE::NiPoint3& KeyValue(RE::NiKeyBasedInterpolator* a_keys, std::uint32_t a_index)
		{
			auto* at = static_cast<std::byte*>(a_keys->GetKeyArray(0)) + static_cast<std::size_t>(a_index) * a_keys->GetKeyStride(0) + sizeof(float);
			return *reinterpret_cast<RE::NiPoint3*>(at);
		}

		void FindTracks(Master& a_m)
		{
			auto* mgr = ManagerOf(a_m.root.get());
			if (!mgr) {
				return;
			}
			for (std::uint16_t s = 0; s < mgr->sequenceArray.capacity(); ++s) {
				auto* seq = mgr->sequenceArray[s].get();
				for (std::uint32_t i = 0; seq && i < seq->arraySize; ++i) {
					const auto& item = seq->interpArray[i];
					if (!item.interpCtlr || !IsColour(item.interpCtlr.get())) {
						continue;
					}
					const auto blk = std::ranges::find_if(a_m.blocks, [&](const Block& a_b) { return a_b.prop.get() == item.interpCtlr->target; });
					auto*      keys = ColourKeys(item.interpolator.get());
					if (blk == a_m.blocks.end() || blk->palette || !keys) {
						continue;
					}
					Block::Track t{ s, i, {} };
					for (std::uint32_t n = 0; n < keys->GetKeyCount(0); ++n) {
						const auto& v = KeyValue(keys, n);
						t.grey.push_back((std::max)({ v.x, v.y, v.z }));
						blk->full = (std::max)(blk->full, t.grey.back());
					}
					blk->tracks.push_back(std::move(t));
				}
			}
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
					b.bright = (std::max)({ a_mat->baseColor.red, a_mat->baseColor.green, a_mat->baseColor.blue });
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
					if (b.palette) {
						m.controllersOff += RemoveColour(a_prop);
					}
					m.blocks.push_back(std::move(b));
				});
				FindTracks(m);
				for (auto& blk : m.blocks) {
					if (!blk.palette && (blk.tracks.empty() || blk.full <= 0.0f)) {
						blk.tracks.clear();
						m.controllersOff += RemoveColour(blk.prop.get());  // keys we could not read: the static color wins
					} else if (!blk.palette) {
						m.controllersOff += RemoveColour(blk.prop.get(), Tinted(m, blk));  // an unread one would write its neutral keys over ours
					}
					m.tracks += blk.tracks.size();
				}
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

		// one row's look: what a copy of its models (and the cached models, once) is painted in
		struct Look
		{
			Color                c = 0;
			float                wardGlow = 1.0f, wardAlpha = 1.0f, clear = 1.0f, castingGlow = 1.0f, kept = 0.0f;
			std::array<float, 3> sprite{};
			bool                 goldVapour = false;
			int                  lighting = -1;
		};

		Look LookOf(const Setting& a_s)
		{
			return { a_s.c, a_s.brightness / 100.0f, a_s.opacity / 100.0f, 1.0f - a_s.transparency / 100.0f, a_s.castingGlow / 100.0f, a_s.kept,
				SpriteRgb(a_s.c, kSpriteSat), GoldVapour(a_s.c) && GoldVapourTexture(), a_s.lighting };
		}

		// two palette paths on one texture object: the engine's stand-in for a missing file (a partial install), never recolored
		bool SharedTexture(const Palette& a_p)
		{
			const auto* rt = a_p.tex ? a_p.tex->rendererTexture : nullptr;
			if (!rt) {
				return false;
			}
			const auto [it, fresh] = gTextureOf.try_emplace(rt, a_p.path);
			if (fresh || SameText(it->second, a_p.path)) {
				return false;
			}
			gLastProblem = std::format("{}: the same texture as {} (a missing file?) - left as it is", a_p.path, it->second);
			return true;
		}

		void PaintPalettes(Row& a_row, const Look& a_look)
		{
			for (auto& p : a_row.palettes) {
				const float want = p.hand ? 0.0f : a_look.kept;  // the hand art is the casting art's look, not a dome's
				if (p.applied == a_look.c && p.kept == want && p.lighting == a_look.lighting) {
					continue;
				}
				auto& s = LoadSource(p.stem);
				if (!s.ok || SharedTexture(p)) {
					// a missing source never comes back, nor a texture another path reached: tried once per color, so it does not
					// force a repaint on every apply
					++gSwapFails;
					p.applied = a_look.c;
					p.kept = want;
					p.lighting = a_look.lighting;
					continue;
				}
				if (Swap(p, s, TrueOnScreen(a_look.c, want))) {
					p.applied = a_look.c;
					p.kept = want;
					p.lighting = a_look.lighting;
					++gSwaps;
				} else {
					++gSwapFails;
				}
			}
		}

		// the look's hue at full brightness (with the domes' screen correction; the hand art takes none)
		RE::NiPoint3 PlainHue(bool a_hand, const Look& a_look)
		{
			const auto [r, g, b] = LightRgb(TrueOnScreen(a_look.c, a_hand ? 0.0f : a_look.kept), false);
			const float m = (std::max)({ r, g, b, 0.001f });
			return { r / m, g / m, b / m };
		}

		// the hue a block's color and its colour keys scale, at full brightness; nothing for a palette block (its emissive is a
		// row coordinate)
		std::optional<RE::NiPoint3> UnitHue(const Block& a_blk, bool a_hand, const Look& a_look)
		{
			if (a_blk.sprite) {
				const float alpha = a_hand ? 1.0f : a_look.wardAlpha;
				return RE::NiPoint3{ a_look.sprite[0] * alpha, a_look.sprite[1] * alpha, a_look.sprite[2] * alpha };
			}
			if (a_blk.palette) {
				return std::nullopt;
			}
			return PlainHue(a_hand, a_look);
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
			a_plainVapour = a_blk.vapourOne && a_look.goldVapour;
			if (a_blk.vapourOne) {
				// the baked copy carries the palette's value and alpha itself, so the block becomes a plain glow in the ward's
				// color; any other hue (or lighting) gets the palette layer back exactly as loaded
				fresh->sourceTexture = a_plainVapour ? RE::NiPointer<RE::NiSourceTexture>(gGoldVapour) : a_blk.source;
				fresh->sourceTexturePath = a_plainVapour ? RE::BSFixedString(kGoldVapour) : a_blk.sourcePath;
			}
			const auto put = [&](const RE::NiPoint3& a_rgb) {
				fresh->baseColor.red = a_rgb.x;
				fresh->baseColor.green = a_rgb.y;
				fresh->baseColor.blue = a_rgb.z;
			};
			if (a_plainVapour) {
				put(PlainHue(a_hand, a_look));
			} else if (a_blk.vapourOne) {
				put({ a_blk.base.red, a_blk.base.green, a_blk.base.blue });  // the palette row coordinate, as loaded
			} else if (const auto hue = UnitHue(a_blk, a_hand, a_look); hue && (a_blk.sprite || a_blk.bright > 0.0001f)) {
				put(*hue * (a_blk.sprite ? 1.0f : a_blk.bright));
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

		// a model's or a live clone's colour keys in a look (a_props: its blocks in the model's order)
		void TintTracks(RE::NiAVObject* a_root, const Master& a_m, const std::vector<RE::BSEffectShaderProperty*>& a_props, const Look& a_look)
		{
			auto* mgr = ManagerOf(a_root);
			if (!mgr || a_props.size() != a_m.blocks.size()) {
				return;
			}
			for (std::size_t b = 0; b < a_m.blocks.size(); ++b) {
				const auto& blk = a_m.blocks[b];
				const auto  hue = blk.tracks.empty() ? std::nullopt : UnitHue(blk, a_m.hand, a_look);
				if (!hue) {
					continue;
				}
				// the brightest key wears the glow the block was painted with statically: its loaded emissive (a sprite: full)
				const float level = blk.sprite ? 1.0f : blk.bright;
				for (const auto& t : blk.tracks) {
					auto* seq = t.seq < mgr->sequenceArray.capacity() ? mgr->sequenceArray[t.seq].get() : nullptr;
					if (!seq || t.item >= seq->arraySize) {
						continue;
					}
					const auto& item = seq->interpArray[t.item];
					auto*       keys = item.interpCtlr && item.interpCtlr->target == a_props[b] ? ColourKeys(item.interpolator.get()) : nullptr;
					if (!keys || keys->GetKeyCount(0) != t.grey.size()) {
						continue;  // not the shape the model was read from
					}
					for (std::uint32_t n = 0; n < t.grey.size(); ++n) {
						KeyValue(keys, n) = *hue * TrackKey(t.grey[n], blk.full, level);
					}
				}
			}
		}

		std::vector<RE::BSEffectShaderProperty*> PropsOf(const Master& a_m)
		{
			std::vector<RE::BSEffectShaderProperty*> out;
			for (const auto& blk : a_m.blocks) {
				out.push_back(blk.prop.get());
			}
			return out;
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

		// the cached models' one paint, at the first apply (data load): nothing copies them yet. A copy that shows before its own
		// paint then wears the look the session started with, never the neutral mesh
		void FirstLook(Row& a_row, const Setting& a_s)
		{
			const auto look = LookOf(a_s);
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
				TintTracks(master.root.get(), master, PropsOf(master), look);
			}
		}

		// one copy of a cached model in a look, block for block (a copy walks its geometry in the model's order); false when it
		// is not the shape the model was read from. Its blocks get materials of their own; its colour keys may be shared with
		// the model (Gamebryo's copy, not shown), so they are float writes into the model's key data - never a release; every
		// copy of a row wears the same look, so they agree either way
		bool PaintCopy(RE::NiAVObject* a_root, const Master& a_m, const Look& a_look)
		{
			std::vector<RE::BSEffectShaderProperty*> props;
			EachEffectBlock(a_root, [&](RE::BSGeometry*, RE::BSEffectShaderProperty* a_prop, RE::BSEffectShaderMaterial*) { props.push_back(a_prop); });
			if (props.size() != a_m.blocks.size()) {
				return false;
			}
			for (std::size_t i = 0; i < props.size(); ++i) {
				const auto& blk = a_m.blocks[i];
				bool        plain = false;
				auto*       fresh = Fresh(blk, a_m.hand, a_look, plain);
				if (!fresh) {
					continue;
				}
				if (blk.vapourOne) {
					VapourBits(props[i], blk, plain);
				}
				PutMaterial(props[i], fresh);
			}
			TintTracks(a_root, a_m, props, a_look);
			return true;
		}

		const Master* MasterOf(const ArtOf& a_art)
		{
			const auto& masters = gRows[a_art.slot].masters;
			const auto  it = std::ranges::find(masters, a_art.model, &Master::model);
			return it != masters.end() ? &*it : nullptr;
		}

		// the copy's record: the look it wears now (a copy that did not match its model is noted too, so it is not tried every frame)
		void NoteCopy(RE::NiAVObject* a_root, const Setting& a_with)
		{
			if (const auto it = std::ranges::find(gCopies, a_root, &Copy::root); it != gCopies.end()) {
				it->with = a_with;
				return;
			}
			a_root->IncRefCount();
			gCopies.push_back({ a_root, a_with });
		}

		// a copy of ours that wears another look than its row's: painted and noted (gLock held). True when painted
		bool PaintStale(RE::NiAVObject* a_root, const ArtOf& a_art)
		{
			const auto& row = gRows[a_art.slot];
			if (row.live.c == 0xFFFFFFFF) {
				return false;  // never colored this session (a Vanilla row): the copy is the model as built
			}
			const auto had = std::ranges::find(gCopies, a_root, &Copy::root);
			if (had != gCopies.end() && had->with == row.live) {
				return false;
			}
			const auto* master = MasterOf(a_art);
			const bool  painted = master && PaintCopy(a_root, *master, LookOf(row.live));
			NoteCopy(a_root, row.live);
			return painted;
		}

		using ArtIt = std::unordered_map<const RE::BGSArtObject*, ArtOf>::const_iterator;

		// the spell in a caster's hand: the one it is casting, else the one equipped there (a ward attached in an earlier frame holds no
		// casting art any more and no spell is current)
		const RE::MagicItem* SpellInHand(const RE::ActorMagicCaster* a_self)
		{
			if (a_self->currentSpell) {
				return a_self->currentSpell;
			}
			const auto hand = static_cast<std::size_t>(a_self->castingSource);
			return a_self->actor && hand <= static_cast<std::size_t>(RE::MagicSystem::CastingSource::kRightHand) ? a_self->actor->GetActorRuntimeData().selectedSpells[hand] : nullptr;
		}

		// The art object of ours a magic caster's attached art came from (WardMath.h FindOurHandArt): the castingArt it holds, the one it held
		// before this update, or - a_spellRoute, her own casters only, and only when it holds none - the casting art the effects of the spell in
		// its hand carry
		ArtIt HandArtOf(const RE::ActorMagicCaster* a_self, const RE::BGSArtObject* a_before, bool a_spellRoute, bool& a_viaSpell)
		{
			std::array<const RE::BGSArtObject*, 8> spell{};
			std::size_t                            n = 0;
			const auto*                            inHand = a_spellRoute && !a_self->castingArt && !a_before ? SpellInHand(a_self) : nullptr;
			if (inHand) {
				for (const auto* e : inHand->effects) {
					if (n < spell.size()) {
						spell[n++] = e && e->baseEffect ? e->baseEffect->data.castingArt : nullptr;
					}
				}
			}
			const auto* art = FindOurHandArt<RE::BGSArtObject>(
				a_self->castingArt, a_before, std::span(spell.data(), n), a_spellRoute, [](const RE::BGSArtObject* a_art) { return gArtOf.contains(a_art); }, a_viaSpell);
			return art ? gArtOf.find(art) : gArtOf.end();
		}

		// a color drag (a main-thread task) over her own casters: the ward in the hand told by the spell in it once its casting art is gone
		ArtIt HandArtOf(const RE::ActorMagicCaster* a_self)
		{
			bool viaSpell = false;
			return HandArtOf(a_self, nullptr, true, viaSpell);
		}

		// Every copy of ours on screen (a held ward's, the menu's preview, the ward in her hands), listed under the game's lock
		// with gLock NOT held: the art hook takes gLock inside the game's update, so holding ours here while asking for the
		// game's could deadlock. gArtOf is read-only once gReady. Another caster's hands follow at his next update (the menu is
		// open only while the game stands still, and then only hers are on show); a look switched to or from Vanilla reaches
		// his hand at his next cast - the art he holds is his old look's
		std::vector<std::pair<RE::NiPointer<RE::NiAVObject>, const ArtOf*>> LiveCopies()
		{
			std::vector<std::pair<RE::NiPointer<RE::NiAVObject>, const ArtOf*>> live;
			if (auto* lists = RE::ProcessLists::GetSingleton()) {
				RE::BSSpinLockGuard guard{ lists->magicEffectsLock };
				for (auto& temp : lists->magicEffects) {
					auto* e = temp.get();
					if (!e || e->GetType() != RE::TEMP_EFFECT_TYPE::kRefModel) {
						continue;
					}
					const auto* m = static_cast<RE::ModelReferenceEffect*>(e);
					const auto  art = m->finished || !m->artObject3D ? gArtOf.end() : gArtOf.find(m->artObject);
					if (art != gArtOf.end()) {
						live.emplace_back(m->artObject3D, &art->second);
					}
				}
			}
			if (auto* player = RE::PlayerCharacter::GetSingleton()) {
				for (auto* caster : player->GetActorRuntimeData().magicCasters) {
					const auto art = caster && caster->castingArtData.attachedArt ? HandArtOf(caster) : ArtIt{ gArtOf.end() };
					if (art != gArtOf.end()) {
						live.emplace_back(caster->castingArtData.attachedArt, &art->second);
					}
				}
			}
			return live;
		}

		// an art effect's update: the copy it shows, painted the frame it appears, before it is drawn
		struct CopyHook
		{
			static bool thunk(RE::ModelReferenceEffect* a_self, float a_delta)
			{
				const bool alive = func(a_self, a_delta);
				if (gReady && a_self->artObject3D && !a_self->finished) {
					Guarded("a new ward copy", [a_self]() {
						if (const auto art = gArtOf.find(a_self->artObject); art != gArtOf.end()) {
							static std::array<std::atomic_bool, 2> seen{};
							NoteThread(seen, "colors: a ward copy updated");
							std::scoped_lock l{ gLock };
							gNewCopies += PaintStale(a_self->artObject3D.get(), art->second) ? 1 : 0;
						}
					});
				}
				return alive;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// a magic caster's update: the ward in the hand - attached by the caster itself (its casting art), never as an art
		// effect - painted the frame it appears, in its caster's look (another caster's from his look's own model and palettes)
		// the hand hook's own count: every call, and every call that found a ward copy of ours attached (the readout's handUpdates)
		std::atomic<std::uint64_t> gHandCalls{ 0 };
		std::atomic<std::uint64_t> gHandOurs{ 0 };
		// why it found none on HER casters: a caster that holds a casting art but has none attached, one tied to a ward of ours only through
		// its spell, and the first few art objects she held that were not ours - where a ward in the hand really lives is read off these
		// (the readout's handUpdates)
		constexpr std::size_t                             kMostMisses = 6;
		std::atomic<std::uint64_t>                        gHandNoRoot{ 0 };
		std::atomic<std::uint64_t>                        gHandViaSpell{ 0 };
		std::array<std::atomic<const void*>, kMostMisses> gHandMissSeen{};
		std::vector<nlohmann::json>                       gHandMisses;  // gLock

		// a_held: the casting art the caster held before the update; a miss is kept once per art (per spell when it held none)
		void NoteHandMiss(const RE::ActorMagicCaster* a_self, const RE::BGSArtObject* a_held, RE::NiNode* a_root)
		{
			const auto*               inHand = SpellInHand(a_self);
			const void*               key = a_held ? static_cast<const void*>(a_held) :
			                                inHand ? static_cast<const void*>(inHand) :
			                                         static_cast<const void*>(a_root);
			std::atomic<const void*>* mine = nullptr;
			for (auto& slot : gHandMissSeen) {
				const void* now = slot.load(std::memory_order_relaxed);
				if (now == key) {
					return;
				}
				const void* none = nullptr;
				if (!now && slot.compare_exchange_strong(none, key)) {
					mine = &slot;
					break;
				}
				if (none == key) {
					return;  // another thread took this slot for the same art
				}
			}
			if (!mine) {
				return;
			}
			try {
				const auto text = [](const char* a_s) { return std::string(a_s ? a_s : ""); };
				auto       children = nlohmann::json::array();
				for (auto& child : a_root->GetChildren()) {
					if (child && children.size() < 6) {
						children.push_back(text(child->name.c_str()));
					}
				}
				auto effects = nlohmann::json::array();
				if (inHand) {
					for (const auto* e : inHand->effects) {
						const auto* art = e && e->baseEffect ? e->baseEffect->data.castingArt : nullptr;
						effects.push_back(art ? Lower(text(art->GetModel())) : std::string("none"));
					}
				}
				std::scoped_lock l{ gLock };
				gHandMisses.push_back({ { "castingArt", a_held ? Lower(text(a_held->GetModel())) : std::string("none") }, { "attached", text(a_root->name.c_str()) },
					{ "children", std::move(children) }, { "spell", inHand ? text(inHand->GetName()) : std::string() },
					{ "effectArts", std::move(effects) } });
			} catch (...) {
				mine->store(nullptr, std::memory_order_relaxed);  // given back: a record that failed is tried again
				throw;
			}
		}

		struct HandHook
		{
			static void thunk(RE::ActorMagicCaster* a_self, float a_delta)
			{
				gHandCalls.fetch_add(1, std::memory_order_relaxed);
				// before the update: its first call attaches the casting art, which another caster takes in his look
				Guarded("another caster's ward in the hand", [a_self]() { LookHandArt(a_self); });
				const auto* before = a_self->castingArt;  // the update may clear it once the art is attached
				func(a_self, a_delta);
				auto*      root = gReady ? a_self->castingArtData.attachedArt.get() : nullptr;
				const bool player = a_self->actor && a_self->actor->IsPlayerRef();
				if (!root) {
					if (player && (before || a_self->castingArt)) {
						gHandNoRoot.fetch_add(1, std::memory_order_relaxed);
					}
					return;
				}
				bool       viaSpell = false;
				const auto art = HandArtOf(a_self, before, player, viaSpell);
				if (art == gArtOf.end() && player) {
					Guarded("a hand art that is not ours", [a_self, before, root]() { NoteHandMiss(a_self, before ? before : a_self->castingArt, root); });
				}
				if (art != gArtOf.end()) {
					gHandOurs.fetch_add(1, std::memory_order_relaxed);
					gHandViaSpell.fetch_add(viaSpell ? 1 : 0, std::memory_order_relaxed);
					Guarded("a new ward in the hand", [root, &art]() {
						static std::array<std::atomic_bool, 2> seen{};
						NoteThread(seen, "colors: a ward in the hand updated");
						// the art attached is already his look's (LookHandArt): painted from its own palettes, no look read per frame
						std::scoped_lock l{ gLock };
						gNewCopies += PaintStale(root, art->second) ? 1 : 0;
					});
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		std::atomic_bool                      gLiveQueued{ false };
		std::atomic_bool                      gLivePending{ false };  // a throttled recolor the menu still owes
		std::chrono::steady_clock::time_point gLiveLast{};
	}

	void LiveRecolor()
	{
		// the menu's render thread, once per frame while a color is dragged: at most ~15 recolors a second, one queued at a
		// time - colors only (no save, no re-equip of a ward in the hands; both happen once the mouse is let go)
		const auto now = std::chrono::steady_clock::now();
		if (now - gLiveLast < std::chrono::milliseconds(66) || gLiveQueued) {
			gLivePending = true;
			return;
		}
		gLiveLast = now;
		gLivePending = !LaterOnce(gLiveQueued, "live recolor", []() { ApplyColors(true); });
	}

	bool LivePending()
	{
		return gLivePending;
	}

	void RegisterRowArt(std::size_t a_slot, std::vector<std::pair<const RE::BGSArtObject*, std::string>> a_arts)
	{
		std::scoped_lock l{ gLock };
		if (a_slot >= kSlots) {
			return;
		}
		for (auto& [art, model] : a_arts) {
			gRows[a_slot].models.push_back(model);
			if (art) {
				gArtOf.insert_or_assign(art, ArtOf{ a_slot, std::move(model) });
			}
		}
	}

	bool ApplyColors(bool a_quiet)
	{
		const int        opacity = Opacity();
		const int        transparency = Has360Ward() ? Transparency() : 0;  // the menu greys it out otherwise
		const int        brightness = Brightness();
		const int        castingGlow = CastingGlow();
		bool             changed = false;
		std::unique_lock l{ gLock };
		Bury();
		const auto swaps = gSwaps, fails = gSwapFails, edits = gEdits;  // the log counts this call
		const bool first = !gReady;
		for (std::size_t i = 0; i < kSlots; ++i) {
			auto& row = gRows[i];
			// every row's models load at the first apply, colored or not: one loaded later would be edited while it may be copied
			if (first && !row.models.empty()) {
				LoadRow(row);
			}
			const auto c = SlotColor(i);
			if (!c || !row.loaded) {
				continue;  // a Vanilla row wears no art of ours
			}
			const Setting want{ *c, opacity, transparency, brightness, castingGlow, DomeKept(), static_cast<int>(LightingPick()) };
			if (first) {
				FirstLook(row, want);
			}
			// a color drag (~15 a second) recolors only what moved; an apply also tries again a texture that was not loaded yet
			if (!a_quiet || row.live != want) {
				PaintPalettes(row, LookOf(want));
			}
			if (row.live != want) {
				row.live = want;
				changed = true;
			}
			if (!a_quiet) {
				row.applied = want;
			}
		}
		gReady = true;
		// every copy on screen whose row's look moved, painted at once: a color drag shows on the ward while the menu is open,
		// when no art effect updates. A call that changed nothing repainted nothing
		l.unlock();
		const auto live = changed ? LiveCopies() : decltype(LiveCopies()){};
		l.lock();
		gLiveDomes = 0;
		for (const auto& [root, art] : live) {
			gLiveDomes += PaintStale(root.get(), *art) ? 1 : 0;
		}
		if (!a_quiet) {
			SKSE::log::info("colors: {} palette(s) swapped, {} failed, {} glow block(s) set, {} live dome(s) repainted, {} new cop(ies) painted since the last apply{}",
				gSwaps - swaps, gSwapFails - fails, gEdits - edits, gLiveDomes, gNewCopies - gCopiesLogged, gSwapFails > fails ? " - " + gLastProblem : "");
			gCopiesLogged = gNewCopies;
		}
		return changed;
	}

	void InstallCopyPaint()
	{
		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_ModelReferenceEffect[0] };
		CopyHook::func = vtbl.write_vfunc(0x28, CopyHook::thunk);
		REL::Relocation<std::uintptr_t> casters{ RE::VTABLE_ActorMagicCaster[0] };
		HandHook::func = casters.write_vfunc(REL::Relocate(0x1D, 0x1D, 0x1F), HandHook::thunk);
		SKSE::log::info("colors: a ward copy is painted the frame it appears (the art effect's and the magic caster's update hooks are in)");
	}

	nlohmann::json ColorsReport()
	{
		std::scoped_lock l{ gLock };
		auto             rows = nlohmann::json::object();
		for (std::size_t i = 0; i < kSlots; ++i) {
			const auto& r = gRows[i];
			std::size_t blocks = 0, off = 0, fills = 0, tracks = 0;
			for (const auto& m : r.masters) {
				blocks += m.blocks.size();
				off += m.controllersOff;
				tracks += m.tracks;
				fills += static_cast<std::size_t>(std::ranges::count_if(m.blocks, [](const Block& a_b) { return a_b.fill; }));
			}
			auto pals = nlohmann::json::array();
			for (const auto& p : r.palettes) {
				pals.push_back({ { "path", p.path }, { "stem", p.stem }, { "loaded", p.tex && p.tex->rendererTexture },
					{ "applied", p.applied == 0xFFFFFFFF ? std::string() : HexColor(p.applied) } });
			}
			rows[std::string(SlotToken(i))] = { { "models", r.models.size() }, { "masters", r.masters.size() }, { "blocks", blocks }, { "fills", fills },
				{ "colourControllersOff", off }, { "colourTracks", tracks }, { "applied", r.applied.c == 0xFFFFFFFF ? std::string() : HexColor(r.applied.c) },
				{ "palettes", std::move(pals) } };
		}
		return { { "swaps", gSwaps }, { "swapFails", gSwapFails }, { "glowEdits", gEdits }, { "liveDomes", gLiveDomes }, { "newCopies", gNewCopies },
			{ "copies", gCopies.size() }, { "missingModels", gMissing }, { "domeKept", DomeKept() },
			{ "handUpdates", { { "calls", gHandCalls.load(std::memory_order_relaxed) }, { "ours", gHandOurs.load(std::memory_order_relaxed) },
								 { "viaSpell", gHandViaSpell.load(std::memory_order_relaxed) }, { "playerNoRoot", gHandNoRoot.load(std::memory_order_relaxed) },
								 { "playerMisses", gHandMisses } } },
			{ "goldVapour", { { "blocks", gGoldVapourBlocks }, { "texture", gGoldVapour != nullptr } } }, { "lastProblem", gLastProblem }, { "rows", std::move(rows) } };
	}

	void SetDomeKeptTest(int a_percent)
	{
		gKeptTest = std::clamp(a_percent, -1, 100);
	}

	// the 2 s art watch (a main-thread task, outside the game's effect updates): the palettes a recolor replaced, and the copies
	// nothing else holds - given back here only, never from the art hook, so no copy is destroyed during the game's update pass
	void ReleaseOld()
	{
		std::scoped_lock l{ gLock };
		Bury();
		std::erase_if(gCopies, [](const Copy& a_c) {
			if (a_c.root->GetRefCount() > 1) {
				return false;
			}
			a_c.root->DecRefCount();
			return true;
		});
	}
}
