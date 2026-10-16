local version = "0.1.0"

set_project("steamframe_keyboard_temp_fix")
set_version(version)

set_arch("x64")
set_languages("c++23")
set_warnings("allextra")

add_rules("mode.debug", "mode.releasedbg")
set_defaultmode("releasedbg")

add_rules("plugin.compile_commands.autoupdate")

set_config("skyrim_se", false)
set_config("skyrim_ae", false)
set_config("skyrim_vr", true)
includes("CommonLibSSE-NG")

target("steamframe_keyboard_temp_fix")
    set_kind("shared")
    add_rules("commonlibsse-ng.plugin", {
        name = "steamframe_keyboard_temp_fix",
        author = "nadako",
        description = "Hacky temporary fix to make VR keyboard input somewhat work with Steam Frame streaming"
    })
    set_pcxxheader("src/pch.h")
    add_files("src/**.cpp")

    after_build(function(target)
        local target_dirs = {
            "C:/Games/MySkyrimVR/modlist/mods/VRKeyboardFix",
            "C:/Games/SkyrimVRForMod/mods/VRKeyboardFix",
        }
        for _, target_dir in ipairs(target_dirs) do
            local target_plugin_dir = path.join(target_dir, "SKSE", "Plugins")
            os.mkdir(target_plugin_dir)
            os.cp(target:targetfile(), target_plugin_dir)
            os.cp(target:symbolfile(), target_plugin_dir)
            print("Deployed info " .. target_dir)
        end
    end)
target_end()
