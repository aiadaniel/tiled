import qbs 1.0

TiledPlugin {
    cpp.defines: base.concat(["XBIN_LIBRARY"])

    files: [
        "xbin_global.h",
        "xbinplugin.cpp",
        "xbinplugin.h",
        "plugin.json",
        "xbin/Layer.hpp",
        "xbin/Map.cpp",
        "xbin/Map.hpp",
        "xbin/PropertyValue.hpp",
        "xbin/Tile.hpp",
        "xbin/TileSheet.hpp",
        "xbin/FakeSfml.hpp",
    ]
}
