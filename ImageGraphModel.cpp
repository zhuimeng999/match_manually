//
// Created by lucius on 2/5/20.
//

#include "ImageGraphModel.h"
#include <QFileInfo>
#include <QMetaEnum>
#include <QDebug>

static const float depthDecederStep = std::numeric_limits<float>::epsilon() * 10;

ImageGraphModel::ImageGraphModel(QObject *parent) : QAbstractItemModel(parent), imageInfos() {
  qDebug() << "ImageGraphMode: total depth resolution " << static_cast<int >(2.0f / depthDecederStep);
}

ImageGraphModel::~ImageGraphModel() {

}

QModelIndex ImageGraphModel::index(int row, int column, const QModelIndex &parent) const {
  return hasIndex(row, column, parent) ? createIndex(row, column) : QModelIndex();
}

QModelIndex ImageGraphModel::parent(const QModelIndex &child) const {
  return QModelIndex();
}

int ImageGraphModel::rowCount(const QModelIndex &parent) const {
  return imageIndex.size();
}

int ImageGraphModel::columnCount(const QModelIndex &parent) const {
  auto columnInfo = QMetaEnum::fromType<ImageGraphModel::ColumnLabelMeta>();
  return columnInfo.keyCount();
}

Qt::ItemFlags ImageGraphModel::flags(const QModelIndex &index) const {
  auto defaultFlag = QAbstractItemModel::flags(index);
  if (index.isValid() and (index.column() == 0)) {
    return Qt::ItemIsUserCheckable | defaultFlag;
  }
  return defaultFlag;
}

QVariant ImageGraphModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid()) {
    return QVariant();
  }
  auto image_id = imageIndex[index.row()];
  switch (index.column()) {
    case 0:
      if ((role == Qt::EditRole) or (role == Qt::DisplayRole)) {
        return QFileInfo(imageInfos.at(image_id).path).baseName();
      } else if (role == Qt::CheckStateRole) {
        return imageInfos.at(image_id).checkState;
      } else if (role == Qt::DisplayPropertyRole) {
        return imageInfos.at(image_id).data;
      } else if (role == Qt::UserRole + 1) {
        return imageInfos.at(image_id).path;
      } else if (role == Qt::UserRole + 2) {
        return image_id;
      }
    default:
      break;
  }
  return QVariant();
}

bool ImageGraphModel::setData(const QModelIndex &index, const QVariant &value, int role) {
  if (role == Qt::UserRole) {

  } else if (index.isValid()) {
    auto image_id = imageIndex[index.row()];
    if (index.column() == 0) {
      if (role == Qt::CheckStateRole) {
        auto checkState = value.value<Qt::CheckState>();
        imageInfos.at(image_id).checkState = checkState;
        emit dataChanged(index, index, {role});
        return true;
      }
    }
  }
  return QAbstractItemModel::setData(index, value, role);
}

QVariant ImageGraphModel::headerData(int section, Qt::Orientation orientation, int role) const {
  if ((orientation == Qt::Horizontal) && ((role == Qt::DisplayRole) || (role == Qt::EditRole))) {
    return QMetaEnum::fromType<ImageGraphModel::ColumnLabelMeta>().key(section);
  }
  return QAbstractItemModel::headerData(section, orientation, role);
}

bool ImageGraphModel::appendImages(const std::vector<QString> &img_paths) {
  beginInsertRows(QModelIndex(), imageInfos.size(), imageInfos.size() + img_paths.size() - 1);
  for (const auto &img_path: img_paths) {
    addImage(img_path);
  }
  endInsertRows();
  return true;
}

bool ImageGraphModel::appendColmapData(const QString &image_dir, const ColmapLoader &loader){
  for(const auto &p: loader.points3D){
    Track tr = {
        .pos = p.XYZ.cast<float>(),
        .error = static_cast<float>(p.error),
        .track_id = p.point3D_id
    };
    tracks.emplace(p.point3D_id,std::move(tr));
    auto &images = tracks.at(p.point3D_id).images;
    auto &kps = tracks.at(p.point3D_id).kps;
    for(const auto &it: p.track){
      images.push_back(it.first);
      kps.push_back(it.second);
    }
    track_id_max = std::max(track_id_max, p.point3D_id);
  }
  beginInsertRows(QModelIndex(), imageInfos.size(), imageInfos.size() + loader.imagesInfo.size() - 1);
  for (const auto &img_info: loader.imagesInfo) {
    QString image_path = image_dir + "/" +QString::fromStdString(img_info.name);
    ImageInfo imageInfo = {
        .path = image_path,
        .checkState = Qt::Unchecked,
        .data = QImage(image_path),
        .image_id = img_info.image_id
    };
    const auto &img_size = imageInfo.data.size();
    imageInfos.emplace(img_info.image_id, std::move(imageInfo));
    imageIndex.push_back(img_info.image_id);
    auto &kps = imageInfos.at(img_info.image_id).keyPoints;
    for(uint32_t i = 0; i < img_info.points2D.size(); i++){
      Eigen::Vector2f kp_pos(img_info.points2D[i].x()/img_size.width(), img_info.points2D[i].y()/img_size.height());
      KeyPoint kp = {
          .pos = kp_pos,
          .track_id = img_info.point3D_ids[i],
          .image_id = img_info.image_id,
          .kp_id = i
      };

      kps.push_back(kp);
    }
    image_id_max = std::max(img_info.image_id, image_id_max);
  }
  endInsertRows();
  return true;
}

