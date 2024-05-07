#include "xbinplugin.h"

#include "bright/CommonMacros.h"
#include "luban/Gen/gen_types.h"

#include "logginginterface.h"
#include "map.h"
#include "mapobject.h"
#include "objectgroup.h"
#include "savefile.h"
#include "tile.h"
#include "tiled.h"
#include "tilelayer.h"
#include "fileformat.h"

#include <QCoreApplication>
#include <QDir>
#include <QtCore>

#include <cmath>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>

#include "mapwriter.h"

namespace
{
    // void xbinToTiledProperties(const xbin::Properties &props, Tiled::Object &obj)
    // {
    //     for (const auto &prop : props) {
    //         if (prop.first[0] == '@')
    //             continue;
    //         switch (prop.second.type) {
    //             case xbin::PropertyValue::String:
    //                 obj.setProperty(QString::fromStdString(prop.first), QString::fromStdString(prop.second.dataStr));
    //                 break;

    //             case xbin::PropertyValue::Bool:
    //                 obj.setProperty(QString::fromStdString(prop.first), prop.second.data.b);
    //                 break;

    //             case xbin::PropertyValue::Float:
    //                 obj.setProperty(QString::fromStdString(prop.first), prop.second.data.f);
    //                 break;

    //             case xbin::PropertyValue::Integer:
    //                 obj.setProperty(QString::fromStdString(prop.first), prop.second.data.i);
    //                 break;
    //         }
    //     }
    // }

    // void tiledToXbinProperties(const Tiled::Properties &properties, xbin::Properties &tprops)
    // {
    //     for (auto it = properties.cbegin(), it_end = properties.cend(); it != it_end; ++it) {
    //         xbin::PropertyValue prop;

    //         switch (it.value().userType()) {
    //         case QVariant::Bool:
    //             prop.type = xbin::PropertyValue::Bool;
    //             prop.data.b = it.value().toBool();
    //             break;
    //         case QVariant::Double:
    //         case QMetaType::Float:
    //             prop.type = xbin::PropertyValue::Float;
    //             prop.data.f = it.value().toFloat();
    //             break;
    //         case QVariant::Int:
    //             prop.type = xbin::PropertyValue::Integer;
    //             prop.data.i = it.value().toInt();
    //             break;
    //         case QVariant::String:
    //             prop.type = xbin::PropertyValue::String;
    //             prop.dataStr = it.value().toString().toStdString();
    //             break;
    //         default:
    //             throw std::invalid_argument(QT_TRANSLATE_NOOP("BinMapFormat", "Unsupported property type"));
    //         }

    //         tprops.insert(std::make_pair(it.key().toStdString(), prop));
    //     }
    // }
}

namespace xbin
{

    using namespace bright;
    using namespace cfg;

    void XBinPlugin::initialize()
    {
        addObject(new XBinMapFormat(this));
    }

    XBinMapFormat::XBinMapFormat(QObject *)
    {
    }

    std::unique_ptr<Tiled::Map> XBinMapFormat::read(const QString &fileName)
    {
        std::ifstream file(fileName.toStdString(), std::ios::in | std::ios::binary);
        if (!file)
        {
            mError = QCoreApplication::translate("File Errors", "Could not open file for reading.");
            return nullptr;
        }

        std::unique_ptr<Tiled::Map> map;

        return map;
    }

