include(../plugin.pri)

DEFINES += XBIN_LIBRARY

SOURCES += xbinplugin.cpp \
    xbin/Map.cpp

HEADERS += xbin_global.h \
    xbinplugin.h \
    xbin/Layer.hpp \
    xbin/Map.hpp \
    xbin/PropertyValue.hpp \
    xbin/Tile.hpp \
    xbin/TileSheet.hpp \
    xbin/FakeSfml.hpp
