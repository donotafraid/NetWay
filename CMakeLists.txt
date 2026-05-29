#指定所需的最低 CMake 版本
cmake_minimum_required(VERSION 3.15)

#设置项目名和支持语言,该部分应该在上述设置之后。不然会造成 不断更新设置的死循环
project(CrossPlatformApp LANGUAGES CXX C)

# 包含 Conan 工具链文件
#${CMAKE_PREFIX_PATH} 是 CMake 的内置变量，find_package()会在这个路径下寻找是否存在可用的文件(系统标准路径 PATH 也是路径之一)
set(CONAN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/conan_debug_build")
if(EXISTS ${CONAN_DIR})
    list(APPEND CMAKE_PREFIX_PATH "${CONAN_DIR}")
endif()


# ... 在 include(conan_toolchain.cmake) 之后 ...

option(ENABLE_ASAN "Enable AddressSanitizer + UBSan" ON)
message(STATUS "ENABLE_ASAN Status : ${ENABLE_ASAN}")

if(ENABLE_ASAN)
    message("=========================================")
    message(STATUS "AddressSanitizer + UBSan: ENABLED")
    message(STATUS "CMAKE_BINARY_DIR = ${CMAKE_BINARY_DIR}")
    
    # 1. 设置构建类型为 Debug（可选）
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
        message(STATUS "CMAKE_BUILD_TYPE set to: ${CMAKE_BUILD_TYPE}")
    else()
        message(STATUS "CMAKE_BUILD_TYPE is: ${CMAKE_BUILD_TYPE}")
    endif()
    
    # 2. 统一的编译标志（只使用一种方式）
    set(SANITIZER_FLAGS "-fsanitize=address,undefined -fno-sanitize=function -fno-omit-frame-pointer -g")
    set(SANITIZER_CXX_FLAGS "${SANITIZER_FLAGS} -fno-optimize-sibling-calls")
    
    # 使用 add_compile_options（推荐）而不是 set CACHE FORCE
    add_compile_options(-fsanitize=address,undefined)
    add_compile_options(-fno-sanitize=function)
    add_compile_options(-fno-omit-frame-pointer)
    add_compile_options(-g)
    add_compile_options(-fno-optimize-sibling-calls)
    
    add_link_options(-fsanitize=address,undefined)
    add_link_options(-fno-sanitize=function)
    
    message(STATUS "Sanitizer flags added to compile/link options")
    
    # 3. 设置 ASAN 运行时选项（仅在运行测试时有效，但可以导出）
    set(ASAN_LOG_DIR "${CMAKE_BINARY_DIR}/asan_logs")
    file(MAKE_DIRECTORY ${ASAN_LOG_DIR})
    message(STATUS "ASAN logs directory: ${ASAN_LOG_DIR}")
    
    # 注意：这只是设置 CMake 环境变量，不影响实际运行的程序
    # 实际运行程序时需要手动设置或通过脚本设置
    message(STATUS "To use ASAN at runtime, set:")
    message(STATUS "  export ASAN_OPTIONS='log_path=${ASAN_LOG_DIR}/asan_report:log_exe_name=1:log_pid=1:detect_leaks=1:leak_check_at_exit=1'")
    
    message("=========================================")
else()
    message(STATUS "AddressSanitizer: DISABLED")
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
find_package(Qt5 COMPONENTS Core Widgets Gui WebEngine WebEngineWidgets  Charts REQUIRED)
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

# 1. 使用变量提高可维护性
set(OPEN62541_ROOT "/root/open62541-v1.4.14")
set(OPEN62541_BUILD "${OPEN62541_ROOT}/build")

# 2. 导入库
add_library(open62541_lib SHARED IMPORTED)
set_target_properties(open62541_lib PROPERTIES
    IMPORTED_LOCATION "${OPEN62541_BUILD}/bin/libopen62541.so"
    INTERFACE_INCLUDE_DIRECTORIES
        "${OPEN62541_BUILD}/src_generated;${OPEN62541_ROOT}/include;${OPEN62541_ROOT}/plugins/include"   
)
get_target_property(OPEN62541_INCLUDE_DIRS open62541_lib INTERFACE_INCLUDE_DIRECTORIES)
message(STATUS "open62541_lib INTERFACE_INCLUDE_DIRECTORIES: ${OPEN62541_INCLUDE_DIRS}")

# 打包 prometheus-cpp-core 库
add_library(prometheus_core SHARED IMPORTED)
set_target_properties(prometheus_core PROPERTIES
    IMPORTED_LOCATION "/root/prometheus-cpp/build/lib/libprometheus-cpp-core.so"
    INTERFACE_INCLUDE_DIRECTORIES 
        "/root/prometheus-cpp/core/include;/root/prometheus-cpp/build/core/include"
)

# 打包 prometheus-cpp-pull 库
add_library(prometheus_pull SHARED IMPORTED)
set_target_properties(prometheus_pull PROPERTIES
    IMPORTED_LOCATION "/root/prometheus-cpp/build/lib/libprometheus-cpp-pull.so"
    INTERFACE_INCLUDE_DIRECTORIES 
        "/root/prometheus-cpp/pull/include;/root/prometheus-cpp/build/pull/include"
)

# 打包 prometheus-cpp-push 库
add_library(prometheus_push SHARED IMPORTED)
set_target_properties(prometheus_push PROPERTIES
    IMPORTED_LOCATION "/root/prometheus-cpp/build/lib/libprometheus-cpp-push.so"
    INTERFACE_INCLUDE_DIRECTORIES 
        "/root/prometheus-cpp/push/include;/root/prometheus-cpp/build/push/include"
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
    Qt5::WebEngine 
    Qt5::WebEngineWidgets  
    Qt5::Charts
    open62541_lib
    prometheus_push
    prometheus_pull
    prometheus_core
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
#${CMAKE_BINARY_DIR} 是 CMake 的内置变量，它会自动指向你执行 构建makefile 命令时所在的目录
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
add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Event_Tracking   
    ${CMAKE_BINARY_DIR}/Event_Tracking-build
)
add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ServiceMetrics   
    ${CMAKE_BINARY_DIR}/ServiceMetrics-build
)
add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/GrafanaDashboardManager   
    ${CMAKE_BINARY_DIR}/GrafanaDashboardManager-build
)
add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/load_config   
    ${CMAKE_BINARY_DIR}/load_config-build
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
 Event_Tracking::Event_Tracking
 ServiceMetrics::ServiceMetrics
GrafanaDashboardManager::GrafanaDashboardManager
Common_Libraries_Struct::Common_Libraries_Struct
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



