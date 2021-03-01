//
// Created by lucius on 1/24/21.
//

#include <QTableView>
#include <QDockWidget>
#include <QToolBar>
#include <QFileDialog>
#include <QSortFilterProxyModel>
#include <QHeaderView>
#include "LoadProjectDialog.h"
#include "graphwidget.h"
#include "ImageGraphModel.h"
#include "VulkanWindow.h"
#include "colampParser.h"
#include "MainWindow.h"

MainWindow::MainWindow(VulkanWindow *vulkanWindow)
        : QMainWindow(), m_window(vulkanWindow), m_graphModel(new ImageGraphModel) {
  auto *imageTable = new QTableView;

  imageTable->setModel(m_graphModel);
  imageTable->setShowGrid(false);
  imageTable->setSortingEnabled(true);
  imageTable->sortByColumn(0, Qt::AscendingOrder);
  imageTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  imageTable->setSelectionMode(QAbstractItemView::SingleSelection);
  for (int i = 1; i < imageTable->model()->columnCount(); i++) {
    imageTable->setColumnHidden(i, true);
  }

  auto tableHeader = imageTable->horizontalHeader();
  tableHeader->setStretchLastSection(true);
  tableHeader->setSectionResizeMode(QHeaderView::Interactive);

  auto *dockWidget = new QDockWidget;
  dockWidget->setWidget(imageTable);
  addDockWidget(Qt::RightDockWidgetArea, dockWidget);

  auto *toolBar = addToolBar("file operation");
  auto *loadAction = toolBar->addAction("load");
  connect(loadAction, &QAction::triggered, this, &MainWindow::loadImages);

  auto *trackWidget = new GraphWidget;
  auto *trackDock = new QDockWidget;
  trackDock->setWidget(trackWidget);
  addDockWidget(Qt::BottomDockWidgetArea, trackDock);

  m_window->setModel(m_graphModel);
  m_window->setTrackScene(trackWidget);
  auto *warp = QWidget::createWindowContainer(m_window);
  setCentralWidget(warp);
}

void MainWindow::loadImages() {
  LoadProjectDialog lpd;
  lpd.exec();
  if(lpd.result() == QDialog::Accepted){
    ColmapLoader loader;
    loader.loadFromColmapSparseDir(lpd.getColmapPath().toStdString());

    m_graphModel->appendColmapData(lpd.getImagePath(), loader);
  }
}
