#指定所需的最低 Cmkae 版本
cmake_minimum_required(VERSION 3.15)
project(ProtocolHeader C CXX)

message("ProtocolHeader Cmake DIR :" ${CMAKE_CURRENT_SOURCE_DIR})

#获取特定文件夹下的.c  .cpp 源文件
set(
    SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/ProtocolHeader.cpp
)

#设置源目录变量
set(TargetFolder "ProtocolHeader")
    
#设置源头文件目录
#创建动态库/静态库文件名，将源文件添加到动态库/静态库文件里
set(appleName "ProtocolHeader")
add_library(${appleName} STATIC ${SOURCES})

#将源头文件的路径传递给建立的动态库/静态库文件里
target_include_directories(${appleName} PUBLIC
${CMAKE_SOURCE_DIR}/include

)

#对本地动态库/静态库文件链接进应用程序里
target_link_libraries(${appleName}
PRIVATE
third_party_deps
)

#为该库创建一个别名
add_library(ProtocolHeader::ProtocolHeader ALIAS ${appleName})

# 显式检查目标是否存在
if(NOT TARGET ProtocolHeader::ProtocolHeader)
    message(STATUS "Dependency target 'ProtocolHeader' not found")
else()
    message(STATUS "Dependency target 'ProtocolHeader' found")
endif()