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

#include "xbinplugin.h"

#include "xbin/Map.hpp"

#include "logginginterface.h"
#include "map.h"
#include "mapobject.h"
#include "objectgroup.h"
#include "savefile.h"
#include "tile.h"
#include "tiled.h"
#include "tilelayer.h"

#include <QCoreApplication>
#include <QDir>

#include <cmath>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>

namespace
{
    void xbinToTiledProperties(const xbin::Properties &props, Tiled::Object &obj)
    {
        for (const auto &prop : props) {
            if (prop.first[0] == '@')
                continue;
            switch (prop.second.type) {
                case xbin::PropertyValue::String:
                    obj.setProperty(QString::fromStdString(prop.first), QString::fromStdString(prop.second.dataStr));
                    break;

                case xbin::PropertyValue::Bool:
                    obj.setProperty(QString::fromStdString(prop.first), prop.second.data.b);
                    break;

                case xbin::PropertyValue::Float:
                    obj.setProperty(QString::fromStdString(prop.first), prop.second.data.f);
                    break;

                case xbin::PropertyValue::Integer:
                    obj.setProperty(QString::fromStdString(prop.first), prop.second.data.i);
                    break;
            }
        }
    }

    void tiledToXbinProperties(const Tiled::Properties &properties, xbin::Properties &tprops)
    {
        for (auto it = properties.cbegin(), it_end = properties.cend(); it != it_end; ++it) {
            xbin::PropertyValue prop;

            switch (it.value().userType()) {
            case QVariant::Bool:
                prop.type = xbin::PropertyValue::Bool;
                prop.data.b = it.value().toBool();
                break;
            case QVariant::Double:
            case QMetaType::Float:
                prop.type = xbin::PropertyValue::Float;
                prop.data.f = it.value().toFloat();
                break;
            case QVariant::Int:
                prop.type = xbin::PropertyValue::Integer;
                prop.data.i = it.value().toInt();
                break;
            case QVariant::String:
                prop.type = xbin::PropertyValue::String;
                prop.dataStr = it.value().toString().toStdString();
                break;
            default:
                throw std::invalid_argument(QT_TRANSLATE_NOOP("BinMapFormat", "Unsupported property type"));
            }

            tprops.insert(std::make_pair(it.key().toStdString(), prop));
        }
    }
}

