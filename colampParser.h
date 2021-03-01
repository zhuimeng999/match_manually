//
// Created by lucius on 1/15/21.
//

#ifndef MATCH_MANUALLY_SCENE_H
#define MATCH_MANUALLY_SCENE_H

#include <Eigen/Eigen>

class ColmapLoader {
public:
  typedef uint32_t camera_t;
  typedef uint32_t image_t;
  typedef uint32_t point2D_t;
  typedef uint64_t point3D_t;
  static const point3D_t kInvalidPoint3DId;

  struct SceneImageInfo {
    image_t image_id;
    camera_t camera_id;
    Eigen::Vector4d Qvec;
    Eigen::Vector3d Tvec;
    std::string name;
    std::vector<Eigen::Vector2d> points2D;
    std::vector<point3D_t> point3D_ids;
  };

  struct SceneCameraInfo {
    camera_t camera_id;
    int model_id;
    uint64_t width;
    uint64_t height;
    std::vector<double> params;
  };

  struct Point3D {
    Eigen::Vector3d XYZ;
    Eigen::Matrix<uint8_t, 3, 1> Color;
    double error;
    std::vector<std::pair<image_t, point2D_t>> track;
    point3D_t point3D_id;
  };

public:
  bool loadFromColmapSparseDir(const std::string &path);

  std::vector<SceneImageInfo> imagesInfo;
  std::vector<SceneCameraInfo> camerasInfo;
  std::vector<Point3D> points3D;

private:
  bool ReadText(const std::string &path);

  bool ReadBinary(const std::string &path);

//  void WriteText(const std::string &path) const;
//
//  void WriteBinary(const std::string &path) const;
//
//  void ReadCamerasText(const std::string &path);
//
//  void ReadImagesText(const std::string &path);
//
//  void ReadPoints3DText(const std::string &path);
//
  bool ReadCamerasBinary(const std::string &path);

  bool ReadImagesBinary(const std::string &path);

  bool ReadPoints3DBinary(const std::string &path);
//
//  void WriteCamerasText(const std::string &path) const;
//
//  void WriteImagesText(const std::string &path) const;
//
//  void WritePoints3DText(const std::string &path) const;
//
//  void WriteCamerasBinary(const std::string &path) const;
//
//  void WriteImagesBinary(const std::string &path) const;
//
//  void WritePoints3DBinary(const std::string &path) const;
};


#endif //MATCH_MANUALLY_SCENE_H
