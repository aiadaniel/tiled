import qbs 1.0

TiledPlugin {
    cpp.defines: base.concat(["XBIN_LIBRARY"])

    files: [
        "xbin_global.h",
        "xbinplugin.cpp",
        "xbinplugin.h",
        "plugin.json",
    ]
}