namespace xbin {

void XBinPlugin::initialize()
{
    addObject(new XBinMapFormat(this));
}


XBinMapFormat::XBinMapFormat(QObject *)
{
}

std::unique_ptr<Tiled::Map> XBinMapFormat::read(const QString &fileName)
{
    std::ifstream file( fileName.toStdString(), std::ios::in | std::ios::binary );
    if (!file) {
        mError = QCoreApplication::translate("File Errors", "Could not open file for reading.");
        return nullptr;
    }

    xbin::Map tmap;
    std::unique_ptr<Tiled::Map> map;
    try
    {
        tmap.loadFromStream(file);

        if (tmap.layers.empty())
            throw std::invalid_argument(QT_TR_NOOP("Map contains no layers."));

        auto &firstLayer = tmap.layers[0];

        map = std::make_unique<Tiled::Map>(Tiled::Map::Orthogonal,
                                           QSize(firstLayer.layerSize.x, firstLayer.layerSize.y),
                                           QSize(firstLayer.tileSize.x, firstLayer.tileSize.y));

        xbinToTiledProperties(tmap.props, *map);

        const QDir fileDir(QFileInfo(fileName).dir());

        std::map< std::string, int > tmapTilesheetMapping;
        for (std::size_t i = 0; i < tmap.tilesheets.size(); ++i) {
            const xbin::TileSheet& ttilesheet = tmap.tilesheets[i];
            tmapTilesheetMapping[ttilesheet.id] = static_cast<int>(i);

            if (ttilesheet.spacing.x != ttilesheet.spacing.y)
                throw std::invalid_argument(QT_TR_NOOP("Tilesheet must have equal spacings."));
            if (ttilesheet.margin.x != ttilesheet.margin.y)
                throw std::invalid_argument(QT_TR_NOOP("Tilesheet must have equal margins."));

            auto tileset = Tiled::Tileset::create(ttilesheet.id.c_str(), ttilesheet.tileSize.x, ttilesheet.tileSize.y, ttilesheet.spacing.x, ttilesheet.margin.x);
            tileset->setImageSource(Tiled::toUrl(QString::fromStdString(ttilesheet.image).replace("\\", "/"), fileDir));
            tileset->loadImage();

            xbinToTiledProperties(ttilesheet.props, *tileset);

            for (const auto &prop : ttilesheet.props) {
                if (prop.first[0] != '@')
                    continue;

                const QString name = QString::fromStdString(prop.first);
                const QVector<QStringRef> strs = name.splitRef('@');
                if (strs[1] == QLatin1String("TileIndex")) {
                    int index = strs[2].toInt();
                    xbin::Properties dummyProps;
                    dummyProps.insert(std::make_pair(strs[3].toUtf8().constData(), prop.second));
                    Tiled::Tile *tile = tileset->findOrCreateTile(index);
                    xbinToTiledProperties(dummyProps, *tile);
                }
                // TODO: 'AutoTile' ?
                // Purely for map making. Appears to be similar to terrains
                // (In tIDE, right click a tilesheet and choose "Auto Tiles..."
            }

            map->addTileset(tileset);
        }
        for (const xbin::Layer& tlayer : tmap.layers) {
            if (tlayer.tileSize.x != firstLayer.tileSize.x || tlayer.tileSize.y != firstLayer.tileSize.y)
                throw std::invalid_argument(QT_TR_NOOP("Different tile sizes per layer are not supported."));

            auto layer = std::make_unique<Tiled::TileLayer>(QString::fromStdString(tlayer.id), 0, 0, tlayer.layerSize.x, tlayer.layerSize.y);
            xbinToTiledProperties(tlayer.props, *layer);
            auto objects = std::make_unique<Tiled::ObjectGroup>(QString::fromStdString(tlayer.id), 0, 0);
            for (std::size_t i = 0; i < tlayer.tiles.size(); ++i) {
                const xbin::Tile& ttile = tlayer.tiles[i];
                int ix = static_cast<int>(i % static_cast<std::size_t>(tlayer.layerSize.x));
                int iy = static_cast<int>(i / static_cast<std::size_t>(tlayer.layerSize.x));

                if (ttile.isNullTile())
                    continue;

                Tiled::Cell cell;
                if (ttile.animatedData.frames.size() > 0) {
                    xbin::Tile tfirstTile = ttile.animatedData.frames[0];
                    Tiled::Tile* firstTile = map->tilesetAt(tmapTilesheetMapping[tfirstTile.tilesheet])->findOrCreateTile(tfirstTile.staticData.tileIndex);
                    QVector<Tiled::Frame> frames;
                    for (const xbin::Tile& tframe : ttile.animatedData.frames) {
                        if (tframe.isNullTile() || tframe.animatedData.frames.size() > 0 ||
                             tframe.tilesheet != tfirstTile.tilesheet)
                            throw std::invalid_argument(QT_TR_NOOP("Invalid animation frame."));

                        Tiled::Frame frame;
                        frame.tileId = tframe.staticData.tileIndex;
                        frame.duration = ttile.animatedData.frameInterval;
                        frames.append(frame);
                    }
                    firstTile->setFrames(frames);
                    cell = Tiled::Cell(firstTile);
                }
                else {
                    cell = Tiled::Cell(map->tilesetAt(tmapTilesheetMapping[ttile.tilesheet]).data(), ttile.staticData.tileIndex);
                }
                layer->setCell(ix, iy, cell);

                if (ttile.props.size() > 0) {
                    auto obj = std::make_unique<Tiled::MapObject>("TileData", QString(), QPointF(ix * tlayer.tileSize.x, iy * tlayer.tileSize.y), QSizeF(tlayer.tileSize.x, tlayer.tileSize.y));
                    xbinToTiledProperties(ttile.props, *obj);
                    objects->addObject(std::move(obj));
                }
            }
            map->addLayer(std::move(layer));
            map->addLayer(std::move(objects));
        }
    }
    catch (std::exception& e) {
        mError = tr((std::string("Exception: ") + e.what()).c_str());
    }

    return map;
}

static bool hasFlags(const Tiled::Cell &cell)
{
    return cell.flippedHorizontally() ||
            cell.flippedVertically() ||
            cell.flippedAntiDiagonally() ||
            cell.rotatedHexagonal120();
}

bool XBinMapFormat::write(const Tiled::Map *map, const QString &fileName, Options options)
{
    Q_UNUSED(options)

    try {
        xbin::Map tmap;
        //tmap.id = map->name();
        tiledToXbinProperties(map->properties(), tmap.props);

        const QDir fileDir(QFileInfo(fileName).dir());

        for (const Tiled::SharedTileset& tilesheet : map->tilesets()) {
            xbin::TileSheet ttilesheet;
            ttilesheet.id = tilesheet->name().toStdString();
            ttilesheet.image = Tiled::toFileReference(tilesheet->imageSource(), fileDir).replace("/", "\\").toStdString();
            ttilesheet.margin.x = ttilesheet.margin.y = tilesheet->margin();
            ttilesheet.spacing.x = ttilesheet.spacing.y = tilesheet->tileSpacing();
            ttilesheet.sheetSize.x = tilesheet->columnCount();
            ttilesheet.sheetSize.y = tilesheet->rowCount();
            ttilesheet.tileSize.x = tilesheet->tileSize().width();
            ttilesheet.tileSize.y = tilesheet->tileSize().height();
            tiledToXbinProperties(tilesheet->properties(), ttilesheet.props);

            Tiled::Properties tilesetTileProperties;
            for (auto tile : tilesheet->tiles()) {
                const auto &props = tile->properties();
                for (auto it = props.begin(), it_end = props.end(); it != it_end; ++it) {
                    tilesetTileProperties.insert("@TileIndex@" + QString::number(tile->id()) + "@" + it.key(), it.value());
                }
            }
            tiledToXbinProperties(tilesetTileProperties, ttilesheet.props);

            tmap.tilesheets.push_back(std::move(ttilesheet));
        }

        std::vector< Tiled::ObjectGroup* > objGroups;
        std::map< std::string, xbin::Layer* > tileLayerIdMap;
        tmap.layers.reserve(static_cast<std::size_t>(map->layers().size()));
        for (Tiled::Layer* rawLayer : map->layers()) {
            if (Tiled::ObjectGroup* layer = rawLayer->asObjectGroup()) {
                objGroups.push_back(layer);
            }
            else if (Tiled::TileLayer* layer = rawLayer->asTileLayer()) {
                xbin::Layer tlayer;
                tlayer.id = layer->name().toStdString();
                tlayer.layerSize.x = layer->width();
                tlayer.layerSize.y = layer->height();
                tlayer.tileSize.x = map->tileWidth();
                tlayer.tileSize.y = map->tileHeight();
                //tlayer.visible = ???;
                for (int iy = 0; iy < tlayer.layerSize.y; ++iy) {
                    for (int ix = 0; ix < tlayer.layerSize.x; ++ix) {
                        Tiled::Cell cell = layer->cellAt(ix, iy);
                        xbin::Tile ttile;
                        ttile.staticData.tileIndex = -1;

                        if (hasFlags(cell)) {
                            Tiled::ERROR("tBIN: Flipped and/or rotated tiles are not supported.",
                                         Tiled::JumpToTile { map, QPoint(ix + layer->x(), iy + layer->y()), layer });
                        }

                        if (Tiled::Tile *tile = cell.tile()) {
                            ttile.tilesheet = tile->tileset()->name().toStdString();
                            if (tile->frames().size() == 0) {
                                ttile.staticData.tileIndex = tile->id();
                                ttile.staticData.blendMode = 0;
                            }
                            else {
                                ttile.animatedData.frameInterval = tile->frames().at(0).duration;

                                for (Tiled::Frame frame : tile->frames()) {
                                    if (frame.duration != ttile.animatedData.frameInterval) {
                                        Tiled::ERROR("tBIN: Frames with different duration are not supported.",
                                                     Tiled::SelectTile { tile });
                                    }

                                    xbin::Tile tframe;
                                    tframe.tilesheet = ttile.tilesheet;
                                    tframe.staticData.tileIndex = frame.tileId;
                                    tframe.staticData.blendMode = 0;
                                    ttile.animatedData.frames.push_back(tframe);
                                }
                            }
                        }
                        tlayer.tiles.push_back(ttile);
                    }
                }
                tiledToXbinProperties(layer->properties(), tlayer.props);
                tmap.layers.push_back(std::move(tlayer));
                tileLayerIdMap[tmap.layers.back().id] = &tmap.layers.back();
            }
            else {
                throw std::invalid_argument(QT_TR_NOOP("Only object and tile layers supported."));
            }
        }

        for (Tiled::ObjectGroup* objs : objGroups) {
            const auto groupName = objs->name();

            xbin::Layer* tiles = tileLayerIdMap[groupName.toStdString()];
            if (!tiles) {
                Tiled::WARNING(QStringLiteral("tBIN: Ignoring object layer \"%1\" without matching tile layer.").arg(groupName),
                               Tiled::SelectLayer { objs });
                continue;
            }

            for (Tiled::MapObject* obj : objs->objects()) {
                if (obj->name() != QLatin1String("TileData")) {
                    Tiled::WARNING(QStringLiteral("tBIN: Ignoring object %1 with name different from 'TileData'.").arg(obj->id()),
                                   Tiled::JumpToObject { obj });
                    continue;
                }

                if (obj->properties().isEmpty()) {
                    Tiled::WARNING(QStringLiteral("tBIN: Ignoring object %1 without custom properties.").arg(obj->id()),
                                   Tiled::JumpToObject { obj });
                    continue;
                }

                if (static_cast<int>(obj->width()) != tiles->tileSize.x ||
                        static_cast<int>(obj->height()) != tiles->tileSize.y ||
                        obj->x() / tiles->tileSize.x != std::floor(obj->x() / tiles->tileSize.x) ||
                        obj->y() / tiles->tileSize.y != std::floor(obj->y() / tiles->tileSize.y)) {
                    Tiled::WARNING(QStringLiteral("tBIN: Object %1 is not aligned to the tile grid.").arg(obj->id()),
                                   Tiled::JumpToObject { obj });
                }

                // Determine tile position based on the center of the object
                int tileX = static_cast<int>(std::floor((obj->x() + (obj->width() / 2)) / tiles->tileSize.x));
                int tileY = static_cast<int>(std::floor((obj->y() + (obj->height() / 2)) / tiles->tileSize.y));

                // Make sure the object is within the map boundaries (also makes sure values are positive)
                tileX = qBound(0, tileX, tiles->layerSize.x - 1);
                tileY = qBound(0, tileY, tiles->layerSize.y - 1);

                std::size_t idx = static_cast<std::size_t>(tileX + tileY * tiles->layerSize.x);
                tiledToXbinProperties(obj->properties(), tiles->tiles[idx].props);
            }
        }

        std::ofstream file(fileName.toStdString(), std::ios::trunc | std::ios::binary);
        if (!file) {
            mError = tr("Could not open file for writing");
            return false;
        }
        tmap.saveToStream(file);
        file.close();
    }
    catch (std::exception& e)
    {
        mError = tr("Exception: %1").arg(tr(e.what()));
        return false;
    }

    return true;
}

QString XBinMapFormat::nameFilter() const
{
    return tr("Tbin map files (*.tbin)");
}

QString XBinMapFormat::shortName() const
{
    return QStringLiteral("tbin");
}

bool XBinMapFormat::supportsFile(const QString &fileName) const
{
    std::ifstream file(fileName.toStdString(), std::ios::in | std::ios::binary);
    if (!file)
        return false;

    std::string magic(6, '\0');
    file.read(&magic[0], static_cast<std::streamsize>(magic.length()));

    return magic == "tBIN10";
}

QString XBinMapFormat::errorString() const
{
    return mError;
}

} // namespace Tbin