    static bool hasFlags(const Tiled::Cell &cell)
    {
        return cell.flippedHorizontally() ||
               cell.flippedVertically() ||
               cell.flippedAntiDiagonally() ||
               cell.rotatedHexagonal120();
    }
    static bool includeTile(const Tiled::Tile *tile)
    {
        if (!tile->className().isEmpty())
            return true;
        if (!tile->properties().isEmpty())
            return true;
        if (!tile->imageSource().isEmpty())
            return true;
        if (tile->objectGroup())
            return true;
        if (tile->isAnimated())
            return true;
        if (tile->probability() != 1.0)
            return true;

        return false;
    }
    bool XBinMapFormat::write(const Tiled::Map *map, const QString &fileName, Options options)
    {
        Q_UNUSED(options)

        mLayerDataFormat = map->layerDataFormat();
        mCompressionlevel = map->compressionLevel();

        try
        {
            bmap::BMap _map;

            // _map.version = FileFormat::versionString().toStdString();
            // _map.tiledversion = QCoreApplication::applicationVersion().toStdString();

            _map.orientation = (bmap::Orientation)map->orientation();
            _map.renderorder = (bmap::RenderOrder)map->renderOrder();

            _map.width = map->width();
            _map.height = map->height();
            _map.tilewidth = map->tileWidth();
            _map.tileheight = map->tileHeight();

            _map.infinite = map->infinite();

            if (map->orientation() == Tiled::Map::Hexagonal)
            {
                _map.hexsidelength = map->hexSideLength();
            } else {
                _map.hexsidelength = 0;
            }
            if (map->orientation() == Tiled::Map::Staggered || map->orientation() == Tiled::Map::Hexagonal)
            {
                _map.staggeraxis = (bmap::StaggerAxis)(map->staggerAxis());
                _map.staggerindex = (bmap::StaggerIndex)(map->staggerIndex());
            } else {
                _map.staggeraxis = bmap::StaggerAxis::StaggerX;
                _map.staggerindex = bmap::StaggerIndex::StaggerOdd;
            }
            _map.nextlayerid = map->nextLayerId();
            _map.nextobjectid = map->nextObjectId();

            // properties...

            // const QDir fileDir(QFileInfo(fileName).dir());
            mDir = QFileInfo(fileName).dir();

            // 1.tileset
            mGidMapper.clear();
            unsigned firstGid = 1;
            for (const Tiled::SharedTileset &ts : map->tilesets())
            {
                std::shared_ptr<bmap::TileSet> item(new bmap::TileSet());

                // 共享指针数据都需要创建保证写时不崩溃
                item->tileoffset = std::make_shared<bmap::TileOffset>();
                item->grid = std::make_shared<bmap::Grid>();
                item->image = std::make_shared<bmap::Image>();

                if (firstGid > 0)
                {
                    item->firstgid = firstGid;
                    const QString &fileName = ts->fileName();
                    if (!fileName.isEmpty())
                    {
                        QString source = fileName;//todo 相对路径
                        item->source = source.toStdString();

                        // _map.tileset.push_back(item);

                        // mGidMapper.insert(firstGid, ts);

                        // firstGid += ts->nextTileId();
                        // continue;
                    } else {
                        item->source = "";
                    }
                }
                else
                {
                    // version & tiledversion
                }
                item->name = ts->name().toStdString();
                item->tilewidth = ts->tileWidth();
                item->tileheight = ts->tileHeight();

                const int tileSpacing = ts->tileSpacing();
                const int margin = ts->margin();
                // if (tileSpacing != 0)
                {
                    item->spacing = tileSpacing;
                }
                // if (margin != 0)
                {
                    item->margin = margin;
                }
                item->tilecount = ts->tileCount();
                item->columns = ts->columnCount();

                //...

                // editor setting...

                const QPoint offset = ts->tileOffset();
                if (!offset.isNull())
                {
                    item->tileoffset->x = offset.x();
                    item->tileoffset->y = offset.y();
                } else {
                    item->tileoffset->x = 0;
                    item->tileoffset->y = 0;
                }

                // orthogonal & gridsize...

                if (ts->orientation() != Tiled::Tileset::Orthogonal || ts->gridSize() != ts->tileSize())
                {

                    item->grid->orientation = Tiled::Tileset::orientationToString(ts->orientation()).toStdString();
                    item->grid->width = ts->gridSize().width();
                    item->grid->height = ts->gridSize().height();
                }

                // transformation...

                // properties...

                // image
                writeImage(ts, item, ts->imageSource(), ts->image(), ts->transparentColor(), QSize(ts->imageWidth(), ts->imageHeight()));

                // all tiles
                const bool isCollection = ts->isCollection();
                const bool includeAllTiles = isCollection || ts->anyTileOutOfOrder();
                for (const Tiled::Tile *tile : ts->tiles())
                {
                    // if (includeAllTiles || includeTile(tile))
                    {
                        bright::SharedPtr<bmap::Tile> bt(new bmap::Tile());
                        bt->id = tile->id();
                        // ...

                        bt->image = std::make_shared<bmap::Image>();
                        // if (isCollection)
                        {
                            writeImage(ts, item, ts->imageSource(), ts->image(), QColor(), tile->size());
                        }
                        // if (tile->objectGroup())
                        //     writeObjectGroupForTile(*bt, *(tile->objectGroup()));

                        // if (tile->isAnimated())
                        {
                            const QVector<Tiled::Frame> &frames = tile->frames();
                            for (const Tiled::Frame &frame : frames)
                            {
                                std::shared_ptr<bmap::Animation> ani(new bmap::Animation());
                                ani->tileid = frame.tileId;
                                ani->duration = frame.duration;
                                bt->animation.push_back(ani);
                            }
                        }
                        item->tiles.push_back(bt);
                    }
                }

                _map.tileset.push_back(item);
                mGidMapper.insert(firstGid, ts);
                firstGid += ts->nextTileId();
            }

            // 2. tilelayer
            // 3. object group layer
            // 4. imagelayer
            // 5. group layer
            for (const Tiled::Layer *layer : map->layers())
            {
                std::shared_ptr<bmap::Layer> bl(new bmap::Layer());
                switch (layer->layerType())
                {
                case Tiled::Layer::TileLayerType:
                    writeTileLayer(bl, *static_cast<const Tiled::TileLayer *>(layer));
                    break;
                case Tiled::Layer::ObjectGroupType:
                    writeObjectGroup(bl, *static_cast<const Tiled::ObjectGroup *>(layer));
                    break;
                case Tiled::Layer::ImageLayerType:
                    // writeImageLayer(w, *static_cast<const Tiled::ImageLayer*>(layer));
                    break;
                case Tiled::Layer::GroupLayerType:
                    // writeGroupLayer(w, *static_cast<const Tiled::GroupLayer*>(layer));
                    break;
                default:
                    break;
                }
                _map.layer.push_back(bl);
            }

            std::ofstream file(fileName.toStdString(), std::ios::trunc | std::ios::binary);
            if (!file)
            {
                mError = tr("Could not open file for writing");
                return false;
            }
            // tmap.saveToStream(file);
            // using bytebuf serialize 每次导表改变以下字段需要对应修改。。。
            ByteBuf bb(16 * 1024);
            // bb.writeString(_map.version);
            // bb.writeString(_map.tiledversion);
            bb.writeInt((int32_t)_map.orientation);
            bb.writeInt((int32_t)_map.renderorder);
            bb.writeInt(_map.width);
            bb.writeInt(_map.height);
            bb.writeInt(_map.tilewidth);
            bb.writeInt(_map.tileheight);
            bb.writeInt(_map.infinite);
            bb.writeInt(_map.hexsidelength);
            bb.writeInt((int32_t)_map.staggeraxis);
            bb.writeInt((int32_t)_map.staggerindex);
            bb.writeInt(_map.nextlayerid);
            bb.writeInt(_map.nextobjectid);
            // properties

            // ::bright::Vector<::bright::SharedPtr<bmap::TileSet>> tileset;
            bb.writeInt(_map.tileset.size());
            for (const bright::SharedPtr<bmap::TileSet> &ts : _map.tileset)
            {
                /**
                 *      ::bright::int32 firstgid;
                        ::bright::String source;
                        ::bright::String name;
                        ::bright::int32 tilewidth;
                        ::bright::int32 tileheight;
                        ::bright::int32 spacing;
                        ::bright::int32 margin;
                        ::bright::int32 tilecount;
                        ::bright::int32 columns;
                        ::bright::String backgroundcolor;
                        ::bright::String objectalignment;
                        ::bright::String tilerendersize;
                        ::bright::String fillmode;
                        ::bright::SharedPtr<bmap::TileOffset> tileoffset;
                        ::bright::SharedPtr<bmap::Grid> grid;
                        ::bright::HashMap<::bright::String, ::bright::SharedPtr<bmap::Property>> properties;
                        ::bright::SharedPtr<bmap::Image> image;
                        ::bright::Vector<::bright::SharedPtr<bmap::Tile>> tiles;
                */
                bb.writeInt(ts->firstgid);
                bb.writeString(ts->source);
                bb.writeString(ts->name);
                bb.writeInt(ts->tilewidth);
                bb.writeInt(ts->tileheight);
                bb.writeInt(ts->spacing);
                bb.writeInt(ts->margin);
                bb.writeInt(ts->tilecount);
                bb.writeInt(ts->columns);
                bb.writeString(ts->backgroundcolor);
                bb.writeString(ts->objectalignment);
                bb.writeString(ts->tilerendersize);
                bb.writeString(ts->fillmode);
                // tileoffset
                bb.writeInt(ts->tileoffset->x);
                bb.writeInt(ts->tileoffset->y);
                // grid
                bb.writeString(ts->grid->orientation);
                bb.writeInt(ts->grid->width);
                bb.writeInt(ts->grid->height);
                // properties

                // image
                bb.writeString(ts->image->source);
                bb.writeInt(ts->image->width);
                bb.writeInt(ts->image->height);
                // bb.writeBytes(&ts->image->fdata->bdata);

                // tiles
                bb.writeInt(ts->tiles.size());
                for (const bright::SharedPtr<bmap::Tile> &tile : ts->tiles)
                {
                    /**
                     *  ::bright::int32 id;
                        ::bright::SharedPtr<bmap::Image> image;
                        ::bright::Vector<::bright::SharedPtr<bmap::ObjectItem>> objs;
                        ::bright::Vector<::bright::SharedPtr<bmap::Animation>> animation;
                        ::bright::HashMap<::bright::String, ::bright::SharedPtr<bmap::Property>> properties;
                    */
                    bb.writeInt(tile->id);

                    // image
                    bb.writeString(tile->image->source);
                    bb.writeInt(tile->image->width);
                    bb.writeInt(tile->image->height);
                    // bb.writeBytes(&tile->image->fdata->bdata);

                    // objs
                    bb.writeInt(tile->objs.size());
                    for (const bright::SharedPtr<bmap::ObjectItem> &obj : tile->objs)
                    {
                        /**
                         *      ::bright::int32 id;
                                ::bright::int32 gid;
                                ::bright::int32 x;
                                ::bright::int32 y;
                                ::bright::int32 width;
                                ::bright::int32 height;
                                ::bright::Vector<::bright::SharedPtr<bmap::Point>> polygon;
                                ::bright::Vector<::bright::SharedPtr<bmap::Point>> polyline;
                        */
                        bb.writeInt(obj->id);
                        bb.writeInt(obj->gid);
                        bb.writeInt(obj->x);
                        bb.writeInt(obj->y);
                        bb.writeInt(obj->width);
                        bb.writeInt(obj->height);

                        bb.writeInt(obj->polygon.size());
                        for (const bright::SharedPtr<bmap::Point> &p : obj->polygon)
                        {
                            bb.writeInt(p->x);
                            bb.writeInt(p->y);
                        }
                        bb.writeInt(obj->polyline.size());
                        for (const bright::SharedPtr<bmap::Point> &p : obj->polyline)
                        {
                            bb.writeInt(p->x);
                            bb.writeInt(p->y);
                        }
                    }

                    // animation
                    bb.writeInt(tile->animation.size());
                    for (const bright::SharedPtr<bmap::Animation> &ani : tile->animation)
                    {
                        bb.writeInt(ani->tileid);
                        bb.writeInt(ani->duration);
                    }

                    // properties
                }
            }

            // ::bright::Vector<::bright::SharedPtr<bmap::Layer>> layer;
            bb.writeInt(_map.layer.size());
            for (const bright::SharedPtr<bmap::Layer> &layer : _map.layer)
            {
                /*
                    ::bright::int32 type;
                    ::bright::int32 id;
                    ::bright::String name;
                    ::bright::int32 x;
                    ::bright::int32 y;
                    ::bright::int32 width;
                    ::bright::int32 height;
                    ::bright::int32 visible;
                    ::bright::int32 locked;
                    ::bright::int32 opacity;
                    ::bright::String tintcolor;
                    ::bright::int32 offsetx;
                    ::bright::int32 offsety;
                    ::bright::int32 repeatx;
                    ::bright::int32 repeaty;
                    ::bright::SharedPtr<bmap::Image> image;
                    ::bright::HashMap<::bright::String, ::bright::SharedPtr<bmap::Property>> properties;
                    ::bright::SharedPtr<bmap::LayerData> data;
                    ::bright::Vector<::bright::SharedPtr<bmap::ObjectItem>> objs;
                    ::bright::Vector<::bright::SharedPtr<bmap::Layer>> layers;
                */
                bb.writeInt(layer->type);
                bb.writeInt(layer->id);
                bb.writeString(layer->name);
                bb.writeInt(layer->x);
                bb.writeInt(layer->y);
                bb.writeInt(layer->width);
                bb.writeInt(layer->height);
                bb.writeInt(layer->visible);
                bb.writeInt(layer->locked);
                bb.writeInt(layer->opacity);
                bb.writeString(layer->tintcolor);
                bb.writeInt(layer->offsetx);
                bb.writeInt(layer->offsety);
                // when image layer
                // bb.writeInt(layer->repeatx);
                // bb.writeInt(layer->repeaty);
                //if (layer->type == Tiled::TileLayer::ImageLayerType) {
                    bb.writeString(layer->image->source);
                    bb.writeInt(layer->image->width);
                    bb.writeInt(layer->image->height);
                    // bb.writeBytes(&layer->image->fdata->bdata);
                //}

                // properties

                // layerdata
                bb.writeSize(layer->ldata->bdata.size());
                for (const int32 i : layer->ldata->bdata) {
                    bb.writeInt(i);
                }

                // objs
                bb.writeInt(layer->objs.size());
                for (const bright::SharedPtr<bmap::ObjectItem> &obj : layer->objs)
                {
                    /**
                     *      ::bright::int32 id;
                            ::bright::int32 gid;
                            ::bright::int32 x;
                            ::bright::int32 y;
                            ::bright::int32 width;
                            ::bright::int32 height;
                            ::bright::Vector<::bright::SharedPtr<bmap::Point>> polygon;
                            ::bright::Vector<::bright::SharedPtr<bmap::Point>> polyline;
                    */
                    bb.writeInt(obj->id);
                    bb.writeInt(obj->gid);
                    bb.writeInt(obj->x);
                    bb.writeInt(obj->y);
                    bb.writeInt(obj->width);
                    bb.writeInt(obj->height);

                    bb.writeInt(obj->polygon.size());
                    for (const bright::SharedPtr<bmap::Point> &p : obj->polygon)
                    {
                        bb.writeInt(p->x);
                        bb.writeInt(p->y);
                    }
                    bb.writeInt(obj->polyline.size());
                    for (const bright::SharedPtr<bmap::Point> &p : obj->polyline)
                    {
                        bb.writeInt(p->x);
                        bb.writeInt(p->y);
                    }
                }

                // layer (only for group layers)
                // bb.writeInt(layer->layers.size());
                // for (const bright::SharedPtr<bmap::Layer> l : layer->layers) {

                // }
            }

            file.write((const char*)bb.getDataUnsafe(), bb.size());
            file.close();
        }
        catch (std::exception &e)
        {
            mError = tr("Exception: %1").arg(tr(e.what()));
            return false;
        }

        return true;
    }

