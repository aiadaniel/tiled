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

#include <QTextStream>
#include <QCoreApplication>
#include <QDir>
#include <QtCore>

#include <cmath>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <iostream> 

#include "mapwriter.h"
#include "maptovariantconverter.h"

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
        addObject(new XBinTilesetFormat(this));
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
            QTextStream cout(stdout);

            bmap::BMap _map;

            // _map.version = FileFormat::versionString().toStdString();
            // _map.tiledversion = QCoreApplication::applicationVersion().toStdString();

            _map.orientation = static_cast<bmap::Orientation>(map->orientation());
            _map.renderorder = static_cast<bmap::RenderOrder>(map->renderOrder());

            _map.width = map->width();
            _map.height = map->height();
            _map.tilewidth = map->tileWidth();
            _map.tileheight = map->tileHeight();

            _map.infinite = map->infinite();

            if (map->orientation() == Tiled::Map::Hexagonal)
            {
                _map.hexsidelength = std::make_shared<bright::int32>(map->hexSideLength());
            } 
            if (map->orientation() == Tiled::Map::Staggered || map->orientation() == Tiled::Map::Hexagonal)
            {
                _map.staggeraxis = std::make_shared<bmap::StaggerAxis>( (bmap::StaggerAxis)(map->staggerAxis()) );
                _map.staggerindex = std::make_shared<bmap::StaggerIndex>( (bmap::StaggerIndex)(map->staggerIndex()) );
            } 

            // 这两个仅编辑器有用
            // _map.nextlayerid = map->nextLayerId();
            // _map.nextobjectid = map->nextObjectId();

            // properties...

            // const QDir fileDir(QFileInfo(fileName).dir());
            mDir = QFileInfo(fileName).dir();
            cout << "mDir:" << mDir.absolutePath() << Qt::endl;

            // 1.tileset
            mGidMapper.clear();
            unsigned firstGid = 1;
            for (const Tiled::SharedTileset &ts : map->tilesets())
            {
                if (firstGid > 0)
                {
                    
                    // item->firstgid = ( firstGid );//std::make_shared<bright::int32>
                    const QString &fileName = ts->fileName();
                    if (!fileName.isEmpty())
                    {
                        cout << "---tsfile:" << fileName << Qt::endl;
                        // 独立tsx文件 独立才支持复用
                        QString source = fileName;//todo 相对路径
                        QString temp = source.replace(".tsx", "");
                        QString temp2 = temp.replace(mDir.absolutePath(), "");
                        // cout << "temp2:" << temp2 << Qt::endl;
                        // temp = temp.mid(temp.lastIndexOf("/")+1);


                        // 直接在此增加tsx对应导出，解决手动一个一个导出的问题,see:  mainwindow.cpp->exportDocument
                        FileFormat::Options options;//两个option区分下
                        options |= FileFormat::WriteMinimized;
                        XBinTilesetFormat tsx = new XBinMapFormat();
                        QString fname = mDir.absolutePath() + temp2 + ".bin";
                        QString fname2 = temp2 + ".bin";
                        // cout << "fname2:" << fname2 << Qt::endl;
                        _map.tileset[firstGid] = fname2.toStdString().substr(1, fname2.length() - 1);
                        if (tsx.write(*ts, fname, options)) {
                            // cout << "write tsxbin " << fname << " success!" << Qt::endl;
                        } else {
                            cout << "write tsxbin " << fname << " failed!" << Qt::endl;
                        }


                        mGidMapper.insert(firstGid, ts);

                        firstGid += ts->nextTileId();
                        continue;
                    } 
                }
                else
                {
                    // version & tiledversion
                }
                
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
            //======================================================================================
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
            
            bb.writeBool(_map.hexsidelength != nullptr);
            if (_map.hexsidelength)
                bb.writeInt(*_map.hexsidelength);
            
            bb.writeBool(_map.staggeraxis != nullptr);
            if (_map.staggeraxis)
                bb.writeInt((int32_t)*_map.staggeraxis);
            
            bb.writeBool(_map.staggerindex != nullptr);
            if (_map.staggerindex)
                bb.writeInt((int32_t)*_map.staggerindex);
            
            // bb.writeInt(_map.nextlayerid);
            // bb.writeInt(_map.nextobjectid);
            
            // properties

            // ::bright::Vector<::bright::SharedPtr<bmap::TileSet>> tileset;
            bb.writeInt(_map.tileset.size());
            for (const auto& pair: _map.tileset) {
                bb.writeInt(pair.first);//firstgid
                bb.writeString(pair.second);//source
            }
            // for (const bright::SharedPtr<bmap::TileSet> &ts : _map.tileset)
            // {
                
            // }// end tileset

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
                bb.writeInt((int)layer->type);
                bb.writeInt(layer->id);
                bb.writeString(layer->name);
                bb.writeBool(layer->x != nullptr);
                if (layer->x) bb.writeInt(*layer->x);
                bb.writeBool(layer->y != nullptr);
                if (layer->y) bb.writeInt(*layer->y);
                // bb.writeInt(layer->width);
                // bb.writeInt(layer->height);
                bb.writeInt(layer->visible);
                bb.writeInt(layer->locked);
                bb.writeFloat(layer->opacity);
                bb.writeString(layer->tintcolor);
                bb.writeInt(layer->offsetx);
                bb.writeInt(layer->offsety);
                // when image layer
                // bb.writeInt(layer->repeatx);
                // bb.writeInt(layer->repeaty);
                // if (layer->type == Tiled::TileLayer::ImageLayerType) {
                bb.writeBool(layer->image != nullptr);
                if (layer->image) {
                    bb.writeString(layer->image->source);
                    bb.writeInt(layer->image->width);
                    bb.writeInt(layer->image->height);
                    // bb.writeBytes(&layer->image->fdata->bdata);
                }

                // properties

                // layerdata
                bb.writeBool(layer->ldata != nullptr);
                if (layer->ldata) {
                    bb.writeString(layer->ldata->bdata);
                    // bb.writeSize(layer->ldata->bdata.size());
                    // for (const int32 i : layer->ldata->bdata) {
                    //     bb.writeInt(i);
                    // }
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
                    bb.writeInt(obj->x);
                    bb.writeInt(obj->y);
                    bb.writeBool(obj->width != nullptr);
                    if (obj->width) bb.writeInt(*obj->width);
                    bb.writeBool(obj->height != nullptr);
                    if (obj->height) bb.writeInt(*obj->height);

                    bb.writeBool(obj->name != nullptr);
                    if (obj->name) bb.writeString(*obj->name);
                    bb.writeBool(obj->gid != nullptr);
                    if (obj->gid) bb.writeInt(*obj->gid);

                    bb.writeInt(obj->polygon.size());
                    for (const bright::SharedPtr<bmap::Point> &p : obj->polygon)
                    {
                        bb.writeInt(p->x);
                        bb.writeInt(p->y);
                    }
                    // bb.writeInt(obj->polyline.size());
                    // for (const bright::SharedPtr<bmap::Point> &p : obj->polyline)
                    // {
                    //     bb.writeInt(p->x);
                    //     bb.writeInt(p->y);
                    // }
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

    void XBinMapFormat::writeImage(
                                   std::shared_ptr<bmap::Image> &bimg,
                                   const QUrl &source,
                                   const QPixmap &image,
                                   const QColor &transColor,
                                   const QSize size)
    {
        if (image.isNull() && source.isEmpty())
        {
            // no need
            // item->image->source = "";
            // item->image->width = 0;
            // item->image->height = 0;
            return;
        }
        else
        {
            bimg = std::make_shared<bmap::Image>();
            // QString fileRef = toFileReference(source, mUseAbsolutePaths ? QString()
            //                                                     : mDir.path());
            // [lxm] 我们在编辑器直接指定图集和具体图片，不需要把全路径都写入，保留文件名即可
            QString temp = Tiled::toFileReference(source, mDir.path()).replace(".png", "");
            temp = temp.mid(temp.lastIndexOf("/")+1);
            bimg->source = temp.toStdString();

            // if (ts->transparentColor().isValid()) item->image->trans = trans;
            const QSize imageSize = image.isNull() ? size : image.size();
            bimg->width = imageSize.width();
            bimg->height = imageSize.height();
            // if (ts->imageSource().isEmpty()) // 当前不会
            // {
            //     // item->image->data->encoding = "base64";

            //     QBuffer buffer;
            //     ts->image().save(&buffer, "png");
            //     // 这个数据结构改二进制
            //     // item->image->fdata->bdata.reserve(buffer.data().size());
            //     // std::memcpy(item->image->fdata->bdata.data(), buffer.data().constData(), buffer.data().size());
            // }
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
        // bl->image = std::make_shared<bmap::Image>();
        // bl->image->source = "";
        // bl->image->width = 0;
        // bl->image->height = 0;

        // when tile layer
        // bl->ldata = std::make_shared<bmap::LayerData>();

        bl->type = (bmap::LayerType)layer.layerType();
        // if (layer.id() != 0)
            bl->id = layer.id();
        // if (!layer.name().isEmpty())
            bl->name = layer.name().toStdString();
        const int x = layer.x();
        const int y = layer.y();
        const qreal opacity = layer.opacity();
        if (x != 0)
            bl->x = std::make_shared<bright::int32>( x );
        if (y != 0)
            bl->y = std::make_shared<bright::int32>( y );
        if (layer.layerType() == Tiled::Layer::TileLayerType)
        {
            auto &tileLayer = static_cast<const Tiled::TileLayer &>(layer);
            int width = tileLayer.width();
            int height = tileLayer.height();

            // bl->width = width;
            // bl->height = height;
        } else {
            // bl->width = bl->height = 0;
        }

        if (!layer.isVisible()) bl->visible = 0;
        else bl->visible = 1;
        if (layer.isLocked()) bl->locked = 1;
        else bl->locked = 0;
        // if (opacity != qreal(1))
            bl->opacity = opacity;
        if (layer.tintColor().isValid())
        {
            bl->tintcolor = Tiled::colorToString(layer.tintColor()).toStdString();
        } else {
            bl->tintcolor = "";
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
            // if (bounds.isEmpty())
            //     bounds = QRect(0, 0, tileLayer.width(), tileLayer.height());

            // bl->ldata = std::make_shared<cfg::bmap::LayerData>();
            // for (int y = bounds.top(); y <= bounds.bottom(); ++y)
            // {
            //     for (int x = bounds.left(); x <= bounds.right(); ++x)
            //     {
            //         const unsigned gid = mGidMapper.cellToGid(tileLayer.cellAt(x, y));
            //         bl->ldata->bdata.emplace_back(gid);// from c++11 (old push_back)
            //     }
            // }

            QByteArray chunkData = mGidMapper.encodeLayerData(tileLayer,
                                                          mLayerDataFormat,
                                                          bounds,
                                                          mCompressionlevel);
            bl->ldata = std::make_shared<cfg::bmap::LayerData>();
            bl->ldata->bdata = QString::fromLatin1(chunkData).toStdString();
        }
    }
    void XBinMapFormat::writeObjectGroupForTile(bmap::Tile &bt, const Tiled::ObjectGroup &objectGroup)
    {
        if (objectGroup.objects().size() <= 0) 
            return;
        bt.objs = std::make_shared<bmap::Objs>();
        for (const Tiled::MapObject *mapObject : objectGroup.objects())
        {
            bright::SharedPtr<cfg::bmap::ObjectItem> oitem = std::make_shared<cfg::bmap::ObjectItem>();
            writeObject(oitem, *mapObject);
            bt.objs->objlist.emplace_back(oitem);
            // idx++;
        }
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
            bl->objs.emplace_back(oitem);
            // idx++;
        }
    }

    static bool shouldWrite(bool holdsInfo, bool isTemplateInstance, bool changed)
    {
        return isTemplateInstance ? changed : holdsInfo;
    }

    void XBinMapFormat::writeObject(bright::SharedPtr<cfg::bmap::ObjectItem> &oitem, const Tiled::MapObject &mapObject)
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
        if (!name.isEmpty())
            oitem->name = std::make_shared<bright::String>( name.toStdString() );

        // if (!className.isEmpty())
        //     w.writeAttribute(FileFormat::classPropertyNameForObject(), className);

        // if (shouldWrite(!mapObject.cell().isEmpty(), isTemplateInstance, mapObject.propertyChanged(Tiled::MapObject::CellProperty)))
        {
            const unsigned gid = mGidMapper.cellToGid(mapObject.cell());
            oitem->gid = std::make_shared<bright::int32>( gid );
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
                oitem->width = std::make_shared<bright::int32>( size.width() );
            // if (size.height() != 0)
                oitem->height = std::make_shared<bright::int32>( size.height() );
        }

        // const qreal rotation = mapObject.rotation();
        // if (shouldWrite(rotation != 0.0, isTemplateInstance, mapObject.propertyChanged(Tiled::MapObject::RotationProperty)))
        //     w.writeAttribute(QStringLiteral("rotation"), QString::number(rotation));

        // if (shouldWrite(!mapObject.isVisible(), isTemplateInstance, mapObject.propertyChanged(Tiled::MapObject::VisibleProperty)))
        //     w.writeAttribute(QStringLiteral("visible"), QLatin1String(mapObject.isVisible() ? "1" : "0"));

        // writeProperties(w, mapObject.properties());

        oitem->polygon.reserve(0);
        // oitem->polyline.reserve(0);
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
                    oitem->polygon.reserve(mapObject.polygon().size());
                    for (const QPointF &point : mapObject.polygon())
                    {
                        bright::SharedPtr<bmap::Point> p(new bmap::Point());
                        p->x = point.x();
                        p->y = point.y();
                        oitem->polygon.emplace_back(p);
                    }
                }
                else
                {
                    //oitem->polyline.clear();

                    // oitem->polyline.reserve(mapObject.polygon().size());
                    // for (const QPointF &point : mapObject.polygon())
                    // {
                    //     bright::SharedPtr<bmap::Point> p(new bmap::Point());
                    //     p->x = point.x();
                    //     p->y = point.y();
                    //     oitem->polyline.emplace_back(p);
                    // }
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
                
                // 直接使用name字段来存
                oitem->name = std::make_shared<bright::String>( mapObject.textData().text.toStdString() );
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
        return tr("Xbin map files (*.tm)");
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






XBinTilesetFormat::XBinTilesetFormat(QObject *parent)
    : Tiled::TilesetFormat(parent)
{
}

Tiled::SharedTileset XBinTilesetFormat::read(const QString &fileName)
{
    return nullptr;
}

bool XBinTilesetFormat::supportsFile(const QString &fileName) const
{
    return true;
}

bool XBinTilesetFormat::write(const Tiled::Tileset &tileset,
                              const QString &fileName,
                              Options options)
{
    QTextStream cout(stdout);

    ByteBuf bb(16 * 1024);

    mDir = QFileInfo(fileName).dir();
    std::ofstream file(fileName.toStdString(), std::ios::trunc | std::ios::binary);
    if (!file)
    {
        mError = tr("Could not open file for writing");
        return false;
    }

// 1 先构造这个对象
//======================================================================================

                std::shared_ptr<bmap::TileSet> item(new bmap::TileSet());

                item->name = tileset.name().toStdString();
                cout << "export tsx:" << (tileset.name()) << Qt::endl;
                item->tilewidth = tileset.tileWidth();
                item->tileheight = tileset.tileHeight();

                const int tileSpacing = tileset.tileSpacing();
                const int margin = tileset.margin();
                // if (tileSpacing != 0)
                {
                    item->spacing = tileSpacing;
                }
                // if (margin != 0)
                {
                    item->margin = margin;
                }
                item->tilecount = tileset.tileCount();
                item->columns = tileset.columnCount();

                //...

                // editor setting...

                const QPoint offset = tileset.tileOffset();
                if (!offset.isNull())
                {
                    item->tileoffset = std::make_shared<bmap::TileOffset>();
                    item->tileoffset->x = offset.x();
                    item->tileoffset->y = offset.y();
                }

                // orthogonal & gridsize...

                if (tileset.orientation() != Tiled::Tileset::Orthogonal || tileset.gridSize() != tileset.tileSize())
                {
                    item->grid = std::make_shared<bmap::Grid>();
                    item->grid->orientation = (bmap::Orientation)(tileset.orientation());
                    item->grid->width = tileset.gridSize().width();
                    item->grid->height = tileset.gridSize().height();
                }

                // transformation...

                // properties...

                // image
                // item->image = std::make_shared<bmap::Image>();
                writeImage(item->image, tileset.imageSource(), tileset.image(), tileset.transparentColor(), QSize(tileset.imageWidth(), tileset.imageHeight()));

                // *******************************************************all tiles
                const bool isCollection = tileset.isCollection();// 这个是指 零散图 还是 合图（image标签）
                const bool includeAllTiles = isCollection || tileset.anyTileOutOfOrder();
                for (const Tiled::Tile *tile : tileset.tiles())
                {
                    // if (includeAllTiles || includeTile(tile))
                    {
                        bright::SharedPtr<bmap::Tile> bt(new bmap::Tile());
                        bt->id = tile->id();
                        // cout << "tileid:" << bt->id << Qt::endl;
                        // ...

                        if (isCollection)
                        {
                            
                            writeImage(bt->image, tile->imageSource(), tile->image(), QColor(), tile->size());
                        }
                        // if (tile->objectGroup())
                        //     writeObjectGroupForTile(*bt, *(tile->objectGroup()));

                        if (tile->isAnimated())
                        {
                            bt->anis = std::make_shared<bmap::Anis>();
                            // std::shared_ptr<bmap::Anis> ani(new bmap::Anis());
                            const QVector<Tiled::Frame> &frames = tile->frames();
                            for (const Tiled::Frame &frame : frames)
                            {
                                std::shared_ptr<bmap::Point> v2(new bmap::Point());
                                v2->x = frame.tileId;
                                v2->y = frame.duration;
                                bt->anis->anilist.emplace_back(v2);
                            }
                        }
                        item->tiles.push_back(bt);
                    }
                }//end tiles

                // _map.tileset.push_back(item);
                // mGidMapper.insert(firstGid, ts);
                // firstGid += ts.nextTileId();

// 2 写对象
//======================================================================================

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
                // bb.writeBool(item->firstgid != nullptr);
                // if (ts->firstgid)
                    // bb.writeInt(item->firstgid);
                // bb.writeString(item->source);
                bb.writeString(item->name);
                bb.writeInt(item->tilewidth);
                bb.writeInt(item->tileheight);
                bb.writeInt(item->spacing);
                bb.writeInt(item->margin);
                bb.writeInt(item->tilecount);
                bb.writeInt(item->columns);
                // bb.writeString(item->backgroundcolor);
                // bb.writeString(item->objectalignment);
                // bb.writeString(item->tilerendersize);
                // bb.writeString(item->fillmode);

                // tileoffset
                bb.writeBool(item->tileoffset != nullptr);
                if (item->tileoffset) {
                    bb.writeInt(item->tileoffset->x);
                    bb.writeInt(item->tileoffset->y);
                }
                // grid
                bb.writeBool(item->grid != nullptr);
                if (item->grid) {
                    bb.writeInt((int)item->grid->orientation);
                    bb.writeInt(item->grid->width);
                    bb.writeInt(item->grid->height);
                }
                // properties

                // image
                bb.writeBool(item->image != nullptr);
                if (item->image) {
                    bb.writeString(item->image->source);
                    bb.writeInt(item->image->width);
                    bb.writeInt(item->image->height);
                    // bb.writeBytes(&item->image->fdata->bdata);
                }

                // tiles
                bb.writeInt(item->tiles.size());
                for (const bright::SharedPtr<bmap::Tile> &tile : item->tiles)
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
                    bb.writeBool(tile->image != nullptr);
                    if (tile->image) {
                        bb.writeString(tile->image->source);
                        bb.writeInt(tile->image->width);
                        bb.writeInt(tile->image->height);
                        // bb.writeBytes(&tile->image->fdata->bdata);
                    }

                    // objs
                    bb.writeBool(tile->objs != nullptr);
                    if (tile->objs) {
                        bb.writeInt(tile->objs->objlist.size());
                        for (const bright::SharedPtr<bmap::ObjectItem> &obj : tile->objs->objlist)
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
                            bb.writeInt(obj->x);
                            bb.writeInt(obj->y);
                            bb.writeBool(obj->width != nullptr);
                            if (obj->width) bb.writeInt(*obj->width);
                            bb.writeBool(obj->height != nullptr);
                            if (obj->height) bb.writeInt(*obj->height);

                            bb.writeBool(obj->name != nullptr);
                            if (obj->name) bb.writeString(*obj->name);
                            bb.writeBool(obj->gid != nullptr);
                            if (obj->gid) bb.writeInt(*obj->gid);

                            bb.writeInt(obj->polygon.size());
                            for (const bright::SharedPtr<bmap::Point> &p : obj->polygon)
                            {
                                bb.writeInt(p->x);
                                bb.writeInt(p->y);
                            }
                            // bb.writeInt(obj->polyline.size());
                            // for (const bright::SharedPtr<bmap::Point> &p : obj->polyline)
                            // {
                            //     bb.writeInt(p->x);
                            //     bb.writeInt(p->y);
                            // }
                        }
                    }

                    // animation
                    bb.writeBool(tile->anis != nullptr);
                    if (tile->anis) {
                        bb.writeInt(tile->anis->anilist.size());
                        for (bright::SharedPtr<bmap::Point> &ani : tile->anis->anilist)
                        {
                            bb.writeInt(ani->x);
                            bb.writeInt(ani->y);
                            // bb.writeVector2(ani);// float 
                        }
                    }
                    // properties

                }// end tiles



    file.write((const char*)bb.getDataUnsafe(), bb.size());
    file.close();

    return true;
}

