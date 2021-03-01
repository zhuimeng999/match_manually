cmake_minimum_required(VERSION 3.19)
project(match_manually)

set(CMAKE_CXX_STANDARD 17)

#set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

add_executable(${PROJECT_NAME} main.cpp
        VulkanRenderer.cpp VulkanRenderer.h
  VulkanWindow.cpp VulkanWindow.h
  MainWindow.cpp MainWindow.h
  ImageGraphModel.cpp ImageGraphModel.h
        graphwidget.cpp graphwidget.h
        colmapParser.cpp colampParser.h
        data/match_manually.qrc MyImageItem.cpp MyImageItem.h LoadProjectDialog.cpp LoadProjectDialog.h)

find_package(Qt5 REQUIRED COMPONENTS Core Widgets)
target_link_libraries(${PROJECT_NAME} PUBLIC Qt::Core Qt::Widgets)

find_package(Eigen3 REQUIRED)
target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC ${EIGEN3_INCLUDE_DIRS})

find_package(Boost COMPONENTS log filesystem)
target_link_libraries(${PROJECT_NAME} PUBLIC Boost::log Boost::filesystem)
