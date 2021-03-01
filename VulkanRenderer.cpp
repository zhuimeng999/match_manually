//
// Created by lucius on 1/24/21.
//

#include <QVulkanFunctions>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QMenu>
#include <QGraphicsPixmapItem>
#include "graphwidget.h"
#include "VulkanRenderer.h"
#include "ImageGraphModel.h"

static const float quadVert[] = {
        0, 0,
        0, 1,
        1, 0,
        1, 1
};

#define ARRAY_SIZE(arr) (sizeof(arr)/sizeof((arr)[0]))

VulkanRenderer::VulkanRenderer(VulkanWindow *vulkanWindow, ImageGraphModel *graphModel, GraphWidget *graphicsScene) :
        m_window(vulkanWindow), m_graphModel(graphModel), m_trackScene(graphicsScene) {
  sceneInfo.proj.setIdentity();
  sceneInfo.pointSize = 10.f;

  actionMenu = new QMenu;
  auto *addTrackAction = actionMenu->addAction("add track");
  connect(addTrackAction, &QAction::triggered, this, &VulkanRenderer::addTrackForKeypoint);

  if (m_graphModel) {
    connect(m_graphModel, &ImageGraphModel::dataChanged, this, &VulkanRenderer::dataChanged);
    connect(m_graphModel, &ImageGraphModel::keyPointsInserted, this, &VulkanRenderer::updateImageKeypoints);
  }
}

//VulkanRenderer::~VulkanRenderer()
//{
//}

void VulkanRenderer::preInitResources() {
  Q_ASSERT(m_window->supportedSampleCounts().contains(8));
  m_window->setSampleCount(8);
  VkFormatProperties formatProperties;
  m_window->vulkanInstance()->functions()->vkGetPhysicalDeviceFormatProperties(m_window->physicalDevice(), VK_FORMAT_R8G8B8A8_UNORM, &formatProperties);
  qDebug() << formatProperties.bufferFeatures;
  assert(formatProperties.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT);
}

void VulkanRenderer::initResources() {
  qDebug("initResources");

  dev = m_window->device();
  m_devFuncs = m_window->vulkanInstance()->deviceFunctions(dev);

  createStageCommandBuffer();
  createBuffers();
  createSampler();
  createDescriptorSets();
  createPipelineLayouts();

  createSelRenderPass();

  createPipelines();
}

void VulkanRenderer::releaseResources() {
  qDebug("releaseResources");

  m_devFuncs->vkDestroyPipelineCache(dev, pipelineCache, nullptr);
  pipelineCache = VK_NULL_HANDLE;
  m_devFuncs->vkDestroyDescriptorPool(dev, descriptorPool, nullptr);
  descriptorPool = VK_NULL_HANDLE;
}

void VulkanRenderer::initSwapChainResources() {
  sceneInfo.windowSize.x() = m_window->swapChainImageSize().width();
  sceneInfo.windowSize.y() = m_window->swapChainImageSize().height();
  createSelAttachment();
}

void VulkanRenderer::releaseSwapChainResources() {

}

void VulkanRenderer::startNextFrame() {
  const QSize sz = m_window->swapChainImageSize();
  assert(m_window->sampleCountFlagBits() > VK_SAMPLE_COUNT_1_BIT);
  VkClearValue clearValues[] = {
          {
                  .color = {.float32 = {1., 1., 1., 1.}}
          },
          {
                  .depthStencil = {1., 0}
          },
          {
                  .color = {.float32 = {1., 1., 1., 1.}}
          }
  };
  VkCommandBuffer cb = m_window->currentCommandBuffer();
  VkRenderPassBeginInfo renderPassBeginInfo = {
          .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
          .pNext = nullptr,
          .renderPass = m_window->defaultRenderPass(),
          .framebuffer = m_window->currentFramebuffer(),
          .renderArea = {
                  .offset = {
                          .x = 0,
                          .y = 0
                  },
                  .extent = {
                          .width = static_cast<uint32_t>(sz.width()),
                          .height = static_cast<uint32_t>(sz.height())
                  }},
          .clearValueCount = sizeof(clearValues) / sizeof(clearValues[0]),
          .pClearValues = clearValues
  };

  m_devFuncs->vkCmdBeginRenderPass(cb, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

  VkViewport viewport;
  viewport.x = viewport.y = 0;
  viewport.width = sz.width();
  viewport.height = sz.height();
  viewport.minDepth = 0;
  viewport.maxDepth = 1;
  m_devFuncs->vkCmdSetViewport(cb, 0, 1, &viewport);

  VkRect2D scissor;
  scissor.offset.x = scissor.offset.y = 0;
  scissor.extent.width = viewport.width;
  scissor.extent.height = viewport.height;
  m_devFuncs->vkCmdSetScissor(cb, 0, 1, &scissor);

  if (!texIdMap.empty()) {
    updateResources();

    m_devFuncs->vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, imageMaterial.pipeline);
    VkBuffer imageVertBuffs[] = {imageMaterial.vert.buffer, instBuf.buffer};
    VkDeviceSize imageVertOffsets[] = {0, 0};
    m_devFuncs->vkCmdBindVertexBuffers(cb, 0, ARRAY_SIZE(imageVertBuffs), imageVertBuffs, imageVertOffsets);
    m_devFuncs->vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, imageMaterial.pipelineLayout, 0, 1,
                                        &imageMaterial.descSet, 0,
                                        nullptr);
    m_devFuncs->vkCmdPushConstants(cb, imageMaterial.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(sceneInfo),
                                   &sceneInfo);
    m_devFuncs->vkCmdDraw(cb, 4, texIdMap.size(), 0, 0);

    m_devFuncs->vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, kpMaterial.pipeline);
    /* share inst buf, do not update, here we only change binding 0*/
    VkDeviceSize kpVertOffsets = 0;
//    imageVertBuffs[0] = kpMaterial.vert.buffer;
    m_devFuncs->vkCmdBindVertexBuffers(cb, 0, 1, &kpMaterial.vert.buffer, &kpVertOffsets);
//    m_devFuncs->vkCmdPushConstants(cb, kpMaterial.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(sceneInfo), &sceneInfo);
    m_devFuncs->vkCmdDrawIndirect(cb, kpMaterial.indirectDrawBuf.buffer, 0, texDatas.size(),
                                  sizeof(VkDrawIndirectCommand));
  }

  if (!lines.empty()) {
    m_devFuncs->vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, lineMaterial.pipeline);
    VkDeviceSize lineVertOffset = 0;
    m_devFuncs->vkCmdBindVertexBuffers(cb, 0, 1, &lineMaterial.vert.buffer, &lineVertOffset);
    m_devFuncs->vkCmdPushConstants(cb, kpMaterial.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(sceneInfo),
                                   &sceneInfo);
    m_devFuncs->vkCmdDraw(cb, 0, 0, 0, 0);
  }

  m_devFuncs->vkCmdEndRenderPass(cb);
  m_window->frameReady();
}

