-- Dynamic Wards - SKSE plugin. GPL-3.0-or-later, see LICENSE.txt.
set_xmakever("3.0.0")
set_project("DynamicWards")
set_version("4.0.0")
set_license("GPL-3.0-or-later")
set_arch("x64")
set_languages("c++23")
-- the DLL carries its own Visual C++ runtime, so an older runtime on a player's PC cannot stop it loading
set_runtimes("MT")
add_rules("mode.releasedbg")
set_defaultmode("releasedbg")

-- GLOW_VR=1 (set by PC Runner\pluginbuild.py for the optional VR download - his call, 2026-09-22)
-- builds for Skyrim VR only; anything else is the SE + AE build the main download ships
local vr_only = os.getenv("GLOW_VR") == "1"
set_config("skyrim_se", not vr_only)
set_config("skyrim_ae", not vr_only)
set_config("skyrim_vr", vr_only)

-- REPRODUCIBLE BUILDS: the same source gives the same DLL, byte for byte, in whatever folder it is built - no time stamp
-- (/Brepro: the compiler and the linker stamp a hash of the contents), no source folder in the code (/pathmap: this folder
-- is written as "."; MSVC takes it only with /experimental:deterministic), no build folder in the DLL (/PDBALTPATH: the .pdb
-- by its name only). Set before CommonLib is included, so its files are built the same way. MSVC only (tools = cl / link):
-- another toolchain never gets a switch it does not know. (A "cl::" prefix would make xmake drop /pathmap despite force.)
add_cxflags("/Brepro", "/experimental:deterministic", "/pathmap:" .. os.projectdir() .. "=.", { force = true, tools = "cl" })
add_shflags("/Brepro", "/PDBALTPATH:%_PDB%", { force = true, tools = "link" })

includes("lib/commonlibsse-ng")

-- DevBench replies are JSON (MIT-licensed library; the same version CommonLib pins for its own JSON option)
add_requires("nlohmann_json v3.12.0")

target("DynamicWards", function()
    add_deps("commonlibsse-ng")
    add_packages("nlohmann_json")
    add_defines("NOMINMAX")  -- windows.h min/max macros break std::min/max and numeric_limits::max
    add_rules("commonlibsse-ng.plugin", {
        name = "DynamicWards",
        author = "izzydoingit",
        description = "Dynamic Wards - any color for any ward, one set of meshes colored in memory, no plugin of its own",
    })
    add_files("src/*.cpp")
    add_headerfiles("src/*.h")
    add_includedirs("src")
    set_pcxxheader("src/PCH.h")
    -- every warning in OUR files is an error (his C++ standard, 2026-10-07: "/W4 /WX ... fix all warnings")
    set_warnings("allextra", "error")
end)

-- the game-free parts (src/WardMath.h, src/SettingsText.h), tested without the game: xmake build testwards && xmake run testwards
-- (PC Runner\plugin build.py dw builds and runs it, then again under clang's address + undefined-behavior checkers)
target("testwards", function()
    set_kind("binary")
    set_default(false)
    add_files("tests/testwards.cpp")
    add_includedirs("src")
    set_warnings("allextra", "error")
end)
