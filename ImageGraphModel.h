//
// Created by lucius on 2/5/20.
//

#ifndef MATCH_MANUALLY_IMAGEGRAPHMODEL_H
#define MATCH_MANUALLY_IMAGEGRAPHMODEL_H

#include <QAbstractItemModel>
#include <QImage>
#include <string>
#include <vector>
#include <Eigen/Eigen>
#include "colampParser.h"

typedef uint64_t Track_ID_T;
typedef uint32_t KeyPoint_ID_T;
typedef uint32_t Image_ID_T;
typedef uint32_t Shape_ID_T;

struct Track {
  Eigen::Vector3f pos;
  std::vector<Image_ID_T> images;
  std::vector<KeyPoint_ID_T> kps;
  std::vector<int> shapes;
  float error;
  Track_ID_T track_id;
};

struct KeyPoint {
  Eigen::Vector2f pos;
  Track_ID_T track_id;
  Image_ID_T image_id;
  KeyPoint_ID_T kp_id;
};

struct ImageInfo {
  QString path;
  Qt::CheckState checkState;
  QImage data;
  std::vector<KeyPoint> keyPoints;
  Image_ID_T image_id;
};

class ImageGraphModel : public QAbstractItemModel {
Q_OBJECT
public:
  std::map<Image_ID_T, ImageInfo> imageInfos;
  std::vector<Image_ID_T> imageIndex;
  Image_ID_T image_id_max = 0;
  std::map<Track_ID_T, Track> tracks;
  Track_ID_T track_id_max = 0;

  enum ColumnLabelMeta {
    name,
    depth
  };

  Q_ENUM(ColumnLabelMeta);

  explicit ImageGraphModel(QObject *parent = nullptr);

  ~ImageGraphModel() override;

  QModelIndex index(int row, int column, const QModelIndex &parent) const override;

  QModelIndex parent(const QModelIndex &child) const override;

  int rowCount(const QModelIndex &parent) const override;

  int columnCount(const QModelIndex &parent) const override;

  Qt::ItemFlags flags(const QModelIndex &index) const override;

  QVariant data(const QModelIndex &index, int role) const override;

  bool setData(const QModelIndex &index, const QVariant &value, int role) override;

  bool appendImages(const std::vector<QString> &img_paths);

  bool appendColmapData(const QString &image_dir, const ColmapLoader &loader);

  KeyPoint_ID_T appendImageKeyPoint(Image_ID_T imgIdx, const Eigen::Vector2f &keyPoint);

  Track_ID_T getOrCreateTrackForKeypoint(Image_ID_T image_id, KeyPoint_ID_T kp_id);

  bool addKeypoint2Track(Track_ID_T track_id, Image_ID_T image_id, KeyPoint_ID_T kp_id);

  QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

signals:

  void keyPointsInserted(int imgIdx);

private:
  ImageInfo &addImage(const QString &image_path);
  Track &addTrack();
  bool checkVectorDuplicate(std::vector<Image_ID_T> v1, std::vector<Image_ID_T> v2);
};


#endif //MATCH_MANUALLY_IMAGEGRAPHMODEL_H
