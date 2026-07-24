#指定所需的最低 CMake 版本
cmake_minimum_required(VERSION 3.15)
project(Sqlite_DB C CXX)

message("Sqlite_DB Cmake DIR :" ${CMAKE_CURRENT_SOURCE_DIR})

set(appleName "Sqlite_DB")

#获取特定文件夹下的.c  .cpp 源文件
set(
    SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Sqlite_Function.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SQLiteCacheRepository.cpp
)

#Moc文件的生成增加头文件信息
set(MOC_HEADERS
  
)

#创建动态库/静态库文件名，将源文件添加到动态库/静态库文件里
add_library(${appleName} STATIC ${SOURCES} ${MOC_HEADERS})

#显示启用 AUTOMOC
set_target_properties(${appleName} PROPERTIES AUTOMOC ON)

#将源头文件的路径传递给建立的动态库/静态库文件里
target_include_directories(${appleName} PUBLIC
$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
$<INSTALL_INTERFACE:include>
)

#寻找对应的库和模块,该部分在父文件里已经执行

#对本地动态库/静态库文件链接进应用程序里
target_link_libraries(${appleName}
PRIVATE
third_party_deps
ProtocolHeader::ProtocolHeader
MQTTEncryptionClient::MQTTEncryptionClient 
Thread_pool_ptr::Thread_pool_ptr
)

#为该库创建一个别名
add_library(Sqlite_DB::Sqlite_DB ALIAS ${appleName})

# 显式检查目标是否存在
if(NOT TARGET Sqlite_DB::Sqlite_DB)
    message(STATUS "Dependency target 'Sqlite_DB' not found")
else()
    message(STATUS "Dependency target 'Sqlite_DB' found")
endif()