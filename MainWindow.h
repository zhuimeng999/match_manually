//
// Created by lucius on 1/24/21.
//

#ifndef MATCH_MANUALLY_MAINWINDOW_H
#define MATCH_MANUALLY_MAINWINDOW_H

#include <QMainWindow>

class VulkanWindow;

class ImageGraphModel;

class MainWindow : public QMainWindow {
Q_OBJECT
public:
  explicit MainWindow(VulkanWindow *vulkanWindow);

public slots:

  void loadImages();

private:
  VulkanWindow *m_window;
  ImageGraphModel *m_graphModel;
};


#endif //MATCH_MANUALLY_MAINWINDOW_H