KeyPoint_ID_T ImageGraphModel::appendImageKeyPoint(Image_ID_T imgIdx, const Eigen::Vector2f &keyPoint) {
  KeyPoint kp = {
          .pos = keyPoint,
          .track_id = std::numeric_limits<Track_ID_T>::max(),
          .image_id = imgIdx,
          .kp_id = static_cast<KeyPoint_ID_T>(imageInfos[imgIdx].keyPoints.size())
  };

  imageInfos[imgIdx].keyPoints.push_back(kp);
  emit keyPointsInserted(imgIdx);
  return kp.kp_id;
}

Track_ID_T ImageGraphModel::getOrCreateTrackForKeypoint(Image_ID_T image_id, KeyPoint_ID_T kp_id) {
  auto &kp = imageInfos.at(image_id).keyPoints.at(kp_id);
  if(kp.track_id == std::numeric_limits<Track_ID_T>::max()){
    auto &tr = addTrack();
    kp.track_id = tr.track_id;
    tr.images.emplace_back(image_id);
    tr.kps.emplace_back(kp_id);
  }

  return kp.track_id;
}

bool ImageGraphModel::addKeypoint2Track(Track_ID_T track_id, Image_ID_T image_id, KeyPoint_ID_T kp_id) {
  auto &kp = imageInfos.at(image_id).keyPoints.at(kp_id);
  auto &tr = tracks.at(track_id);

  for (const auto it: tr.images) {
    if (it == image_id) {
      qWarning("there is another kp has same image_id in this track");
      return false;
    }
  }

  if (kp.track_id == std::numeric_limits<Track_ID_T>::max()) {
    kp.track_id = tr.track_id;
    tr.kps.emplace_back(kp.kp_id);
    tr.images.emplace_back(kp.image_id);
  } else if (kp.track_id == tr.track_id){
    qWarning("kp already in the track");
  } else {
    qWarning() << "merge track " << kp.track_id << " and " << tr.track_id;
    const auto &kp_tr = tracks.at(kp.track_id);
    if(checkVectorDuplicate(kp_tr.images, tr.images)){
      qFatal("there is duplicate image in these two track");
      return false;
    }
    for(size_t i = 0; i < kp_tr.kps.size(); i++){
      imageInfos[kp_tr.images[i]].keyPoints[kp_tr.kps[i]].track_id = tr.track_id;
    }
    tr.kps.insert(tr.kps.end(), kp_tr.kps.begin(), kp_tr.kps.end());
    tr.images.insert(tr.images.end(), kp_tr.images.begin(), kp_tr.images.end());
    tracks.erase(kp.track_id);
  }
  return true;
}

ImageInfo &ImageGraphModel::addImage(const QString &image_path)
{
  ImageInfo imageInfo = {
          .path = image_path,
          .checkState = Qt::Unchecked,
          .data = QImage(image_path),
          .image_id = image_id_max
  };
  imageIndex.push_back(imageInfo.image_id);
  imageInfos[image_id_max] = std::move(imageInfo);

  return imageInfos[image_id_max++];
}

Track &ImageGraphModel::addTrack()
{
  Track tr = {
          .pos = Eigen::Vector3f::Zero(),
          .error = 0.f,
          .track_id = track_id_max
  };
  tracks.emplace(track_id_max,std::move(tr));
  return tracks[track_id_max++];
}

bool ImageGraphModel::checkVectorDuplicate(std::vector<Image_ID_T> v1, std::vector<Image_ID_T> v2)
{
  std::sort(v1.begin(), v1.end());
  std::sort(v2.begin(), v2.end());
  size_t i = 0;
  size_t j = 0;
  while (true){
    if((i == v1.size()) || (j = v2.size())){
      return false;
    }
    if(v1[i] == v2[j]){
      return true;
    } else if(v1[i] < v2[j]){
      i++;
    } else {
      j++;
    }
  }
}