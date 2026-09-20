#指定所需的最低 Cmkae 版本
cmake_minimum_required(VERSION 3.15)
project(Thread_pool_ptr C CXX)

message("Thread_pool_ptr Cmake DIR :" ${CMAKE_CURRENT_SOURCE_DIR})

set(appleName "Thread_pool_ptr")

#获取特定文件夹下的.c  .cpp 源文件
set(
    SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Thread_pool_define.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ConnectionPool.cpp
)

#设置源目录变量
set(TargetFolder "Thread_pool_ptr")

#创建动态库/静态库文件名，将源文件添加到动态库/静态库文件里
add_library(${appleName} STATIC ${SOURCES})

#显示启用 AUTOMOC
set_target_properties(${appleName} PROPERTIES AUTOMOC ON)

#将源头文件的路径传递给建立的动态库/静态库文件里
target_include_directories(${appleName} PUBLIC
${CMAKE_SOURCE_DIR}/include

)

#寻找对应的库和模块
target_link_libraries(${appleName} 
PRIVATE 
third_party_deps
)

#为该库创建一个别名
add_library(Thread_pool_ptr::Thread_pool_ptr ALIAS ${appleName})

# 显式检查目标是否存在
if(NOT TARGET Thread_pool_ptr::Thread_pool_ptr)
    message(STATUS "Dependency target 'Thread_pool_ptr' not found")
else()
    message(STATUS "Dependency target 'Thread_pool_ptr' found")
endif()