void XBinTilesetFormat::writeImage(
                                std::shared_ptr<bmap::Image> &bimg,
                                const QUrl &source,
                                const QPixmap &image,
                                const QColor &transColor,
                                const QSize size)
{
    QTextStream cout(stdout);
    if (image.isNull() && source.isEmpty())
    {
        // no need
        // item->image->source = "";
        // item->image->width = 0;
        // item->image->height = 0;
        return;
    }
    else
    {
        bimg = std::make_shared<bmap::Image>();
        // QString temp = Tiled::toFileReference(source, QString());//.replace(".png", "");// 去扩展名 使用相对目录
        // [lxm] 我们在编辑器直接指定图集和具体图片，不需要把全路径都写入，保留文件名即可
        // [lxm] 二次更改回完整相对路径
        QString temp = Tiled::toFileReference(source, mDir.path());//.replace(".png", "");
        cout << "tsx temp:" << temp << Qt::endl;
        // temp = temp.mid(temp.lastIndexOf("/")+1);
        bimg->source = temp.toStdString();

        // if (ts->transparentColor().isValid()) item->image->trans = trans;
        const QSize imageSize = image.isNull() ? size : image.size();
        bimg->width = imageSize.width();
        bimg->height = imageSize.height();
        // if (ts->imageSource().isEmpty()) // 当前不会
        // {
        //     // item->image->data->encoding = "base64";

        //     QBuffer buffer;
        //     ts->image().save(&buffer, "png");
        //     // 这个数据结构改二进制
        //     // item->image->fdata->bdata.reserve(buffer.data().size());
        //     // std::memcpy(item->image->fdata->bdata.data(), buffer.data().constData(), buffer.data().size());
        // }
    }
}

QString XBinTilesetFormat::nameFilter() const
{
    return tr("xbin tileset files (*.tm)");
}

QString XBinTilesetFormat::shortName() const
{
    return QStringLiteral("xbintileset");
}

QString XBinTilesetFormat::errorString() const
{
    return mError;
}

} // namespace Tbin