    void XBinMapFormat::writeImage(const Tiled::SharedTileset &ts,
                                   std::shared_ptr<bmap::TileSet> &item,
                                   const QUrl &source,
                                   const QPixmap &image,
                                   const QColor &transColor,
                                   const QSize size)
    {
        if (ts->image().isNull() && ts->imageSource().isEmpty())
        {
            // no need
            item->image->source = "";
            item->image->width = 0;
            item->image->height = 0;
            return;
        }
        else
        {

            // QString fileRef = toFileReference(source, mUseAbsolutePaths ? QString()
            //                                                     : mDir.path());
            item->image->source = Tiled::toFileReference(ts->imageSource(), mDir.path()).replace("/", "\\").toStdString();

            // if (ts->transparentColor().isValid()) item->image->trans = trans;
            item->image->width = ts->tileWidth();
            item->image->height = ts->tileHeight();
            if (ts->imageSource().isEmpty()) // 当前不会
            {
                // item->image->data->encoding = "base64";

                QBuffer buffer;
                ts->image().save(&buffer, "png");
                // 这个数据结构改二进制
                // item->image->fdata->bdata.reserve(buffer.data().size());
                // std::memcpy(item->image->fdata->bdata.data(), buffer.data().constData(), buffer.data().size());
            }
        }
    }
    void XBinMapFormat::writeTileLayer(std::shared_ptr<bmap::Layer> &bl, const Tiled::TileLayer &tileLayer)
    {
        writeLayerAttributes(bl, tileLayer);

        // properties...

        // need to adjust infinite...
        writeTileLayerData(bl, tileLayer, QRect(0, 0, tileLayer.width(), tileLayer.height()));
    }
    void XBinMapFormat::writeLayerAttributes(std::shared_ptr<bmap::Layer> &bl, const Tiled::Layer &layer)
    {
        // when image layer
        bl->image = std::make_shared<bmap::Image>();
        bl->image->source = "";
        bl->image->width = 0;
        bl->image->height = 0;

        bl->ldata = std::make_shared<bmap::LayerData>();

        bl->type = layer.layerType();
        // if (layer.id() != 0)
            bl->id = layer.id();
        // if (!layer.name().isEmpty())
            bl->name = layer.name().toStdString();
        const int x = layer.x();
        const int y = layer.y();
        const qreal opacity = layer.opacity();
        // if (x != 0)
            bl->x = x;
        // if (y != 0)
            bl->y = y;
        // if (layer.layerType() == Tiled::Layer::TileLayerType)
        {
            auto &tileLayer = static_cast<const Tiled::TileLayer &>(layer);
            int width = tileLayer.width();
            int height = tileLayer.height();

            bl->width = width;
            bl->height = height;
        }

        // if (!layer.isVisible())
            bl->visible = 0;
        // if (layer.isLocked())
            bl->locked = 1;
        // if (opacity != qreal(1))
            bl->opacity = opacity;
        if (layer.tintColor().isValid())
        {
            bl->tintcolor = Tiled::colorToString(layer.tintColor()).toStdString();
        }

        const QPointF offset = layer.offset();
        if (!offset.isNull())
        {
            bl->offsetx = offset.x();
            bl->offsety = offset.y();
        } else {
            bl->offsetx = 0;
            bl->offsety = 0;
        }

        // const QPointF parallaxFactor = layer.parallaxFactor();
        // if (parallaxFactor.x() != 1.0)
        //     w.writeAttribute(QStringLiteral("parallaxx"), QString::number(parallaxFactor.x()));
        // if (parallaxFactor.y() != 1.0)
        //     w.writeAttribute(QStringLiteral("parallaxy"), QString::number(parallaxFactor.y()));
    }
    void XBinMapFormat::writeTileLayerData(std::shared_ptr<bmap::Layer> &bl, const Tiled::TileLayer &tileLayer, QRect bounds)
    {
        // if (mLayerDataFormat == Map::XML) {
        //     for (int y = bounds.top(); y <= bounds.bottom(); y++) {
        //         for (int x = bounds.left(); x <= bounds.right(); x++) {
        //             const unsigned gid = mGidMapper.cellToGid(tileLayer.cellAt(x, y));
        //             w.writeStartElement(QStringLiteral("tile"));
        //             if (gid != 0)
        //                 w.writeAttribute(QStringLiteral("gid"), QString::number(gid));
        //             w.writeEndElement();
        //         }
        //     }
        // } else
        // if (mLayerDataFormat == Map::CSV) {
        //     QString chunkData;

        //     if (!mMinimize)
        //         chunkData.append(QLatin1Char('\n'));

        //     for (int y = bounds.top(); y <= bounds.bottom(); y++) {
        //         for (int x = bounds.left(); x <= bounds.right(); x++) {
        //             const unsigned gid = mGidMapper.cellToGid(tileLayer.cellAt(x, y));
        //             chunkData.append(QString::number(gid));
        //             if (x != bounds.right() || y != bounds.bottom())
        //                 chunkData.append(QLatin1Char(','));
        //         }
        //         if (!mMinimize)
        //             chunkData.append(QLatin1Char('\n'));
        //     }

        //     w.writeCharacters(chunkData);
        // } else
        {
            if (bounds.isEmpty())
                bounds = QRect(0, 0, tileLayer.width(), tileLayer.height());

            bl->ldata = std::make_shared<cfg::bmap::LayerData>();
            for (int y = bounds.top(); y <= bounds.bottom(); ++y)
            {
                for (int x = bounds.left(); x <= bounds.right(); ++x)
                {
                    const unsigned gid = mGidMapper.cellToGid(tileLayer.cellAt(x, y));
                    // bl->ldata->bdata.insert(bl->ldata->bdata.end(), static_cast<char>(gid));
                    // bl->ldata->bdata.insert(bl->ldata->bdata.end(), static_cast<char>(gid >> 8));
                    // bl->ldata->bdata.insert(bl->ldata->bdata.end(), static_cast<char>(gid >> 16));
                    // bl->ldata->bdata.insert(bl->ldata->bdata.end(), static_cast<char>(gid >> 24));
                    bl->ldata->bdata.insert(bl->ldata->bdata.end(), gid);
                }
            }
        }
    }
    void XBinMapFormat::writeObjectGroupForTile(bmap::Tile &bt, const Tiled::ObjectGroup &objectGroup)
    {
        // if (objectGroup.id() != 0)
        //     bt->id = objectGroup.id();
        // if (!objectGroup.name().isEmpty())
        //     bt->name = objectGroup.name().toStdString();
        // const int x = objectGroup.x();
        // const int y = objectGroup.y();
        // const qreal opacity = objectGroup.opacity();
        // if (x != 0)
        //     bt->x = x;
        // if (y != 0)
        //     bt->y = y;
        // if (objectGroup.layerType() == Tiled::Layer::TileLayerType) {
        //     auto &tileLayer = static_cast<const Tiled::TileLayer&>(objectGroup);
        //     int width = tileLayer.width();
        //     int height = tileLayer.height();

        //     bt->width = width;
        //     bt->height = height;
        // }

        // if (!objectGroup.isVisible())
        //     bt->visible = 0;
        // if (objectGroup.isLocked())
        //     bt->locked = 1;
        // if (opacity != qreal(1))
        //     bt->opacity = opacity;
        // if (objectGroup.tintColor().isValid()) {
        //     bt->tintcolor = Tiled::colorToString(objectGroup.tintColor()).toStdString();
        // }

        // const QPointF offset = objectGroup.offset();
        // if (!offset.isNull()) {
        //     bt->offsetx = offset.x();
        //     bt->offsety = offset.y();
        // }
    }
    void XBinMapFormat::writeObjectGroup(std::shared_ptr<bmap::Layer> &bl, const Tiled::ObjectGroup &objectGroup)
    {

        // if (objectGroup.color().isValid())
        //     w.writeAttribute(QStringLiteral("color"), colorToString(objectGroup.color()));

        // if (objectGroup.drawOrder() != Tiled::ObjectGroup::TopDownOrder) {
        //     w.writeAttribute(QStringLiteral("draworder"), drawOrderToString(objectGroup.drawOrder()));
        // }

        writeLayerAttributes(bl, objectGroup);

        // writeProperties(w, objectGroup.properties());

        // int idx = 0;
        bl->objs.reserve(objectGroup.objects().size());
        for (const Tiled::MapObject *mapObject : objectGroup.objects())
        {
            bright::SharedPtr<cfg::bmap::ObjectItem> oitem = std::make_shared<cfg::bmap::ObjectItem>();
            writeObject(oitem, *mapObject);
            bl->objs.push_back(oitem);
            // idx++;
        }
    }

