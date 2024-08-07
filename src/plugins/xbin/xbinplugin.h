/*
 * TBIN Tiled Plugin
 * Copyright 2017, Chase Warrington <spacechase0.and.cat@gmail.com>
 *
 * This file is part of Tiled.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "xbin_global.h"

#include "tilesetformat.h"

#include "mapformat.h"
#include "gidmapper.h"
#include "plugin.h"

#include "luban/Gen/gen_types.h"

#include <QObject>


namespace Tiled {
class Map;
}

namespace xbin {

using namespace cfg;

class XBINSHARED_EXPORT XBinPlugin : public Tiled::Plugin
{
    Q_OBJECT
    Q_INTERFACES(Tiled::Plugin)
    Q_PLUGIN_METADATA(IID "org.mapeditor.Plugin" FILE "plugin.json")

public:
    void initialize() override;
};


class XBINSHARED_EXPORT XBinMapFormat : public Tiled::MapFormat
{
    Q_OBJECT
    Q_INTERFACES(Tiled::MapFormat)

public:
    XBinMapFormat(QObject *parent = nullptr);

    std::unique_ptr<Tiled::Map> read(const QString &fileName) override;
    bool supportsFile(const QString &fileName) const override;

    bool write(const Tiled::Map *map, const QString &fileName, Options options) override;

    QString nameFilter() const override;
    QString shortName() const override;
    QString errorString() const override;

private:
    void writeImage(
                std::shared_ptr<bmap::Image> &item,
                const QUrl &source,
                const QPixmap &image,
                const QColor &transColor,
                const QSize size);
    void writeLayerAttributes(std::shared_ptr<bmap::Layer> &bl, const Tiled::Layer &layer);
    void writeTileLayer(std::shared_ptr<bmap::Layer> &bl, const Tiled::TileLayer &tileLayer);
    void writeTileLayerData(std::shared_ptr<bmap::Layer> &bl, const Tiled::TileLayer &tileLayer, QRect bounds);
    void writeObjectGroupForTile(bmap::Tile &bt, const Tiled::ObjectGroup &objectGroup);
    void writeObjectGroup(std::shared_ptr<bmap::Layer> &bl, const Tiled::ObjectGroup &objectGroup);
    void writeObject(bright::SharedPtr<cfg::bmap::ObjectItem> &oitem, const Tiled::MapObject &mapObject);

protected:
    QString mError;

private:
    QDir mDir;      // The directory in which the file is being saved
    Tiled::GidMapper mGidMapper;
    Tiled::Map::LayerDataFormat mLayerDataFormat;
    int mCompressionlevel;
};

// 图集
class XBINSHARED_EXPORT XBinTilesetFormat : public Tiled::TilesetFormat
{
    Q_OBJECT
    Q_INTERFACES(Tiled::TilesetFormat)

public:
    XBinTilesetFormat(QObject *parent = nullptr);

    Tiled::SharedTileset read(const QString &fileName) override;
    bool supportsFile(const QString &fileName) const override;

    bool write(const Tiled::Tileset &tileset, const QString &fileName, Options options) override;

    QString nameFilter() const override;
    QString shortName() const override;
    QString errorString() const override;

private:
    void writeImage(
                std::shared_ptr<bmap::Image> &item,
                const QUrl &source,
                const QPixmap &image,
                const QColor &transColor,
                const QSize size);

protected:
    QString mError;
    QDir mDir;
};

} // namespace Json
