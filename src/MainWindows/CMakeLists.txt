#指定所需的最低 Cmkae 版本
cmake_minimum_required(VERSION 3.15)
project(MainWindows C CXX)

message("MainWindows Cmake DIR :" ${CMAKE_CURRENT_SOURCE_DIR})

set(appleName "MainWindows")

#获取特定文件夹下的.c  .cpp 源文件
set(
    SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/MainWindow_Rebuild.cpp
    # ${CMAKE_CURRENT_SOURCE_DIR}/Monitor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/DropArea.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/LineChartTest.cpp
)

#确保编译器能找到库文件进行 Moc 文件的生成
set (MOC_HEADERS
    # ${CMAKE_CURRENT_SOURCE_DIR}/../../include/MainWindows/Monitor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/MainWindows/DropArea.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/MainWindows/MainWindow_Rebuild.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/MainWindows/LineChartTest.h
)

#创建动态库/静态库文件名，将源文件添加到动态库/静态库文件里
add_library(${appleName} STATIC ${SOURCES} ${MOC_HEADERS})

set_target_properties(${appleName} PROPERTIES AUTOMOC ON)

#确保编译器能找到头文件进行编译,用于 #include 指令
target_include_directories(${appleName} PUBLIC
${CMAKE_SOURCE_DIR}/include

)

#对本地动态库/静态库文件链接进应用程序里
target_link_libraries(${appleName}
PRIVATE
Sqlite_DB::Sqlite_DB
PLC::PLC
third_party_deps
)

#为该库创建一个别名
add_library(MainWindows::MainWindows ALIAS ${appleName})

# 显式检查目标是否存在
if(NOT TARGET MainWindows::MainWindows)
    message(STATUS "Dependency target 'MainWindows' not found")
else()
    message(STATUS "Dependency target 'MainWindows' found")
endif()