    static bool shouldWrite(bool holdsInfo, bool isTemplateInstance, bool changed)
    {
        return isTemplateInstance ? changed : holdsInfo;
    }

    void XBinMapFormat::writeObject(bright::SharedPtr<cfg::bmap::ObjectItem> oitem, const Tiled::MapObject &mapObject)
    {
        const int id = mapObject.id();
        const QString &name = mapObject.name();
        const QString &className = mapObject.className();
        const QPointF pos = mapObject.position();

        bool isTemplateInstance = mapObject.isTemplateInstance();

        // if (!mapObject.isTemplateBase())
            oitem->id = id;

        if (const Tiled::ObjectTemplate *objectTemplate = mapObject.objectTemplate())
        {
            // QString fileName = objectTemplate->fileName();
            // if (!mUseAbsolutePaths)
            //     fileName = filePathRelativeTo(mDir, fileName);
            // w.writeAttribute(QStringLiteral("template"), fileName);
        }

        // if (shouldWrite(!name.isEmpty(), isTemplateInstance, mapObject.propertyChanged(Tiled::MapObject::NameProperty)))
        //     w.writeAttribute(QStringLiteral("name"), name);

        // if (!className.isEmpty())
        //     w.writeAttribute(FileFormat::classPropertyNameForObject(), className);

        // if (shouldWrite(!mapObject.cell().isEmpty(), isTemplateInstance, mapObject.propertyChanged(Tiled::MapObject::CellProperty)))
        {
            const unsigned gid = mGidMapper.cellToGid(mapObject.cell());
            oitem->gid = gid;
        }

        // if (!mapObject.isTemplateBase())
        {
            oitem->x = pos.x();
            oitem->y = pos.y();
        }

        // if (shouldWrite(true, isTemplateInstance, mapObject.propertyChanged(Tiled::MapObject::SizeProperty)))
        {
            const QSizeF size = mapObject.size();
            // if (size.width() != 0)
                oitem->width = size.width();
            // if (size.height() != 0)
                oitem->height = size.height();
        }

        // const qreal rotation = mapObject.rotation();
        // if (shouldWrite(rotation != 0.0, isTemplateInstance, mapObject.propertyChanged(Tiled::MapObject::RotationProperty)))
        //     w.writeAttribute(QStringLiteral("rotation"), QString::number(rotation));

        // if (shouldWrite(!mapObject.isVisible(), isTemplateInstance, mapObject.propertyChanged(Tiled::MapObject::VisibleProperty)))
        //     w.writeAttribute(QStringLiteral("visible"), QLatin1String(mapObject.isVisible() ? "1" : "0"));

        // writeProperties(w, mapObject.properties());

        switch (mapObject.shape())
        {
        case Tiled::MapObject::Rectangle:
            break;
        case Tiled::MapObject::Polygon:
        case Tiled::MapObject::Polyline:
        {
            // if (shouldWrite(true, isTemplateInstance, mapObject.propertyChanged(Tiled::MapObject::ShapeProperty)))
            {

                if (mapObject.shape() == Tiled::MapObject::Polygon)
                {
                    //oitem->polygon.clear();
                    for (const QPointF &point : mapObject.polygon())
                    {
                        bright::SharedPtr<bmap::Point> p(new bmap::Point());
                        p->x = point.x();
                        p->y = point.y();
                        oitem->polygon.insert(oitem->polygon.end(), p);
                    }
                }
                else
                {
                    //oitem->polyline.clear();
                    for (const QPointF &point : mapObject.polygon())
                    {
                        bright::SharedPtr<bmap::Point> p(new bmap::Point());
                        p->x = point.x();
                        p->y = point.y();
                        oitem->polyline.insert(oitem->polygon.end(), p);
                    }
                }
            }
            break;
        }
        case Tiled::MapObject::Ellipse:
            // if (shouldWrite(true, isTemplateInstance, mapObject.propertyChanged(Tiled::MapObject::ShapeProperty)))
            //     w.writeEmptyElement(QLatin1String("ellipse"));
            break;
        case Tiled::MapObject::Text:
        {
            if (shouldWrite(true, isTemplateInstance,
                            mapObject.propertyChanged(Tiled::MapObject::TextProperty) ||
                                mapObject.propertyChanged(Tiled::MapObject::TextFontProperty) ||
                                mapObject.propertyChanged(Tiled::MapObject::TextAlignmentProperty) ||
                                mapObject.propertyChanged(Tiled::MapObject::TextWordWrapProperty) ||
                                mapObject.propertyChanged(Tiled::MapObject::TextColorProperty)))
                // writeObjectText(w, mapObject.textData());
                break;
        }
        case Tiled::MapObject::Point:
            // if (shouldWrite(true, isTemplateInstance, mapObject.propertyChanged(Tiled::MapObject::ShapeProperty)))
            //     w.writeEmptyElement(QLatin1String("point"));
            break;
        }
    }
    QString XBinMapFormat::nameFilter() const
    {
        return tr("Xbin map files (*.bin)");
    }

    QString XBinMapFormat::shortName() const
    {
        return QStringLiteral("xbin");
    }

    bool XBinMapFormat::supportsFile(const QString &fileName) const
    {
        std::ifstream file(fileName.toStdString(), std::ios::in | std::ios::binary);
        if (!file)
            return false;

        // std::string magic(6, '\0');
        // file.read(&magic[0], static_cast<std::streamsize>(magic.length()));

        // return magic == "tBIN10";
        return true;
    }

    QString XBinMapFormat::errorString() const
    {
        return mError;
    }

} // namespace Tbin
