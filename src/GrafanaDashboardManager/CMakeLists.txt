#指定所需的最低 Cmkae 版本
cmake_minimum_required(VERSION 3.15)
project(GrafanaDashboardManager C CXX)

message("GrafanaDashboardManager Cmake DIR :" ${CMAKE_CURRENT_SOURCE_DIR})

set(appleName "GrafanaDashboardManager")

#获取特定文件夹下的.c  .cpp 源文件
set(
    SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/GrafanaDashboard.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GrafanaServer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ObjectRouter.cpp
)

#设置源目录变量
set(TargetFolder "GrafanaDashboardManager")

#确保编译器能找到库文件进行 Moc 文件的生成
set (MOC_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/GrafanaDashboardManager/GrafanaDashboard.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/GrafanaDashboardManager/GrafanaServer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/GrafanaDashboardManager/ObjectRouter.h
)

#创建动态库/静态库文件名，将源文件添加到动态库/静态库文件里
add_library(${appleName} STATIC ${SOURCES} ${MOC_HEADERS})

set_target_properties(${appleName} PROPERTIES AUTOMOC ON)

#确保编译器能找到头文件进行编译,用于 #include 指令
target_include_directories(${appleName} PUBLIC
$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
$<INSTALL_INTERFACE:include>
)

#对本地动态库/静态库文件链接进应用程序里
target_link_libraries(${appleName}
PRIVATE
third_party_deps
)

#为该库创建一个别名
add_library(GrafanaDashboardManager::GrafanaDashboardManager ALIAS ${appleName})

# 显式检查目标是否存在
if(NOT TARGET GrafanaDashboardManager::GrafanaDashboardManager)
    message(STATUS "Dependency target 'GrafanaDashboardManager' not found")
else()
    message(STATUS "Dependency target 'GrafanaDashboardManager' found")
endif()