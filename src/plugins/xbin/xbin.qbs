import qbs 1.0

TiledPlugin {
    cpp.defines: base.concat(["XBIN_LIBRARY"])

    cpp.includePaths: ["./"]

    files: [
        "bright/serialization/ByteBuf.cpp",
        "bright/serialization/ByteBuf.h",
        "bright/CfgBean.hpp",
        "bright/CommonMacros.h",
        "luban/Gen/gen_stub_0.cpp",
        "luban/Gen/gen_types.h",
        "xbin_global.h",
        "xbinplugin.cpp",
        "xbinplugin.h",
        "plugin.json",
    ]
}
