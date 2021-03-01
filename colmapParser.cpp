//
// Created by lucius on 1/15/21.
//

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <boost/log/trivial.hpp>
#include <boost/filesystem.hpp>
#include "colampParser.h"

namespace fs = boost::filesystem;

bool ColmapLoader::loadFromColmapSparseDir(const std::string &path) {
  if (fs::is_regular_file(path + "/cameras.bin") &&
      fs::is_regular_file(path + "/images.bin") &&
      fs::is_regular_file(path + "/points3D.bin")) {
    return ReadBinary(path);
  } else if (fs::is_regular_file(path + "/cameras.txt") &&
             fs::is_regular_file(path + "/images.txt") &&
             fs::is_regular_file(path + "/points3D.txt")) {
    return ReadText(path);
  }
  BOOST_LOG_TRIVIAL(error) << "cameras, images, points3D files do not exist at " << path;
  return false;
}

bool ColmapLoader::ReadText(const std::string &path) {
  return false;
}

bool ColmapLoader::ReadBinary(const std::string &path) {
  return ReadCamerasBinary(path + "/cameras.bin") &&
         ReadImagesBinary(path + "/images.bin") &&
         ReadPoints3DBinary(path + "/points3D.bin");
}

struct BinaryImageInfoReadHelper1 {
  ColmapLoader::image_t image_id;
  double QVec[4];
  double TVec[3];
  ColmapLoader::camera_t camera_id;
} __attribute__((packed));

struct BinaryImageInfoReadHelper2 {
  double x;
  double y;
  ColmapLoader::point3D_t point3D_id;
} __attribute__((packed));

bool ColmapLoader::ReadImagesBinary(const std::string &path) {
  const size_t fileSize = fs::file_size(path);
  int fd = open(path.c_str(), O_RDONLY, 0);
  if (fd == -1) {
    BOOST_LOG_TRIVIAL(warning) << "unable to open binary image data file " << path;
    return false;
  }
  void *mmappedData = mmap(NULL, fileSize, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0);
  char *bytes = static_cast<char *>(mmappedData);
  size_t pos = 0;
  imagesInfo.resize(*reinterpret_cast<uint64_t *>(bytes + pos));
  pos = pos + sizeof(uint64_t);

  BOOST_LOG_TRIVIAL(info) << "total " << imagesInfo.size() << "imags";
  for (auto &img :imagesInfo) {
    const BinaryImageInfoReadHelper1 *const helper1 = reinterpret_cast<BinaryImageInfoReadHelper1 *>(bytes + pos);
    img.image_id = helper1->image_id;
    memcpy(img.Qvec.data(), &helper1->QVec[0], sizeof(helper1->QVec));
    memcpy(img.Tvec.data(), &helper1->TVec[0], sizeof(helper1->TVec));
    img.camera_id = helper1->camera_id;
    pos = pos + sizeof(BinaryImageInfoReadHelper1);

    img.name = bytes + pos;
    pos = pos + img.name.size() + 1;

    BOOST_LOG_TRIVIAL(debug) << img.name;
    img.points2D.resize(*reinterpret_cast<uint64_t *>(bytes + pos));
    pos = pos + sizeof(uint64_t);

    img.point3D_ids.resize(img.points2D.size());
    const BinaryImageInfoReadHelper2 *const helper2 = reinterpret_cast<BinaryImageInfoReadHelper2 *>(bytes + pos);
    for (int i = 0; i < img.points2D.size(); i++) {
      img.points2D[i].x() = helper2[i].x;
      img.points2D[i].y() = helper2[i].y;
      img.point3D_ids[i] = helper2[i].point3D_id;
    }
    pos = pos + sizeof(BinaryImageInfoReadHelper2) * img.points2D.size();
  }
  munmap(mmappedData, fileSize);
  close(fd);
  assert(fileSize == pos);
  return true;
}

const std::vector<std::pair<std::string, int>> CAMERA_INFOS = {
        {"SIMPLE_PINHOLE", 3},  // 0
        {"PINHOLE",        4},         // 1
        {"SIMPLE_RADIAL",  4},   // 2
        {"RADIAL",         5},          // 3
        {"OPENCV",         8},          // 4
        {"OPENCV_FISHEYE", 8},  // 5
        {"FULL_OPENCV",    12}     //6
};

