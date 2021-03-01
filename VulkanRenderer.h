//
// Created by lucius on 1/24/21.
//

#ifndef MATCH_MANUALLY_VULKANRENDERER_H
#define MATCH_MANUALLY_VULKANRENDERER_H

#include <QMutex>
#include "VulkanWindow.h"
#include "ImageGraphModel.h"
class QMenu;

#define MAX_IMAGE_NUM 256
#define MAX_KEYPOINT_NUM 10000*MAX_IMAGE_NUM

class VulkanRenderer : public QObject, public QVulkanWindowRenderer {
Q_OBJECT
public:
  explicit VulkanRenderer(VulkanWindow *vulkanWindow, ImageGraphModel *graphModel, GraphWidget *graphicsScene);

//  ~VulkanRenderer();
  void preInitResources() override;

  void initResources() override;

  void releaseResources() override;

  void initSwapChainResources() override;

  void releaseSwapChainResources() override;

  void startNextFrame() override;

  void setModel(ImageGraphModel *model);

  void setTrackScene(GraphWidget *graphicsScene);

  void addImage(int image_id);

  void removeImage(int image_id);

  void mousePressEvent(QMouseEvent *e);

  void mouseReleaseEvent(QMouseEvent *e);

  void mouseMoveEvent(QMouseEvent *e);

  void wheelEvent(QWheelEvent *e);

public slots:

  void dataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QVector<int> &roles);

  void updateImageKeypoints(int image_id);;

  void addTrackForKeypoint();

private:
  const VkFormat select_image_format = VK_FORMAT_R32G32B32A32_SFLOAT;
  VulkanWindow *m_window;
  QMenu *actionMenu;
  enum {
    RENDER_MODE_NORMAL,
    RENDER_MODE_TRACK
  } myMode = RENDER_MODE_NORMAL;
  Track_ID_T curr_track_id = std::numeric_limits<Track_ID_T>::max();
  ImageGraphModel *m_graphModel;
  GraphWidget *m_trackScene;

  VkDevice dev = VK_NULL_HANDLE;
  QVulkanDeviceFunctions *m_devFuncs = nullptr;

  VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
  VkPipelineCache pipelineCache = VK_NULL_HANDLE;

  VkSampler sampler;
  struct TextureData {
    int image_id;
    VkDeviceMemory memory;
    VkImage image;
    VkImageView imageView;
  };

  struct BufferData {
    VkDeviceMemory memory;
    VkBuffer buffer;
  };

  float image_min_depth = 1.0f;
  const float image_depth_internal = 2e-7;

  struct textureExtraInfo {
    Eigen::Matrix4f mat;
    float width;
    float height;
    float depth;
    uint8_t reserve[4];
  };
  static_assert(sizeof(textureExtraInfo[2]) == (16 + 4) * sizeof(float) * 2);
  std::vector<TextureData> texDatas;
  std::vector<TextureData> tex2remove;
  std::vector<textureExtraInfo> texExtraInfos;
  std::map<Image_ID_T, uint32_t> texIdMap;

  struct SceneInfo {
    Eigen::Matrix4f proj;
    Eigen::Vector2f windowSize;
    float pointSize;
  };
  SceneInfo sceneInfo;

  struct LineInfo {
    Eigen::Matrix4f mat;
    Eigen::Vector3f color;
    float depth;
    Eigen::Vector2f pos;
    float width;
    float height;
  };
  static_assert(sizeof(LineInfo[2]) == (16 + 3 + 1 + 2 + 2) * sizeof(float) * 2, "aa");
  std::vector<LineInfo> lines;

  struct VertexAttribute {
    float x;
    float y;
    uint32_t rgba;
  } __attribute__((packed));
  std::vector<VertexAttribute> vas;
  std::vector<VkDrawIndirectCommand> indirectDrawCmds;

  VkFence fence = VK_NULL_HANDLE;
  VkCommandBuffer stageCB = VK_NULL_HANDLE;
  VkCommandPool stagePool = VK_NULL_HANDLE;

  BufferData instBuf;
  uint8_t *instBufPtr = nullptr;
  struct {
    BufferData vert;
    VkDescriptorSetLayout descSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet descSet = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipeline pipeline_sel = VK_NULL_HANDLE;
  } imageMaterial;

  struct {
    VertexAttribute *vertStagePtr;
    BufferData vertStage;
    BufferData vert;
    BufferData indirectDrawBufStage;
    BufferData indirectDrawBuf;
    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;
    VkPipeline pipeline_sel;
  } kpMaterial;

  struct {
    BufferData vertStage;
    BufferData vert;
    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;
  } lineMaterial;

  struct OffscreenPass {
    BufferData pixeBuf;
    TextureData color, depth;
    VkRenderPass renderPass;
    VkFramebuffer framebuffer;
  } objSelectPass;

  ulong mouseLastTime = 0;
  Eigen::Vector2f mouseLastPos;
  struct {
    Eigen::Vector2f uv;
    uint32_t tex_id;
    uint32_t kp_id;
    uint32_t image_id;
    uint32_t image_kp_id;
  } selectInfo, selectInfoLast;
  bool vertexChange = false;
  bool lineChange = false;
  bool imageChange = false;

  VkShaderModule loadShader(const QString &filename);

  uint32_t getMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties);

  VkFormat getSupportedDepthFormat();

  BufferData createBuffer(uint32_t len, VkBufferUsageFlags usage, bool hostAccessEnable);

  void writeBuffer(VkBuffer buf, VkDeviceMemory memory, const void *data, VkDeviceSize offset, VkDeviceSize len);

  void copyBuffer(VkBuffer dstBuf, VkBuffer srcBuf, VkDeviceSize len);

  TextureData createImage(const QSize &sz, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
                          VkImageLayout imageLayout, bool imageOnly);

  void writeLinearImage(const QImage &img, VkImage image, VkDeviceMemory memory);

  void readLinearImage(const QImage &img, VkImage image, VkDeviceMemory memory);

  void uploadImage(VkCommandBuffer cb, VkImage dstImage, VkImage srcImage, const QSize &sz);

  void downloadImage(VkCommandBuffer cb, VkImage dstImage, VkImage srcImage, const QSize &sz);

  void beginStageCommandBuffer();

  void flushStageCommandBuffer();

  void createSelAttachment();

  void readPixel(VkCommandBuffer cb, VkBuffer buffer, VkImage image, const QPoint &pos);

  void modifySelKpColor();

  void updateResources();

  void createStageCommandBuffer();

  void createBuffers();

  void createSampler();

  void createDescriptorSets();

  void createPipelineLayouts();

  void createSelRenderPass();

  void createPipelines();

  void selectObject(const QPoint &pos);
};


#endif //MATCH_MANUALLY_VULKANRENDERER_H
