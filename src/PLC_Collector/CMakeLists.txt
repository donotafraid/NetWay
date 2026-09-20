#指定所需的最低 Cmkae 版本
cmake_minimum_required(VERSION 3.15)
project(PLC_Collector C CXX)

message("PLC_Collector CMake DIR: " ${CMAKE_CURRENT_SOURCE_DIR})

#获取特定文件夹下的.c  .cpp 源文件
set(
    SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/collector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/DataConsumer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MessageSendService.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PushGatewayImpl.cpp
)

#设置源头文件目录
#创建动态库/静态库文件名，将源文件添加到动态库/静态库文件里
set(appleName "PLC_Collector")
add_library(${appleName} STATIC ${SOURCES} ${MOC_HEADERS})
set_target_properties(${appleName} PROPERTIES AUTOMOC ON)

#将源头文件的路径传递给建立的动态库/静态库文件里
target_include_directories(${appleName} PUBLIC
${CMAKE_SOURCE_DIR}/include

)

#对本地动态库/静态库文件链接进应用程序里
target_link_libraries(${appleName}
PRIVATE
ServiceMetrics::ServiceMetrics
PLC::PLC
third_party_deps
)

#为该库创建一个别名
add_library(PLC_Collector::PLC_Collector ALIAS ${appleName})

# 显式检查目标是否存在
if(NOT TARGET PLC_Collector::PLC_Collector)
    message(STATUS "Dependency target 'PLC_Collector' not found")
else()
    message(STATUS "Dependency target 'PLC_Collector' found")
endif()
