//
// Created by lucius on 1/24/21.
//

#include <QModelIndex>
#include "VulkanRenderer.h"
#include "VulkanWindow.h"
#include "ImageGraphModel.h"

QVulkanWindowRenderer *VulkanWindow::createRenderer() {
  m_renderer = new VulkanRenderer(this, m_graphModel, m_trackScene);
  return m_renderer;
}

void VulkanWindow::setModel(ImageGraphModel *model) {
  Q_ASSERT(m_graphModel == nullptr);
  m_graphModel = model;
  if (m_renderer) {
    m_renderer->setModel(model);
  }
}

void VulkanWindow::setTrackScene(GraphWidget *trackScene) {
  Q_ASSERT(m_trackScene == nullptr);
  m_trackScene = trackScene;
  if (m_renderer) {
    m_renderer->setTrackScene(m_trackScene);
  }
}

void VulkanWindow::mousePressEvent(QMouseEvent *e) {
  m_renderer->mousePressEvent(e);
}

void VulkanWindow::mouseReleaseEvent(QMouseEvent *e) {
  m_renderer->mouseReleaseEvent(e);
}

void VulkanWindow::mouseMoveEvent(QMouseEvent *e) {
  m_renderer->mouseMoveEvent(e);
}

void VulkanWindow::wheelEvent(QWheelEvent *e) {
  m_renderer->wheelEvent(e);
}
