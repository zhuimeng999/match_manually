#include <QApplication>
#include <QLoggingCategory>
#include "VulkanWindow.h"
#include "MainWindow.h"

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  QVulkanInstance inst;
  QLoggingCategory::setFilterRules(QStringLiteral("qt.vulkan=true"));
  inst.setLayers(QByteArrayList() << "VK_LAYER_LUNARG_standard_validation");
  if (!inst.create())
    qFatal("Failed to create Vulkan instance: %d", inst.errorCode());

  auto *vulkanWindow = new VulkanWindow;
  vulkanWindow->setVulkanInstance(&inst);

  MainWindow mainWindow(vulkanWindow);
  mainWindow.resize(1080, 720);
  mainWindow.show();

  return QApplication::exec();
}
