//
// Created by lucius on 1/24/21.
//

#ifndef MATCH_MANUALLY_VULKANWINDOW_H
#define MATCH_MANUALLY_VULKANWINDOW_H

#include <Eigen/Eigen>
#include <QVulkanWindow>

class GraphWidget;

class VulkanRenderer;

class ImageGraphModel;

class VulkanWindow : public QVulkanWindow {
  Q_OBJECT
public:
  QVulkanWindowRenderer *createRenderer() override;

  void setModel(ImageGraphModel *model);
  void setTrackScene(GraphWidget *trackScene);

private:
  void mousePressEvent(QMouseEvent * e) override;
  void mouseReleaseEvent(QMouseEvent *e) override;
  void mouseMoveEvent(QMouseEvent *e) override;
  void wheelEvent(QWheelEvent * e) override;

  VulkanRenderer *m_renderer = nullptr;

  ImageGraphModel *m_graphModel = nullptr;
  GraphWidget *m_trackScene = nullptr;
};


#endif //MATCH_MANUALLY_VULKANWINDOW_H