struct BinaryCameraInfoReadHelper {
  ColmapLoader::camera_t camera_id;
  int model_id;
  uint64_t width;
  uint64_t height;
} __attribute__((packed));

bool ColmapLoader::ReadCamerasBinary(const std::string &path) {
  const size_t fileSize = fs::file_size(path);
  int fd = open(path.c_str(), O_RDONLY, 0);
  if (fd == -1) {
    BOOST_LOG_TRIVIAL(warning) << "unable to open binary camera data file " << path;
    return false;
  }
  void *mmappedData = mmap(NULL, fileSize, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0);
  char *bytes = static_cast<char *>(mmappedData);
  size_t pos = 0;
  camerasInfo.resize(*reinterpret_cast<uint64_t *>(bytes + pos));
  pos = pos + sizeof(uint64_t);

  BOOST_LOG_TRIVIAL(info) << "total " << camerasInfo.size() << " cameras";
  for (auto &camera :camerasInfo) {
    const BinaryCameraInfoReadHelper *const helper = reinterpret_cast<BinaryCameraInfoReadHelper *>(bytes + pos);
    camera.camera_id = helper->camera_id;
    camera.model_id = helper->model_id;
    camera.width = helper->width;
    camera.height = helper->height;
    pos = pos + sizeof(BinaryCameraInfoReadHelper);
    camera.params.resize(CAMERA_INFOS.at(camera.model_id).second);
    const size_t params_mem_size = camera.params.size() * sizeof(camera.params[0]);
    memcpy(camera.params.data(), bytes + pos, params_mem_size);
    pos = pos + params_mem_size;
  }
  munmap(mmappedData, fileSize);
  close(fd);
  assert(fileSize == pos);
  return true;
}

struct BinaryPoints3DInfoReadHelper {
  ColmapLoader::point3D_t point3D_id;
  double XYZ[3];
  uint8_t Color[3];
  double error;
  uint64_t track_length;
  struct {
    ColmapLoader::image_t image_id;
    ColmapLoader::point2D_t point2D_idx;
  } __attribute__((packed)) tracks[];
} __attribute__((packed));

bool ColmapLoader::ReadPoints3DBinary(const std::string &path) {
  const size_t fileSize = fs::file_size(path);
  int fd = open(path.c_str(), O_RDONLY, 0);
  if (fd == -1) {
    BOOST_LOG_TRIVIAL(warning) << "unable to open binary camera data file " << path;
    return false;
  }
  void *mmappedData = mmap(NULL, fileSize, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0);
  char *bytes = static_cast<char *>(mmappedData);
  size_t pos = 0;
  points3D.resize(*reinterpret_cast<uint64_t *>(bytes + pos));
  pos = pos + sizeof(uint64_t);

  BOOST_LOG_TRIVIAL(info) << "total " << points3D.size() << " 3d point, loading ...";
  for (auto &p3: points3D) {
    const BinaryPoints3DInfoReadHelper *const helper = reinterpret_cast<BinaryPoints3DInfoReadHelper *>(bytes + pos);
    p3.point3D_id = helper->point3D_id;
    p3.XYZ.x() = helper->XYZ[0];
    p3.XYZ.y() = helper->XYZ[1];
    p3.XYZ.z() = helper->XYZ[2];
    p3.Color.x() = helper->Color[0];
    p3.Color.y() = helper->Color[1];
    p3.Color.z() = helper->Color[2];
    p3.error = helper->error;
    p3.track.resize(helper->track_length);
    for (int i = 0; i < p3.track.size(); i++) {
      p3.track[i].first = helper->tracks[i].image_id;
      p3.track[i].second = helper->tracks[i].point2D_idx;
    }
    pos = pos + sizeof(BinaryPoints3DInfoReadHelper) + sizeof(helper->tracks[0]) * p3.track.size();
  }
  munmap(mmappedData, fileSize);
  close(fd);
  assert(fileSize == pos);
  BOOST_LOG_TRIVIAL(info) << "3d point loading done";
  return true;
}
