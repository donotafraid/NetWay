#指定所需的最低 CMake 版本
cmake_minimum_required(VERSION 3.15)

include("${CMAKE_CURRENT_SOURCE_DIR}/toolchain.cmake")

#设置项目名和支持语言,该部分应该在上述设置之后。不然会造成 不断更新设置的死循环
project(CrossPlatformApp LANGUAGES CXX C)

# 包含 Conan 工具链文件
#${CMAKE_BINARY_DIR} 是 CMake 的内置变量，它会自动指向你执行 构建makefile 命令时所在的目录
list(APPEND CMAKE_PREFIX_PATH "${CONAN_DIR}")
set(CONAN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/conan_debug_build")
include(${CONAN_DIR}/conan_toolchain.cmake)
message(STATUS "Conan include paths: ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES}")
message(STATUS "CXX Compiler: ${CMAKE_CXX_COMPILER}")

if(USE_ASAN)
    message(STATUS "AddressSanitizer enabled (Clang)")
    add_compile_options(
        -fsanitize=address
        -fsanitize=undefined
        -fno-omit-frame-pointer
        -fno-optimize-sibling-calls
    )
    add_link_options(-fsanitize=address -fsanitize=undefined)
endif()

# 添加 Clang 特有编译选项
add_compile_options(
    -fstandalone-debug  # 更好的调试信息
    -Wthread-safety     # 线程安全分析
    -Wno-unused-command-line-argument
)

# 修改构建类型配置
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(
        -glldb            # 使用 LLDB 兼容格式
        -fno-limit-debug-info
    )
endif()

# 确保生成 compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

#设置 Qt5 以及其他模块 的路径
find_package(Qt5 COMPONENTS Core Widgets Gui REQUIRED)
find_package(fmt REQUIRED)
find_package(spdlog REQUIRED)   
find_package(SQLite3 REQUIRED)   
find_package(eclipse-paho-mqtt-c REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(PahoMqttCpp REQUIRED)
find_package(Backward REQUIRED)
find_package(Boost REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(protobuf REQUIRED)
find_package(CURL REQUIRED)

# 设置 Protocol Buffers 文件的路径 (需要在命令行先生成后，再执行 cmake, 保证在 build 中生成了 proto.h 文件)
set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(PROTO_SRCS "${PROTO_DIR}/proto/message_struct.pb.cc" )
set(PROTO_HDRS "${PROTO_DIR}/proto/message_struct.pb.h")
# 将生成的 Protocol Buffers 文件编译为静态库
add_library(proto_files STATIC ${PROTO_SRCS} ${PROTO_HDRS})
target_include_directories(proto_files SYSTEM PUBLIC ${PROTO_DIR} ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES}) 
target_link_libraries(proto_files PUBLIC protobuf::libprotobuf)

# 打包 snap7 库
add_library(snap7_lib SHARED IMPORTED)
set_target_properties(snap7_lib PROPERTIES
    IMPORTED_LOCATION "/usr/lib/libsnap7.so"
    INTERFACE_INCLUDE_DIRECTORIES "/root/snap7/release/wrappers/c-cpp"
)

#设置第三方依赖项 
add_library(third_party_deps INTERFACE)
target_link_libraries(third_party_deps INTERFACE 
    proto_files
    snap7_lib 
    nlohmann_json::nlohmann_json
    eclipse-paho-mqtt-c::paho-mqtt3as-static
    openssl::openssl
    PahoMqttCpp::paho-mqttpp3-static
    spdlog::spdlog
    SQLite::SQLite3 
    Backward::Backward
    Boost::headers
    protobuf::libprotobuf
    Qt5::Core
    Qt5::Widgets
    Qt5::Gui

)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 设置构建类型为 Debug 模式。此模式下，CMake 会 自动启用编译器调试选项（如 -g）并 禁用优化（如 -O0）。
# 对于 GNU 编译器（gcc/g++），Debug 模式通常隐含以下行为：
# 生成调试符号（-g）。
# 禁用大部分优化（-O0），确保代码结构与源代码一致。
# 启用断言（-D_DEBUG）和其他调试宏。
add_compile_options(-O0)

#添加子目录 （自动执行子目录的 CMakeLists.txt ）
add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ProtocolHeader 
    ${CMAKE_BINARY_DIR}/ProtocolHeader-build
)
add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/MQTTEncryptionClient 
    ${CMAKE_BINARY_DIR}/MQTTEncryptionClient-build
)
add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/MQTTDecryptionServer 
    ${CMAKE_BINARY_DIR}/MQTTDecryptionServer-build
)
add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Sqlite_DB 
    ${CMAKE_BINARY_DIR}/Sqlite_DB-build
)
add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/MainWindows 
    ${CMAKE_BINARY_DIR}/MainWindows-build
)
add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Thread_pool 
    ${CMAKE_BINARY_DIR}/Thread_pool-build
)
add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PLC   
    ${CMAKE_BINARY_DIR}/PLC-build
)

#设置头文件目录
set(CurrentFolder "${CMAKE_CURRENT_SOURCE_DIR}")
set(SRC_DIR "${CurrentFolder}/src")

include_directories(
    "${CurrentFolder}/include"
)

#获取 源入口 文件
file(GLOB SRC_FILES  "${SRC_DIR}/main.cpp")

#创建可执行文件 
add_executable(file_platform ${SRC_FILES}
        src/main.cpp)

#设置动态库/静态库目录,里面包括了 .lib ，.include 的消息
target_link_libraries(file_platform PRIVATE 
 MQTTEncryptionClient::MQTTEncryptionClient 
 MQTTDecryptionServer::MQTTDecryptionServer
 Sqlite_DB::Sqlite_DB
 MainWindows::MainWindows
 Thread_pool_ptr::Thread_pool_ptr
 PLC::PLC
 third_party_deps
 )

#遍历输出链接的库名称
foreach(lib IN  LISTS LINK_LIBRARIES)
   message(STATUS "Link Library: ${lib}") 
endforeach()

#设置输出目录
set_target_properties(
    file_platform PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CurrentFolder}/bin"
)