void VulkanRenderer::updateResources() {
  memcpy(instBufPtr, texExtraInfos.data(), sizeof(texExtraInfos[0]) * texExtraInfos.size());

  if (imageChange) {
    std::vector<VkDescriptorImageInfo> descriptorImageInfos;
    descriptorImageInfos.reserve(texIdMap.size());
    for (const auto &it : texIdMap) {
      descriptorImageInfos.push_back(
              {sampler, texDatas[it.second].imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
    }

    VkWriteDescriptorSet writeDescriptorSet[] = {
            {
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .pNext = nullptr,
                    .dstSet = imageMaterial.descSet,
                    .dstBinding = 0,
                    .dstArrayElement = 0,
                    .descriptorCount = static_cast<uint32_t>(descriptorImageInfos.size()),
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .pImageInfo = descriptorImageInfos.data(),
                    .pBufferInfo = nullptr,
                    .pTexelBufferView = nullptr
            }
    };
    m_devFuncs->vkUpdateDescriptorSets(dev, ARRAY_SIZE(writeDescriptorSet), writeDescriptorSet, 0, nullptr);
    imageChange = false;
  }

  if (vertexChange) {
    beginStageCommandBuffer();
    VkDeviceSize total_vertex = 0;
    for (const auto &it: texDatas) {
      total_vertex += m_graphModel->imageInfos.at(it.image_id).keyPoints.size();
    }
    copyBuffer(kpMaterial.vert.buffer, kpMaterial.vertStage.buffer, total_vertex * sizeof(VertexAttribute));
    copyBuffer(kpMaterial.indirectDrawBuf.buffer, kpMaterial.indirectDrawBufStage.buffer,
               texDatas.size() * sizeof(VkDrawIndirectCommand));
    flushStageCommandBuffer();
    vertexChange = false;
  }
}

VkShaderModule VulkanRenderer::loadShader(const QString &filename) {
  QFile file(filename);
  if (!file.open(QIODevice::ReadOnly)) {
    qWarning("Failed to read shader %s", qPrintable(filename));
    return VK_NULL_HANDLE;
  }
  QByteArray blob = file.readAll();
  file.close();

  VkShaderModuleCreateInfo createInfo;
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.pNext = nullptr;
  createInfo.codeSize = blob.size();
  createInfo.pCode = reinterpret_cast<const uint32_t *>(blob.data());
  createInfo.flags = 0;
  VkShaderModule shaderModule;
  if (m_devFuncs->vkCreateShaderModule(dev, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
    qFatal("can not create shader module");
    return VK_NULL_HANDLE;
  }
  return shaderModule;
}

uint32_t VulkanRenderer::getMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties) {
  VkPhysicalDeviceMemoryProperties physicalDeviceMemoryProperties;
  m_window->vulkanInstance()->functions()->vkGetPhysicalDeviceMemoryProperties(m_window->physicalDevice(),
                                                                               &physicalDeviceMemoryProperties);
  for (uint32_t i = 0; i < physicalDeviceMemoryProperties.memoryTypeCount; i++) {
    if ((typeBits & 1) == 1) {
      if ((physicalDeviceMemoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
        return i;
      }
    }
    typeBits >>= 1;
  }
  qFatal("can not find proper memory type");
}

VkFormat VulkanRenderer::getSupportedDepthFormat() {
  // Since all depth formats may be optional, we need to find a suitable depth format to use
  // Start with the highest precision packed format
  std::vector<VkFormat> depthFormats = {
          VK_FORMAT_D32_SFLOAT_S8_UINT,
          VK_FORMAT_D32_SFLOAT,
          VK_FORMAT_D24_UNORM_S8_UINT,
          VK_FORMAT_D16_UNORM_S8_UINT,
          VK_FORMAT_D16_UNORM
  };

  for (auto &format : depthFormats) {
    VkFormatProperties formatProps;
    m_window->vulkanInstance()->functions()->vkGetPhysicalDeviceFormatProperties(m_window->physicalDevice(), format,
                                                                                 &formatProps);
    // Format must support depth stencil attachment for optimal tiling
    if (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
      return format;
    }
  }
  qFatal("can not find depth format");
  return VK_FORMAT_UNDEFINED;
}

VulkanRenderer::BufferData VulkanRenderer::createBuffer(uint32_t len, VkBufferUsageFlags usage, bool hostAccessEnable) {
  VkBufferCreateInfo bufferCreateInfo = {
          .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
          .pNext = nullptr,
          .flags = 0,
          .size = len,
          .usage = usage,
          .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
          .queueFamilyIndexCount = 0,
          .pQueueFamilyIndices = nullptr
  };
  VkBuffer buffer;
  if (m_devFuncs->vkCreateBuffer(dev, &bufferCreateInfo, nullptr, &buffer) != VK_SUCCESS) {
    qFatal("can not create uniform buffer");
  }
  VkMemoryRequirements memoryRequirements;
  m_devFuncs->vkGetBufferMemoryRequirements(dev, buffer, &memoryRequirements);
  uint32_t memProp = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  if (hostAccessEnable) {
    memProp = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  }
  VkMemoryAllocateInfo memoryAllocateInfo = {
          .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
          .pNext = nullptr,
          .allocationSize = memoryRequirements.size,
          .memoryTypeIndex = getMemoryType(memoryRequirements.memoryTypeBits, memProp)
  };
  VkDeviceMemory memory;
  if (m_devFuncs->vkAllocateMemory(dev, &memoryAllocateInfo, nullptr, &memory) != VK_SUCCESS) {
    qFatal("can not allocate memory");
  }
  if (m_devFuncs->vkBindBufferMemory(dev, buffer, memory, 0) != VK_SUCCESS) {
    qFatal("can not bind buffer memory");
  }
  return {memory, buffer};
}

void VulkanRenderer::writeBuffer(VkBuffer buf, VkDeviceMemory memory, const void *data, VkDeviceSize offset, VkDeviceSize len) {
  uint8_t *devMemPtr;
  if (m_devFuncs->vkMapMemory(dev, memory, offset, len, 0, reinterpret_cast<void **>(&devMemPtr)) != VK_SUCCESS) {
    qFatal("can not map memory");
  }
  memcpy(devMemPtr, data, len);
  m_devFuncs->vkUnmapMemory(dev, memory);
}

void VulkanRenderer::copyBuffer(VkBuffer dstBuf, VkBuffer srcBuf, VkDeviceSize len) {
  VkBufferCopy bufferCopy = {
          .srcOffset = 0,
          .dstOffset = 0,
          .size = len
  };
  m_devFuncs->vkCmdCopyBuffer(stageCB, srcBuf, dstBuf, 1, &bufferCopy);
}

VulkanRenderer::TextureData
VulkanRenderer::createImage(const QSize &sz, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
                            VkImageLayout imageLayout, bool imageOnly) {
  VkImageCreateInfo imageCreateInfo;
  imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageCreateInfo.pNext = nullptr;
  imageCreateInfo.flags = 0;
  imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
  imageCreateInfo.format = format;
  imageCreateInfo.extent.width = sz.width();
  imageCreateInfo.extent.height = sz.height();
  imageCreateInfo.extent.depth = 1;
  imageCreateInfo.mipLevels = 1;
  imageCreateInfo.arrayLayers = 1;
  imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageCreateInfo.tiling = tiling;
  imageCreateInfo.usage = usage;
  imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageCreateInfo.queueFamilyIndexCount = 0;
  imageCreateInfo.pQueueFamilyIndices = nullptr;
  imageCreateInfo.initialLayout = imageLayout;

  VkImage image;
  if (m_devFuncs->vkCreateImage(dev, &imageCreateInfo, nullptr, &image)) {
    qFatal("can not create image");
  }

  VkMemoryRequirements memoryRequirements;
  m_devFuncs->vkGetImageMemoryRequirements(dev, image, &memoryRequirements);
  VkMemoryAllocateInfo memoryAllocateInfo;
  memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  memoryAllocateInfo.pNext = nullptr;
  uint32_t memProp = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  if (tiling == VK_IMAGE_TILING_LINEAR) {
    memProp = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  }
  memoryAllocateInfo.memoryTypeIndex = getMemoryType(memoryRequirements.memoryTypeBits, memProp);
  memoryAllocateInfo.allocationSize = memoryRequirements.size;
  VkDeviceMemory memory;
  if (m_devFuncs->vkAllocateMemory(dev, &memoryAllocateInfo, nullptr, &memory) != VK_SUCCESS) {
    qFatal("can not allocate memory");
  }
  if (m_devFuncs->vkBindImageMemory(dev, image, memory, 0) != VK_SUCCESS) {
    qFatal("can not bind image and memory");
  }
  VkImageView imageView = VK_NULL_HANDLE;
  if (!imageOnly) {
    VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
    if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
      aspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    VkImageViewCreateInfo imageViewCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .image = image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = format,
            .components = {
                    .r = VK_COMPONENT_SWIZZLE_R,
                    .g = VK_COMPONENT_SWIZZLE_G,
                    .b = VK_COMPONENT_SWIZZLE_B,
                    .a = VK_COMPONENT_SWIZZLE_A
            },
            .subresourceRange = {
                    .aspectMask = aspectFlags,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1
            }
    };
    if (m_devFuncs->vkCreateImageView(dev, &imageViewCreateInfo, nullptr, &imageView) != VK_SUCCESS) {
      qFatal("can not create image view");
    }
  }
  return {-1, memory, image, imageView};
}

void VulkanRenderer::writeLinearImage(const QImage &img, VkImage image, VkDeviceMemory memory) {
  VkImageSubresource subres = {
          .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
          .mipLevel = 0, // mip level
          .arrayLayer = 0
  };
  VkSubresourceLayout layout;
  m_devFuncs->vkGetImageSubresourceLayout(dev, image, &subres, &layout);

  uchar *p;
  VkResult err = m_devFuncs->vkMapMemory(dev, memory, layout.offset, layout.size, 0, reinterpret_cast<void **>(&p));
  if (err != VK_SUCCESS) {
    qWarning("Failed to map memory for linear image: %d", err);
  }

  for (int y = 0; y < img.height(); ++y) {
    const uchar *line = img.constScanLine(y);
    memcpy(p, line, img.width() * 4);
    p += layout.rowPitch;
  }

  m_devFuncs->vkUnmapMemory(dev, memory);
}

void VulkanRenderer::readLinearImage(const QImage &img, VkImage image, VkDeviceMemory memory) {
  VkImageSubresource subres = {
          .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
          .mipLevel = 0, // mip level
          .arrayLayer = 0
  };
  VkSubresourceLayout layout;
  m_devFuncs->vkGetImageSubresourceLayout(dev, image, &subres, &layout);

  uchar *p;
  VkResult err = m_devFuncs->vkMapMemory(dev, memory, layout.offset, layout.size, 0, reinterpret_cast<void **>(&p));
  if (err != VK_SUCCESS) {
    qWarning("Failed to map memory for linear image: %d", err);
  }

  for (int y = 0; y < img.height(); ++y) {
    uchar *line = (uchar *) img.scanLine(y);
    memcpy(line, p, img.width() * 4);
    p += layout.rowPitch;
  }

  m_devFuncs->vkUnmapMemory(dev, memory);
}

void VulkanRenderer::uploadImage(VkCommandBuffer cb, VkImage dstImage, VkImage srcImage, const QSize &sz) {
  VkImageMemoryBarrier barrier = {
          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
          .pNext = nullptr,
          .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
          .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
          .oldLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
          .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          .srcQueueFamilyIndex = m_window->graphicsQueueFamilyIndex(),
          .dstQueueFamilyIndex = m_window->graphicsQueueFamilyIndex(),
          .image = srcImage,
          .subresourceRange = {
                  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                  .baseMipLevel = 0,
                  .levelCount = 1,
                  .baseArrayLayer = 0,
                  .layerCount = 1
          }
  };

  m_devFuncs->vkCmdPipelineBarrier(cb,
                                   VK_PIPELINE_STAGE_HOST_BIT,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   0, 0, nullptr, 0, nullptr,
                                   1, &barrier);

  barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.srcAccessMask = 0;
  barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.image = dstImage;
  m_devFuncs->vkCmdPipelineBarrier(cb,
                                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   0, 0, nullptr, 0, nullptr,
                                   1, &barrier);

  VkImageCopy copyInfo = {
          .srcSubresource = {
                  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                  .mipLevel = 0,
                  .baseArrayLayer = 0,
                  .layerCount = 1
          },
          .srcOffset = {
                  .x = 0,
                  .y = 0,
                  .z = 0
          },
          .dstSubresource = {
                  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                  .mipLevel = 0,
                  .baseArrayLayer = 0,
                  .layerCount = 1
          },
          .dstOffset = {
                  .x = 0,
                  .y = 0,
                  .z = 0
          },
          .extent = {
                  .width = static_cast<uint32_t>(sz.width()),
                  .height = static_cast<uint32_t>(sz.height()),
                  .depth = 1
          }
  };
  m_devFuncs->vkCmdCopyImage(cb, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyInfo);

  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  barrier.image = dstImage;
  m_devFuncs->vkCmdPipelineBarrier(cb,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                   0, 0, nullptr, 0, nullptr,
                                   1, &barrier);
}

void VulkanRenderer::downloadImage(VkCommandBuffer cb, VkImage dstImage, VkImage srcImage, const QSize &sz) {
  VkImageMemoryBarrier barrier = {
          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
          .pNext = nullptr,
          .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
          .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
          .oldLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
          .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          .srcQueueFamilyIndex = m_window->graphicsQueueFamilyIndex(),
          .dstQueueFamilyIndex = m_window->graphicsQueueFamilyIndex(),
          .image = srcImage,
          .subresourceRange = {
                  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                  .baseMipLevel = 0,
                  .levelCount = 1,
                  .baseArrayLayer = 0,
                  .layerCount = 1
          }
  };
//
//  m_devFuncs->vkCmdPipelineBarrier(cb,
//                                   VK_PIPELINE_STAGE_HOST_BIT,
//                                   VK_PIPELINE_STAGE_TRANSFER_BIT,
//                                   0, 0, nullptr, 0, nullptr,
//                                   1, &barrier);

  barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.srcAccessMask = 0;
  barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.image = dstImage;
  m_devFuncs->vkCmdPipelineBarrier(cb,
                                   VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   0, 0, nullptr, 0, nullptr,
                                   1, &barrier);

  VkImageCopy copyInfo = {
          .srcSubresource = {
                  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                  .mipLevel = 0,
                  .baseArrayLayer = 0,
                  .layerCount = 1
          },
          .srcOffset = {
                  .x = 0,
                  .y = 0,
                  .z = 0
          },
          .dstSubresource = {
                  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                  .mipLevel = 0,
                  .baseArrayLayer = 0,
                  .layerCount = 1
          },
          .dstOffset = {
                  .x = 0,
                  .y = 0,
                  .z = 0
          },
          .extent = {
                  .width = static_cast<uint32_t>(sz.width()),
                  .height = static_cast<uint32_t>(sz.height()),
                  .depth = 1
          }
  };
  m_devFuncs->vkCmdCopyImage(cb, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyInfo);

  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
  barrier.image = dstImage;
  m_devFuncs->vkCmdPipelineBarrier(cb,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   0, 0, nullptr, 0, nullptr,
                                   1, &barrier);
}

void VulkanRenderer::beginStageCommandBuffer() {
  m_devFuncs->vkResetCommandBuffer(stageCB, 0);
  m_devFuncs->vkResetFences(dev, 1, &fence);
  VkCommandBufferBeginInfo commandBufferBeginInfo = {
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
          .pNext = nullptr,
          .flags = 0,
          .pInheritanceInfo = nullptr
  };
  if (m_devFuncs->vkBeginCommandBuffer(stageCB, &commandBufferBeginInfo) != VK_SUCCESS) {
    qFatal("can not begine command buffer");
  }
}

void VulkanRenderer::flushStageCommandBuffer() {
  m_devFuncs->vkEndCommandBuffer(stageCB);

  VkSubmitInfo submitInfo = {
          .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
          .pNext = nullptr,
          .waitSemaphoreCount = 0,
          .pWaitSemaphores = nullptr,
          .pWaitDstStageMask = nullptr,
          .commandBufferCount = 1,
          .pCommandBuffers = &stageCB,
          .signalSemaphoreCount = 0,
          .pSignalSemaphores = nullptr
  };
  if (m_devFuncs->vkQueueSubmit(m_window->graphicsQueue(), 1, &submitInfo, fence) != VK_SUCCESS) {
    qFatal("can not submit command");
  }
  VkResult res = m_devFuncs->vkWaitForFences(dev, 1, &fence, false, UINT64_MAX);
  if (res != VK_SUCCESS) {
    qDebug() << "wait for fence failed" << res;
    qFatal("wait for fence failed");
  }
}

void VulkanRenderer::createSelAttachment() {
  const QSize sz = m_window->swapChainImageSize();

  objSelectPass.pixeBuf = createBuffer(4 * sizeof(float), VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
  objSelectPass.color = createImage(sz, select_image_format, VK_IMAGE_TILING_OPTIMAL,
                                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                    VK_IMAGE_LAYOUT_UNDEFINED, false);
  objSelectPass.depth = createImage(sz, getSupportedDepthFormat(), VK_IMAGE_TILING_OPTIMAL,
                                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_LAYOUT_UNDEFINED, false);
  VkImageView attachments[2] = {
          objSelectPass.color.imageView,
          objSelectPass.depth.imageView
  };

  VkFramebufferCreateInfo framebufferCreateInfo = {
          .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
          .pNext = nullptr,
          .flags = 0,
          .renderPass = objSelectPass.renderPass,
          .attachmentCount = ARRAY_SIZE(attachments),
          .pAttachments = attachments,
          .width = static_cast<uint32_t>(sz.width()),
          .height = static_cast<uint32_t>(sz.height()),
          .layers = 1,
  };
  if (m_devFuncs->vkCreateFramebuffer(dev, &framebufferCreateInfo, nullptr, &objSelectPass.framebuffer) != VK_SUCCESS) {
    qFatal("can not create frame buffer");
  }
}

void VulkanRenderer::readPixel(VkCommandBuffer cb, VkBuffer buffer, VkImage image, const QPoint &pos) {
  VkBufferImageCopy bufferImageCopy;
  bufferImageCopy.bufferOffset = 0;
  bufferImageCopy.bufferImageHeight = 0;
  bufferImageCopy.bufferRowLength = 0;
  bufferImageCopy.imageExtent.width = 1;
  bufferImageCopy.imageExtent.height = 1;
  bufferImageCopy.imageExtent.depth = 1;
  bufferImageCopy.imageOffset.x = pos.x();
  bufferImageCopy.imageOffset.y = pos.y();
  bufferImageCopy.imageOffset.z = 0;
  bufferImageCopy.imageSubresource.baseArrayLayer = 0;
  bufferImageCopy.imageSubresource.layerCount = 1;
  bufferImageCopy.imageSubresource.mipLevel = 0;
  bufferImageCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  m_devFuncs->vkCmdCopyImageToBuffer(cb, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1,
                                     &bufferImageCopy);
}

void VulkanRenderer::modifySelKpColor()
{
  if(curr_track_id == std::numeric_limits<Track_ID_T>::max()){
    return;
  }
  const auto &tr = m_graphModel->tracks.at(curr_track_id);

  for(int i = 0 ; i < tr.images.size(); i++){
    if(texIdMap.count(tr.images[i])){
      uint32_t tex_id = texIdMap.at(tr.images[i]);
      VkDeviceSize kp_offset = indirectDrawCmds[tex_id].firstVertex;
      vas[kp_offset + tr.kps[i]].rgba = 0xFF0000FFu;
      kpMaterial.vertStagePtr[kp_offset + tr.kps[i]].rgba = 0xFF0000FFu;
    }
  }
}

void VulkanRenderer::createStageCommandBuffer() {
  VkFenceCreateInfo fenceCreateInfo = {
          .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
          .pNext = nullptr,
          .flags = VK_FENCE_CREATE_SIGNALED_BIT
  };
  if (m_devFuncs->vkCreateFence(dev, &fenceCreateInfo, nullptr, &fence) != VK_SUCCESS) {
    qFatal("can not create fence");
  }

  VkCommandPoolCreateInfo commandPoolCreateInfo = {
          .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
          .pNext = nullptr,
          .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
          .queueFamilyIndex = m_window->graphicsQueueFamilyIndex()
  };
  if (m_devFuncs->vkCreateCommandPool(dev, &commandPoolCreateInfo, nullptr, &stagePool) != VK_SUCCESS) {
    qFatal("can not create command pool");
  }

  VkCommandBufferAllocateInfo commandBufferAllocateInfo = {
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
          .pNext = nullptr,
          .commandPool = stagePool,
          .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
          .commandBufferCount = 1
  };
  if (m_devFuncs->vkAllocateCommandBuffers(dev, &commandBufferAllocateInfo, &stageCB) != VK_SUCCESS) {
    qFatal("can not allocate command buffer");
  }
}

void VulkanRenderer::createBuffers() {
  instBuf = createBuffer(MAX_IMAGE_NUM * sizeof(textureExtraInfo), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
  if (m_devFuncs->vkMapMemory(dev, instBuf.memory, 0, MAX_IMAGE_NUM * sizeof(textureExtraInfo), 0,
                              reinterpret_cast<void **>(&instBufPtr)) != VK_SUCCESS) {
    qFatal("can not map inst buf");
  }

  BufferData bd = createBuffer(sizeof(quadVert), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                               false);
  BufferData stageBd = createBuffer(sizeof(quadVert), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
  writeBuffer(stageBd.buffer, stageBd.memory, (void *) quadVert, 0, sizeof(quadVert));
  beginStageCommandBuffer();
  copyBuffer(bd.buffer, stageBd.buffer, sizeof(quadVert));
  flushStageCommandBuffer();
  m_devFuncs->vkDestroyBuffer(dev, stageBd.buffer, nullptr);
  m_devFuncs->vkFreeMemory(dev, stageBd.memory, nullptr);
  imageMaterial.vert = bd;

  kpMaterial.vert = createBuffer(MAX_KEYPOINT_NUM * sizeof(VertexAttribute),
                                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                 false);
  kpMaterial.vertStage = createBuffer(MAX_KEYPOINT_NUM * sizeof(VertexAttribute), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
  kpMaterial.indirectDrawBuf = createBuffer(MAX_IMAGE_NUM * sizeof(VkDrawIndirectCommand),
                                            VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                            false);
  kpMaterial.indirectDrawBufStage = createBuffer(MAX_IMAGE_NUM * sizeof(VkDrawIndirectCommand),
                                                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
  if (m_devFuncs->vkMapMemory(dev, kpMaterial.vertStage.memory, 0, MAX_KEYPOINT_NUM * sizeof(VertexAttribute), 0, reinterpret_cast<void **>(&kpMaterial.vertStagePtr)) != VK_SUCCESS) {
    qFatal("can not map memory");
  }

  lineMaterial.vertStage = createBuffer(500 * sizeof(LineInfo), VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
  lineMaterial.vert = createBuffer(500 * sizeof(LineInfo),
                                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, false);

}

void VulkanRenderer::createSampler() {
  VkSamplerCreateInfo samplerCreateInfo;
  samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerCreateInfo.pNext = nullptr;
  samplerCreateInfo.flags = 0;
  samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
  samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
  samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  samplerCreateInfo.mipLodBias = 0.0;
  samplerCreateInfo.anisotropyEnable = VK_FALSE;
  samplerCreateInfo.maxAnisotropy = 0.0;
  samplerCreateInfo.compareEnable = VK_FALSE;
  samplerCreateInfo.compareOp = VK_COMPARE_OP_NEVER;
  samplerCreateInfo.minLod = 0;
  samplerCreateInfo.maxLod = 0;
  samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
  samplerCreateInfo.unnormalizedCoordinates = VK_FALSE;
  if (m_devFuncs->vkCreateSampler(dev, &samplerCreateInfo, nullptr, &sampler) != VK_SUCCESS) {
    qFatal("can not create sampler");
  }
}

void VulkanRenderer::createDescriptorSets() {
  VkDescriptorPoolSize descriptorPoolSizes[] = {
          {
                  .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                  .descriptorCount = MAX_IMAGE_NUM
          }
  };
  VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
          .pNext = nullptr,
          .flags = 0,
          .maxSets = 1,
          .poolSizeCount = sizeof(descriptorPoolSizes) / sizeof(descriptorPoolSizes[0]),
          .pPoolSizes = descriptorPoolSizes
  };

  if (m_devFuncs->vkCreateDescriptorPool(dev, &descriptorPoolCreateInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
    qFatal("can not create descriptor pool");
  }

  VkDescriptorSetLayoutBinding descriptorSetLayoutBindings[] = {
          {
                  .binding = 0,
                  .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                  .descriptorCount = MAX_IMAGE_NUM,
                  .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                  .pImmutableSamplers = nullptr
          }
  };
  VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
          .pNext = nullptr,
          .flags = 0,
          .bindingCount = ARRAY_SIZE(descriptorSetLayoutBindings),
          .pBindings = descriptorSetLayoutBindings
  };
  if (m_devFuncs->vkCreateDescriptorSetLayout(dev, &descriptorSetLayoutCreateInfo, nullptr,
                                              &imageMaterial.descSetLayout) != VK_SUCCESS) {
    qFatal("can not create descriptor set layout");
  }

  VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
          .pNext = nullptr,
          .descriptorPool = descriptorPool,
          .descriptorSetCount = 1,
          .pSetLayouts = &imageMaterial.descSetLayout
  };
  if (m_devFuncs->vkAllocateDescriptorSets(dev, &descriptorSetAllocateInfo, &imageMaterial.descSet) != VK_SUCCESS) {
    qFatal("can not allocate Descriptor set");
  }
}

void VulkanRenderer::createPipelineLayouts() {
  VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {
          .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
          .pNext = nullptr,
          .flags = 0,
          .initialDataSize = 0,
          .pInitialData = nullptr
  };
  if (m_devFuncs->vkCreatePipelineCache(dev, &pipelineCacheCreateInfo, nullptr, &pipelineCache) != VK_SUCCESS) {
    qFatal("can not create pipeline cache");
  }

  VkPushConstantRange imagePushConstantRanges[] = {
          {
                  .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                  .offset = 0,
                  .size = sizeof(sceneInfo)
          }
  };
  VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {
          .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
          .pNext = nullptr,
          .flags = 0,
          .setLayoutCount = 1,
          .pSetLayouts = &imageMaterial.descSetLayout,
          .pushConstantRangeCount = ARRAY_SIZE(imagePushConstantRanges),
          .pPushConstantRanges = imagePushConstantRanges
  };
  if (m_devFuncs->vkCreatePipelineLayout(dev, &pipelineLayoutCreateInfo, nullptr, &imageMaterial.pipelineLayout) !=
      VK_SUCCESS) {
    qFatal("can not create pipeline layout");
  }

  pipelineLayoutCreateInfo.setLayoutCount = 0;
  pipelineLayoutCreateInfo.pSetLayouts = nullptr;
  if (m_devFuncs->vkCreatePipelineLayout(dev, &pipelineLayoutCreateInfo, nullptr, &kpMaterial.pipelineLayout) !=
      VK_SUCCESS) {
    qFatal("can not create keypoint pipeline layout");
  }

  if (m_devFuncs->vkCreatePipelineLayout(dev, &pipelineLayoutCreateInfo, nullptr, &lineMaterial.pipelineLayout) !=
      VK_SUCCESS) {
    qFatal("can not create keypoint pipeline layout");
  }
}

void VulkanRenderer::createPipelines() {
  VkShaderModule vertexShader = loadShader(":/glsl/images.vert.spv");
  VkShaderModule fragmentShader = loadShader(":/glsl/images.frag.spv");

  VkPipelineShaderStageCreateInfo shaderStages[] = {
          {
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .pNext = nullptr,
                  .flags = 0,
                  .stage = VK_SHADER_STAGE_VERTEX_BIT,
                  .module = vertexShader,
                  .pName = "main",
                  .pSpecializationInfo = nullptr
          },
          {
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .pNext = nullptr,
                  .flags = 0,
                  .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                  .module = fragmentShader,
                  .pName = "main",
                  .pSpecializationInfo = nullptr
          }

  };

  VkVertexInputBindingDescription vertexInputBindingDescriptions[] = {
          {
                  .binding = 0, // binding
                  .stride = 2 * sizeof(float),
                  .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
          },
          {
                  .binding = 1,
                  .stride = (16 + 1 + 3) * sizeof(float),
                  .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE
          }
  };

  VkVertexInputAttributeDescription vertexInputAttributeDescription[] = {
          { // position
                  .location = 0, // location
                  .binding = 0, // binding
                  .format = VK_FORMAT_R32G32_SFLOAT,
                  .offset =0 // offset
          },
          { // instTranslate
                  .location = 1,
                  .binding = 1,
                  .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                  .offset = 0
          },
          { // instTranslate
                  .location = 2,
                  .binding = 1,
                  .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                  .offset = 4 * sizeof(float)
          },
          { // instTranslate
                  .location = 3,
                  .binding = 1,
                  .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                  .offset = 8 * sizeof(float)
          },
          { // instTranslate
                  .location = 4,
                  .binding = 1,
                  .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                  .offset = 12 * sizeof(float)
          },
          { // instTranslate
                  .location = 5,
                  .binding = 1,
                  .format = VK_FORMAT_R32G32_SFLOAT,
                  .offset = 16 * sizeof(float)
          },
          { // instTranslate
                  .location = 6,
                  .binding = 1,
                  .format = VK_FORMAT_R32_SFLOAT,
                  .offset = 18 * sizeof(float)
          }
  };

  VkPipelineVertexInputStateCreateInfo pipelineVertexInputStateCreateInfo;
  pipelineVertexInputStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  pipelineVertexInputStateCreateInfo.pNext = nullptr;
  pipelineVertexInputStateCreateInfo.flags = 0;
  pipelineVertexInputStateCreateInfo.vertexBindingDescriptionCount = ARRAY_SIZE(vertexInputBindingDescriptions);
  pipelineVertexInputStateCreateInfo.pVertexBindingDescriptions = vertexInputBindingDescriptions;
  pipelineVertexInputStateCreateInfo.vertexAttributeDescriptionCount = ARRAY_SIZE(vertexInputAttributeDescription);
  pipelineVertexInputStateCreateInfo.pVertexAttributeDescriptions = vertexInputAttributeDescription;

  // Create pipeline
  VkPipelineInputAssemblyStateCreateInfo inputAssemblyState;
  inputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssemblyState.pNext = nullptr;
  inputAssemblyState.flags = 0;
  inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  inputAssemblyState.primitiveRestartEnable = VK_FALSE;

  VkPipelineRasterizationStateCreateInfo rasterizationState{};
  rasterizationState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizationState.pNext = nullptr;
  rasterizationState.flags = 0;
  rasterizationState.depthClampEnable = VK_FALSE;
  rasterizationState.rasterizerDiscardEnable = VK_FALSE;
  rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizationState.cullMode = VK_CULL_MODE_NONE;
  rasterizationState.frontFace = VK_FRONT_FACE_CLOCKWISE;
  rasterizationState.depthBiasEnable = VK_FALSE;
  rasterizationState.depthBiasConstantFactor = 0;
  rasterizationState.depthBiasClamp = 0;
  rasterizationState.depthBiasSlopeFactor = 0;
  rasterizationState.lineWidth = 1.0f;

  std::vector<VkPipelineColorBlendAttachmentState> pipelineColorBlendAttachmentStates = {
          {.blendEnable = VK_FALSE, .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT}
  };

  VkPipelineColorBlendStateCreateInfo colorBlendState;
  colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlendState.pNext = nullptr;
  colorBlendState.flags = 0;
  colorBlendState.blendConstants[0] = 0;
  colorBlendState.blendConstants[1] = 0;
  colorBlendState.blendConstants[2] = 0;
  colorBlendState.blendConstants[3] = 0;
  colorBlendState.pAttachments = pipelineColorBlendAttachmentStates.data();
  colorBlendState.attachmentCount = pipelineColorBlendAttachmentStates.size();
  colorBlendState.logicOp = VK_LOGIC_OP_CLEAR;
  colorBlendState.logicOpEnable = VK_FALSE;

  VkPipelineDepthStencilStateCreateInfo depthStencilState{};
  depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencilState.depthTestEnable = VK_TRUE;
  depthStencilState.depthWriteEnable = VK_TRUE;
  depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  depthStencilState.back.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

  VkPipelineViewportStateCreateInfo viewportState;
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.pNext = nullptr;
  viewportState.flags = 0;
  viewportState.pViewports = nullptr;
  viewportState.viewportCount = 1;
  viewportState.pScissors = nullptr;
  viewportState.scissorCount = 1;

  VkPipelineMultisampleStateCreateInfo multisampleState;
  multisampleState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampleState.pNext = nullptr;
  multisampleState.flags = 0;
  multisampleState.alphaToCoverageEnable = VK_FALSE;
  multisampleState.alphaToOneEnable = VK_FALSE;
  multisampleState.sampleShadingEnable = VK_FALSE;
  multisampleState.minSampleShading = VK_NULL_HANDLE;
  multisampleState.pSampleMask = nullptr;
  multisampleState.rasterizationSamples = m_window->sampleCountFlagBits();

  VkDynamicState dynamicStateEnables[] = {
          VK_DYNAMIC_STATE_VIEWPORT,
          VK_DYNAMIC_STATE_SCISSOR
  };
  VkPipelineDynamicStateCreateInfo pipelineDynamicStateCreateInfo{};
  pipelineDynamicStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  pipelineDynamicStateCreateInfo.pDynamicStates = dynamicStateEnables;
  pipelineDynamicStateCreateInfo.dynamicStateCount = ARRAY_SIZE(dynamicStateEnables);
  pipelineDynamicStateCreateInfo.flags = 0;

  VkGraphicsPipelineCreateInfo graphicsPipelineCreateInfo;
  graphicsPipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  graphicsPipelineCreateInfo.pNext = nullptr;
  graphicsPipelineCreateInfo.flags = 0;
  graphicsPipelineCreateInfo.pStages = shaderStages;
  graphicsPipelineCreateInfo.stageCount = ARRAY_SIZE(shaderStages);
  graphicsPipelineCreateInfo.pVertexInputState = &pipelineVertexInputStateCreateInfo;
  graphicsPipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
  graphicsPipelineCreateInfo.pTessellationState = nullptr;
  graphicsPipelineCreateInfo.pViewportState = &viewportState;
  graphicsPipelineCreateInfo.pRasterizationState = &rasterizationState;
  graphicsPipelineCreateInfo.pMultisampleState = &multisampleState;
  graphicsPipelineCreateInfo.pDepthStencilState = &depthStencilState;
  graphicsPipelineCreateInfo.pColorBlendState = &colorBlendState;
  graphicsPipelineCreateInfo.pDynamicState = &pipelineDynamicStateCreateInfo;
  graphicsPipelineCreateInfo.layout = imageMaterial.pipelineLayout;
  graphicsPipelineCreateInfo.renderPass = m_window->defaultRenderPass();
  graphicsPipelineCreateInfo.subpass = VK_NULL_HANDLE;
  graphicsPipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
  graphicsPipelineCreateInfo.basePipelineIndex = 0;

  if (m_devFuncs->vkCreateGraphicsPipelines(dev, pipelineCache, 1, &graphicsPipelineCreateInfo, nullptr,
                                            &imageMaterial.pipeline) != VK_SUCCESS) {
    qFatal("can not create image pipeline");
  }

  m_devFuncs->vkDestroyShaderModule(dev, vertexShader, nullptr);
  m_devFuncs->vkDestroyShaderModule(dev, fragmentShader, nullptr);

  auto vertexShader_sel = loadShader(":/glsl/images_sel.vert.spv");
  auto fragmentShader_sel = loadShader(":/glsl/images_sel.frag.spv");

  shaderStages[0].module = vertexShader_sel;
  shaderStages[1].module = fragmentShader_sel;
  multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  graphicsPipelineCreateInfo.renderPass = objSelectPass.renderPass;
  if (m_devFuncs->vkCreateGraphicsPipelines(dev, pipelineCache, 1, &graphicsPipelineCreateInfo, nullptr,
                                            &imageMaterial.pipeline_sel) != VK_SUCCESS) {
    qFatal("can not create image pipeline");
  }
  m_devFuncs->vkDestroyShaderModule(dev, vertexShader_sel, nullptr);
  m_devFuncs->vkDestroyShaderModule(dev, fragmentShader_sel, nullptr);

  auto keyPointVertexShader = loadShader(":/glsl/imageKeypoints.vert.spv");
  auto keyPointFragmentShader = loadShader(":/glsl/imageKeypoints.frag.spv");

  shaderStages[0].module = keyPointVertexShader;
  shaderStages[1].module = keyPointFragmentShader;

  VkVertexInputBindingDescription kpVertexInputBindingDescriptions[] = {
          {
                  .binding = 0, // binding
                  .stride = sizeof(VertexAttribute),
                  .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
          },
          {
                  .binding = 1,
                  .stride = (16 + 1 + 3) * sizeof(float),
                  .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE
          }
  };

  VkVertexInputAttributeDescription kpVertexInputAttributeDescription[] = {
          { // position
                  .location = 0, // location
                  .binding = 0, // binding
                  .format = VK_FORMAT_R32G32_SFLOAT,
                  .offset =0 // offset
          },
          { // position
                  .location = 1, // location
                  .binding = 0, // binding
                  .format = VK_FORMAT_R8G8B8A8_UNORM,
                  .offset = offsetof(VertexAttribute, rgba) // offset
          },
          { // instTranslate
                  .location = 2,
                  .binding = 1,
                  .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                  .offset = 0
          },
          { // instTranslate
                  .location = 3,
                  .binding = 1,
                  .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                  .offset = 4 * sizeof(float)
          },
          { // instTranslate
                  .location = 4,
                  .binding = 1,
                  .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                  .offset = 8 * sizeof(float)
          },
          { // instTranslate
                  .location = 5,
                  .binding = 1,
                  .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                  .offset = 12 * sizeof(float)
          },
          { // instTranslate
                  .location = 6,
                  .binding = 1,
                  .format = VK_FORMAT_R32G32_SFLOAT,
                  .offset = 16 * sizeof(float)
          },
          { // instTranslate
                  .location = 7,
                  .binding = 1,
                  .format = VK_FORMAT_R32_SFLOAT,
                  .offset = 18 * sizeof(float)
          }
  };

  pipelineVertexInputStateCreateInfo.vertexBindingDescriptionCount = ARRAY_SIZE(kpVertexInputBindingDescriptions);
  pipelineVertexInputStateCreateInfo.pVertexBindingDescriptions = kpVertexInputBindingDescriptions;
  pipelineVertexInputStateCreateInfo.vertexAttributeDescriptionCount = ARRAY_SIZE(kpVertexInputAttributeDescription);
  pipelineVertexInputStateCreateInfo.pVertexAttributeDescriptions = kpVertexInputAttributeDescription;

  inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
  multisampleState.rasterizationSamples = m_window->sampleCountFlagBits();
  graphicsPipelineCreateInfo.renderPass = m_window->defaultRenderPass();
  graphicsPipelineCreateInfo.layout = kpMaterial.pipelineLayout;
  if (m_devFuncs->vkCreateGraphicsPipelines(dev, pipelineCache, 1, &graphicsPipelineCreateInfo, nullptr,
                                            &kpMaterial.pipeline) != VK_SUCCESS) {
    qFatal("can not create keypoint pipeline");
  }

  m_devFuncs->vkDestroyShaderModule(dev, keyPointVertexShader, nullptr);
  m_devFuncs->vkDestroyShaderModule(dev, keyPointFragmentShader, nullptr);

  auto keyPointVertexShader_sel = loadShader(":/glsl/imageKeypoints_sel.vert.spv");
  auto keyPointFragmentShader_sel = loadShader(":/glsl/imageKeypoints_sel.frag.spv");

  shaderStages[0].module = keyPointVertexShader_sel;
  shaderStages[1].module = keyPointFragmentShader_sel;

  pipelineColorBlendAttachmentStates[0].colorWriteMask = VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  graphicsPipelineCreateInfo.renderPass = objSelectPass.renderPass;
  if (m_devFuncs->vkCreateGraphicsPipelines(dev, pipelineCache, 1, &graphicsPipelineCreateInfo, nullptr,
                                            &kpMaterial.pipeline_sel) != VK_SUCCESS) {
    qFatal("can not create keypoint pipeline");
  }
  m_devFuncs->vkDestroyShaderModule(dev, keyPointVertexShader_sel, nullptr);
  m_devFuncs->vkDestroyShaderModule(dev, keyPointFragmentShader_sel, nullptr);

  auto lineVertexShader = loadShader(":/glsl/line.vert.spv");
  auto lineFragmentShader = loadShader(":/glsl/line.frag.spv");

  shaderStages[0].module = lineVertexShader;
  shaderStages[1].module = lineFragmentShader;

  VkVertexInputBindingDescription lineVertexInputBindingDescriptions[] = {
          {
                  .binding = 0, // binding
                  .stride = 24 * sizeof(float),
                  .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
          }
  };

  VkVertexInputAttributeDescription lineVertexInputAttributeDescription[] = {
          { // instTranslate
                  .location = 0,
                  .binding = 0,
                  .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                  .offset = 0
          },
          { // instTranslate
                  .location = 1,
                  .binding = 0,
                  .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                  .offset = 4 * sizeof(float)
          },
          { // instTranslate
                  .location = 2,
                  .binding = 0,
                  .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                  .offset = 8 * sizeof(float)
          },
          { // instTranslate
                  .location = 3,
                  .binding = 0,
                  .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                  .offset = 12 * sizeof(float)
          },
          { // instTranslate
                  .location = 4,
                  .binding = 0,
                  .format = VK_FORMAT_R32G32B32_SFLOAT,
                  .offset = 16 * sizeof(float)
          },
          { // instTranslate
                  .location = 5,
                  .binding = 0,
                  .format = VK_FORMAT_R32_SFLOAT,
                  .offset = 19 * sizeof(float)
          },
          { // instTranslate
                  .location = 6,
                  .binding = 0,
                  .format = VK_FORMAT_R32G32_SFLOAT,
                  .offset = 20 * sizeof(float)
          },
          { // instTranslate
                  .location = 7,
                  .binding = 0,
                  .format = VK_FORMAT_R32G32_SFLOAT,
                  .offset = 22 * sizeof(float)
          }
  };

  pipelineVertexInputStateCreateInfo.vertexBindingDescriptionCount = ARRAY_SIZE(lineVertexInputBindingDescriptions);
  pipelineVertexInputStateCreateInfo.pVertexBindingDescriptions = lineVertexInputBindingDescriptions;
  pipelineVertexInputStateCreateInfo.vertexAttributeDescriptionCount = ARRAY_SIZE(lineVertexInputAttributeDescription);
  pipelineVertexInputStateCreateInfo.pVertexAttributeDescriptions = lineVertexInputAttributeDescription;

  inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
  multisampleState.rasterizationSamples = m_window->sampleCountFlagBits();
  graphicsPipelineCreateInfo.renderPass = m_window->defaultRenderPass();
  graphicsPipelineCreateInfo.layout = lineMaterial.pipelineLayout;
  if (m_devFuncs->vkCreateGraphicsPipelines(dev, pipelineCache, 1, &graphicsPipelineCreateInfo, nullptr,
                                            &lineMaterial.pipeline) != VK_SUCCESS) {
    qFatal("can not create keypoint pipeline");
  }

  m_devFuncs->vkDestroyShaderModule(dev, lineVertexShader, nullptr);
  m_devFuncs->vkDestroyShaderModule(dev, lineFragmentShader, nullptr);
}

void VulkanRenderer::createSelRenderPass() {
  VkAttachmentDescription attachmentDescriptions[] = {
          {
                  .flags = 0,
                  .format = select_image_format,
                  .samples = VK_SAMPLE_COUNT_1_BIT,
                  .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                  .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                  .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                  .stencilStoreOp  = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                  .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                  .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
          },
          {
                  .flags = 0,
                  .format = getSupportedDepthFormat(),
                  .samples = VK_SAMPLE_COUNT_1_BIT,
                  .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                  .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                  .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                  .stencilStoreOp  = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                  .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                  .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
          }
  };
  VkAttachmentReference colorReferences[] = {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
  VkAttachmentReference depthReference = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

  VkSubpassDescription subpassDescriptions[] = {
          {
                  .flags = 0,
                  .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                  .inputAttachmentCount = 0,
                  .pInputAttachments = nullptr,
                  .colorAttachmentCount = ARRAY_SIZE(colorReferences),
                  .pColorAttachments = colorReferences,
                  .pResolveAttachments = nullptr,
                  .pDepthStencilAttachment = &depthReference,
                  .preserveAttachmentCount = 0,
                  .pPreserveAttachments = nullptr
          }
  };

  VkSubpassDependency subpassDependency[] = {
          {
                  .srcSubpass = VK_SUBPASS_EXTERNAL,
                  .dstSubpass = 0,
                  .srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
                  .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                  .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                  .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT
          },
          {
                  .srcSubpass = 0,
                  .dstSubpass = VK_SUBPASS_EXTERNAL,
                  .srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                  .dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
                  .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                  .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                  .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT
          }
  };
  VkRenderPassCreateInfo renderPassCreateInfo = {
          .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
          .pNext = nullptr,
          .flags = 0,
          .attachmentCount = ARRAY_SIZE(attachmentDescriptions),
          .pAttachments = attachmentDescriptions,
          .subpassCount = ARRAY_SIZE(subpassDescriptions),
          .pSubpasses = subpassDescriptions,
          .dependencyCount = ARRAY_SIZE(subpassDependency),
          .pDependencies = subpassDependency
  };
  if (m_devFuncs->vkCreateRenderPass(dev, &renderPassCreateInfo, nullptr, &objSelectPass.renderPass) != VK_SUCCESS) {
    qFatal("can not create render pass");
  }
}

void VulkanRenderer::selectObject(const QPoint &pos) {
  const QSize sz = m_window->swapChainImageSize();

  const union {
    uint32_t i;
    float f;
  } select_default = {.i = 0xFFFFFFFFu};
  VkClearValue clearValues[] = {
          {
                  .color = {.float32 = {select_default.f, select_default.f, select_default.f, select_default.f}}
          },
          {
                  .depthStencil = {1., 0}
          }
  };
  beginStageCommandBuffer();
  VkRenderPassBeginInfo renderPassBeginInfo = {
          .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
          .pNext = nullptr,
          .renderPass = objSelectPass.renderPass,
          .framebuffer = objSelectPass.framebuffer,
          .renderArea = {
                  .offset = {
                          .x = 0,
                          .y = 0
                  },
                  .extent = {
                          .width = static_cast<uint32_t>(sz.width()),
                          .height = static_cast<uint32_t>(sz.height())
                  }},
          .clearValueCount = sizeof(clearValues) / sizeof(clearValues[0]),
          .pClearValues = clearValues
  };

  m_devFuncs->vkCmdBeginRenderPass(stageCB, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

  if (!texIdMap.empty()) {
    // do not update resource
//    updateResources();

    VkViewport viewport;
    viewport.x = viewport.y = 0;
    viewport.width = sz.width();
    viewport.height = sz.height();
    viewport.minDepth = 0;
    viewport.maxDepth = 1;
    m_devFuncs->vkCmdSetViewport(stageCB, 0, 1, &viewport);

    VkRect2D scissor;
    scissor.offset.x = scissor.offset.y = 0;
    scissor.extent.width = viewport.width;
    scissor.extent.height = viewport.height;
    m_devFuncs->vkCmdSetScissor(stageCB, 0, 1, &scissor);

    m_devFuncs->vkCmdBindPipeline(stageCB, VK_PIPELINE_BIND_POINT_GRAPHICS, imageMaterial.pipeline_sel);
    VkBuffer imageVertBuffs[] = {imageMaterial.vert.buffer, instBuf.buffer};
    VkDeviceSize imageVertOffsets[] = {0, 0};
    m_devFuncs->vkCmdBindVertexBuffers(stageCB, 0, ARRAY_SIZE(imageVertBuffs), imageVertBuffs, imageVertOffsets);
// do not use descriptor set
//        m_devFuncs->vkCmdBindDescriptorSets(stageCB, VK_PIPELINE_BIND_POINT_GRAPHICS, imageMaterial.pipelineLayout, 0, 1, &imageMaterial.descSet, 0,
//                                        nullptr);
    m_devFuncs->vkCmdPushConstants(stageCB, imageMaterial.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                                   sizeof(sceneInfo), &sceneInfo);
    m_devFuncs->vkCmdDraw(stageCB, 4, texIdMap.size(), 0, 0);

    m_devFuncs->vkCmdBindPipeline(stageCB, VK_PIPELINE_BIND_POINT_GRAPHICS, kpMaterial.pipeline_sel);
    /* share inst buf, do not update, here we only change binding 0*/
    VkDeviceSize kpVertOffsets = 0;
    m_devFuncs->vkCmdBindVertexBuffers(stageCB, 0, 1, &kpMaterial.vert.buffer, &kpVertOffsets);
//    m_devFuncs->vkCmdPushConstants(cb, kpMaterial.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(sceneInfo), &sceneInfo.proj);
    m_devFuncs->vkCmdDrawIndirect(stageCB, kpMaterial.indirectDrawBuf.buffer, 0, texDatas.size(),
                                  sizeof(VkDrawIndirectCommand));
  }

  m_devFuncs->vkCmdEndRenderPass(stageCB);
  readPixel(stageCB, objSelectPass.pixeBuf.buffer, objSelectPass.color.image, pos);
  flushStageCommandBuffer();

  uint8_t *p;
  if (m_devFuncs->vkMapMemory(dev, objSelectPass.pixeBuf.memory, 0, 4 * sizeof(float), 0,
                              reinterpret_cast<void **>(&p)) != VK_SUCCESS) {
    qFatal("can not map pixel buffer");
  }
  memcpy(&selectInfo, p, 4 * sizeof(float));
  m_devFuncs->vkUnmapMemory(dev, objSelectPass.pixeBuf.memory);
  if (selectInfo.tex_id == UINT32_MAX) {
    selectInfo.image_id = UINT32_MAX;
    return;
  }
  selectInfo.image_id = texDatas[selectInfo.tex_id].image_id;
  uint32_t image_kp_id = selectInfo.kp_id;
  for (const auto &it: texDatas) {
    const auto curr_size = m_graphModel->imageInfos.at(it.image_id).keyPoints.size();
    if (image_kp_id < curr_size) {
      break;
    } else {
      image_kp_id = image_kp_id - curr_size;
    }
  }
  selectInfo.image_kp_id = image_kp_id;
}

void VulkanRenderer::mousePressEvent(QMouseEvent *e) {
  assert(m_window->devicePixelRatio() == 1.0f);
  ulong timeDelta = e->timestamp() - mouseLastTime;
  mouseLastTime = e->timestamp();
  mouseLastPos.x() = e->localPos().x();
  mouseLastPos.y() = e->localPos().y();

  if (e->buttons() & Qt::LeftButton) {
    if (timeDelta < 300) {
      if ((selectInfo.image_id >= 0) && (selectInfo.kp_id == UINT32_MAX)) {
        m_graphModel->appendImageKeyPoint(selectInfo.image_id, selectInfo.uv);
      }
      return;
    }
    selectObject(e->pos());
    qDebug() << selectInfo.image_id << selectInfo.kp_id << "(" << selectInfo.uv.x() << ", " << selectInfo.uv.y() << ")";
    if (selectInfo.kp_id != UINT32_MAX) {
      if (myMode == RENDER_MODE_TRACK) {
        if (m_graphModel->addKeypoint2Track(curr_track_id, selectInfo.image_id, selectInfo.image_kp_id)) {
          const auto &imgInfo = m_graphModel->imageInfos.at(selectInfo.image_id);
          const auto &kp = imgInfo.keyPoints.at(selectInfo.image_kp_id);
          const auto &img = imgInfo.data;
          m_trackScene->addKeyPointImage(img, QPointF(kp.pos.x() * img.width() - 0.5, kp.pos.y() * img.height() - 0.5));
        }
      }
    } else if (selectInfo.tex_id != UINT32_MAX) {
      image_min_depth = image_min_depth - image_depth_internal;
      texExtraInfos[selectInfo.tex_id].depth = image_min_depth;
    }
  } else if (e->buttons() & Qt::RightButton) {
    selectObject(e->pos());
    if (selectInfo.kp_id != UINT32_MAX) {
      image_min_depth = image_min_depth - image_depth_internal;
      texExtraInfos[selectInfo.tex_id].depth = image_min_depth;
      actionMenu->exec(e->globalPos());
    } else {
      if (myMode == RENDER_MODE_TRACK) {
        m_window->setCursor(Qt::ArrowCursor);
        myMode = RENDER_MODE_NORMAL;
      }
    }
  }
  e->accept();
  m_window->requestUpdate();
}

void VulkanRenderer::mouseReleaseEvent(QMouseEvent *e) {

}

void VulkanRenderer::mouseMoveEvent(QMouseEvent *e) {
  if (e->buttons() & Qt::LeftButton) {
    if (selectInfo.kp_id != UINT32_MAX) {

    } else if (selectInfo.tex_id != UINT32_MAX) {
      auto dx = 2 * (e->localPos().x() - mouseLastPos.x());
      auto dy = 2 * (e->localPos().y() - mouseLastPos.y());
      Eigen::Vector3f d = sceneInfo.proj.inverse().block(0, 0, 3, 3) * Eigen::Vector3f(dx, dy, 0);
      texExtraInfos[selectInfo.tex_id].mat.block(0, 3, 3, 1) += d;
      mouseLastPos.x() = e->localPos().x();
      mouseLastPos.y() = e->localPos().y();
      e->accept();
      m_window->requestUpdate();
    }
  }
}

void VulkanRenderer::wheelEvent(QWheelEvent *e) {
  QPoint numPixels = e->pixelDelta();
  auto numDegrees = e->angleDelta().y() / 8;

  Q_ASSERT(numPixels.isNull());
  auto numSteps = numDegrees / 15;
  Eigen::DiagonalMatrix<float, 4> scaleMat;
  scaleMat.diagonal() << 0.1 * numSteps + 1., 0.1 * numSteps + 1., 1., 1.;
  sceneInfo.proj = scaleMat * sceneInfo.proj;
  e->accept();

  m_window->requestUpdate();
}

void VulkanRenderer::setModel(ImageGraphModel *model) {
  if (m_graphModel) {
    Q_ASSERT(m_graphModel == model);
  } else {
    Q_ASSERT(m_graphModel == nullptr);
    m_graphModel = model;
    connect(m_graphModel, &ImageGraphModel::dataChanged, this, &VulkanRenderer::dataChanged);
    connect(m_graphModel, &ImageGraphModel::keyPointsInserted, this, &VulkanRenderer::updateImageKeypoints);
  }
}

void VulkanRenderer::setTrackScene(GraphWidget *graphicsScene) {
  if (m_trackScene) {
    Q_ASSERT(m_trackScene == graphicsScene);
  } else {
    Q_ASSERT(m_trackScene == nullptr);
    m_trackScene = graphicsScene;
  }
}

void
VulkanRenderer::dataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QVector<int> &roles) {
  if (roles.contains(Qt::CheckStateRole)) {
    for (auto i = topLeft.row(); i <= bottomRight.row(); i++) {
      auto &&index = m_graphModel->index(i, 0, QModelIndex());
      if (m_graphModel->data(index, Qt::CheckStateRole).value<Qt::CheckState>() == Qt::Checked) {
        addImage(m_graphModel->data(index, Qt::UserRole + 2).value<Image_ID_T>());
      } else {
        removeImage(m_graphModel->data(index, Qt::UserRole + 2).value<Image_ID_T>());
      }
    }
  }
}

void VulkanRenderer::addImage(int image_id) {
  const auto img_rgba = m_graphModel->imageInfos.at(image_id).data.convertToFormat(QImage::Format_RGBA8888_Premultiplied);

  TextureData &&tex = createImage(img_rgba.size(), VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
                                  VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                  VK_IMAGE_LAYOUT_UNDEFINED, false);

  TextureData &&stageTex = createImage(img_rgba.size(), VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_LINEAR,
                                       VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_LAYOUT_PREINITIALIZED, true);
  writeLinearImage(img_rgba, stageTex.image, stageTex.memory);
  beginStageCommandBuffer();
  uploadImage(stageCB, tex.image, stageTex.image, img_rgba.size());
  flushStageCommandBuffer();

  m_devFuncs->vkDestroyImage(dev, stageTex.image, nullptr);
  m_devFuncs->vkFreeMemory(dev, stageTex.memory, nullptr);

  const auto &imgInfo = m_graphModel->imageInfos.at(image_id);
  const auto image_vert_count = imgInfo.keyPoints.size();

  uint32_t image_vert_offset = vas.size();
  vas.reserve(vas.size() + image_vert_count);
  for (const auto &it:imgInfo.keyPoints) {
    auto &va = vas.emplace_back();
    va.x = it.pos.x();
    va.y = it.pos.y();
    if(it.track_id == std::numeric_limits<Track_ID_T>::max()){
      va.rgba = 0xFFFF00FFu;
    } else {
      va.rgba = 0xFF00FFFFu;
    }
  }

  memcpy(kpMaterial.vertStagePtr + image_vert_offset, vas.data() + image_vert_offset, image_vert_count * sizeof(VertexAttribute));

  VkDrawIndirectCommand  & drawIndirectCommand= indirectDrawCmds.emplace_back();
  drawIndirectCommand.vertexCount = static_cast<uint32_t>(image_vert_count);
  drawIndirectCommand.instanceCount = 1;
  drawIndirectCommand.firstVertex = image_vert_offset;
  drawIndirectCommand.firstInstance = static_cast<uint32_t>(texDatas.size());
  writeBuffer(kpMaterial.indirectDrawBufStage.buffer, kpMaterial.indirectDrawBufStage.memory, &drawIndirectCommand,
              texDatas.size() * sizeof(VkDrawIndirectCommand), sizeof(VkDrawIndirectCommand));

  texIdMap[image_id] = texDatas.size();
  tex.image_id = image_id;
  texDatas.push_back(tex);
  image_min_depth = image_min_depth - image_depth_internal;
  texExtraInfos.push_back(
          {Eigen::Matrix4f::Identity(), static_cast<float>(img_rgba.width()), static_cast<float>(img_rgba.height()),
           image_min_depth});

  vertexChange = true;
  imageChange = true;
  m_window->requestUpdate();
}

void VulkanRenderer::removeImage(int image_id) {
  int tex_id = texIdMap.at(image_id);
  uint32_t image_vert_offset = indirectDrawCmds[tex_id].firstVertex;
  vas.erase(vas.begin() + image_vert_offset, vas.begin() + image_vert_offset + indirectDrawCmds[tex_id].vertexCount);
  memcpy(kpMaterial.vertStagePtr + image_vert_offset, vas.data() + image_vert_offset,
              (vas.size() - image_vert_offset) * sizeof(VertexAttribute));

  for(int i = tex_id ; i < (indirectDrawCmds.size() - 1); i++){
    indirectDrawCmds[i].firstVertex = image_vert_offset;
    indirectDrawCmds[i].vertexCount = indirectDrawCmds[i + 1].vertexCount;
    image_vert_offset += indirectDrawCmds[i].vertexCount;
  }
  indirectDrawCmds.pop_back();

  writeBuffer(kpMaterial.indirectDrawBufStage.buffer, kpMaterial.indirectDrawBufStage.memory, indirectDrawCmds.data() + tex_id,
              tex_id * sizeof(VkDrawIndirectCommand), (indirectDrawCmds.size() - tex_id) * sizeof(VkDrawIndirectCommand));

  tex2remove.push_back(texDatas[tex_id]);
  texDatas.erase(texDatas.begin() + tex_id);
  texExtraInfos.erase(texExtraInfos.begin() + tex_id);

  texIdMap.erase(image_id);
  for (auto &it: texIdMap) {
    if (it.second > tex_id) {
      it.second = it.second - 1;
    }
  }

  vertexChange = true;
  imageChange = true;
  m_window->requestUpdate();
}

void VulkanRenderer::updateImageKeypoints(int image_id) {
  uint32_t tex_id = texIdMap.at(image_id);
  const auto &imgInfo = m_graphModel->imageInfos.at(image_id);
  uint32_t lastKpStart = indirectDrawCmds[tex_id].firstVertex;
  uint32_t lastKpCount = indirectDrawCmds[tex_id].vertexCount;
  uint32_t currKpCount = imgInfo.keyPoints.size();
  indirectDrawCmds[tex_id].vertexCount = currKpCount;
  if(lastKpCount < currKpCount){
    uint32_t changed = currKpCount - lastKpCount;
    for(uint32_t i = tex_id + 1; i < indirectDrawCmds.size(); i++){
      indirectDrawCmds[i].firstVertex += changed;
    }
    vas.insert(vas.begin() + lastKpStart + lastKpCount, changed, VertexAttribute());
  } else if(lastKpCount == currKpCount){

  } else {
    uint32_t changed = lastKpCount - currKpCount;
    for(uint32_t i = tex_id + 1; i < indirectDrawCmds.size(); i++){
      indirectDrawCmds[i].firstVertex -= changed;
    }
    vas.erase(vas.begin() + lastKpStart + lastKpCount - changed, vas.begin() + lastKpStart + lastKpCount);
  }

  uint32_t currKpOffset = lastKpStart;
  for (const auto &it: imgInfo.keyPoints) {
    auto &va = vas[currKpOffset];
    currKpOffset++;
    va.x = it.pos.x();
    va.y = it.pos.y();
    if(it.track_id == std::numeric_limits<Track_ID_T>::max()){
      va.rgba = 0xFFFF00FFu;
    } else {
      va.rgba = 0xFF00FFFFu;
    }
  }
  memcpy(kpMaterial.vertStagePtr + lastKpStart, vas.data() + lastKpStart, (vas.size() - lastKpStart) * sizeof(VertexAttribute));
  writeBuffer(kpMaterial.indirectDrawBufStage.buffer, kpMaterial.indirectDrawBufStage.memory, indirectDrawCmds.data() + tex_id,
              tex_id * sizeof(VkDrawIndirectCommand), (indirectDrawCmds.size() - tex_id) * sizeof(VkDrawIndirectCommand));

  vertexChange = true;
  m_window->requestUpdate();
}

void VulkanRenderer::addTrackForKeypoint() {
//  curr_track_id = m_graphModel->imageInfos.at(selectInfo.image_id).keyPoints.at(selectInfo.image_kp_id).track_id;
  curr_track_id = m_graphModel->getOrCreateTrackForKeypoint(selectInfo.image_id, selectInfo.image_kp_id);
  m_trackScene->clear();
  const auto &track = m_graphModel->tracks.at(curr_track_id);
  for (int i = 0; i < track.images.size(); i++) {
    const auto &imgInfo = m_graphModel->imageInfos.at(track.images[i]);
    const auto &kp = imgInfo.keyPoints.at(track.kps[i]);
    m_trackScene->addKeyPointImage(imgInfo.data, QPointF(kp.pos.x() * imgInfo.data.width() - 0.5, kp.pos.y() * imgInfo.data.height() - 0.5));
  }
  modifySelKpColor();
  vertexChange = true;
  m_window->requestUpdate();

  m_window->setCursor(Qt::PointingHandCursor);
  myMode = RENDER_MODE_TRACK;
}