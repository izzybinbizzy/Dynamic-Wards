-- Dynamic Wards - SKSE plugin. GPL-3.0-or-later, see LICENSE.txt.
set_xmakever("3.0.0")
set_project("DynamicWards")
set_version("3.1.0")
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

includes("lib/commonlibsse-ng")

-- DevBench replies are JSON (MIT-licensed library; the same version CommonLib pins for its own JSON option)
add_requires("nlohmann_json v3.12.0")

target("DynamicWards", function()
    add_deps("commonlibsse-ng")
    add_packages("nlohmann_json")
    add_rules("commonlibsse-ng.plugin", {
        name = "DynamicWards",
        author = "izzydoingit",
        description = "Dynamic Wards - any color for any ward, one set of meshes colored in memory, no plugin of its own",
    })
    add_files("src/*.cpp")
    add_headerfiles("src/*.h")
    add_includedirs("src")
    set_pcxxheader("src/PCH.h")
    set_warnings("allextra")
end)
