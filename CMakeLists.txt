#指定所需的最低 CMake 版本
cmake_minimum_required(VERSION 3.15)

#设置项目名和支持语言,该部分应该在上述设置之后。不然会造成 不断更新设置的死循环
project(CrossPlatformApp LANGUAGES CXX C)

# 包含 Conan 工具链文件
#${CMAKE_PREFIX_PATH} 是 CMake 的内置变量，find_package()会在这个路径下寻找是否存在可用的文件(系统标准路径 PATH 也是路径之一)
set(CONAN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/conan_debug_build"
    CACHE PATH "Conan 依赖目录（可按环境覆盖）")
if(EXISTS ${CONAN_DIR})
    list(APPEND CMAKE_PREFIX_PATH "${CONAN_DIR}")
endif()

# 允许通过 -DOPEN62541_ROOT=... 覆盖；默认沿用你本地路径
set(OPEN62541_ROOT "/root/open62541-v1.4.14-noSanitizer"
    CACHE PATH "open62541 source root")
set(OPEN62541_BUILD "${OPEN62541_ROOT}/build"
    CACHE PATH "open62541 build dir")

# ... 在 include(conan_toolchain.cmake) 之后 ...
# ============================================================
# Sanitizer 选项（ASan+UBSan 和 TSan 互斥）
# ============================================================
option(ENABLE_ASAN "Enable AddressSanitizer + UBSan" OFF)
option(ENABLE_LSAN "Enable AddressSanitizer + LSan (leak only)" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)

# 三者互斥
set(_san_count 0)
foreach(_opt ENABLE_ASAN ENABLE_LSAN ENABLE_TSAN)
    if(${_opt})
        math(EXPR _san_count "${_san_count} + 1")
    endif()
endforeach()
if(_san_count GREATER 1)
    message(FATAL_ERROR "Only one of ENABLE_ASAN/ENABLE_LSAN/ENABLE_TSAN can be ON")
endif()

if(ENABLE_LSAN)
    message(STATUS "AddressSanitizer + LSan: ENABLED")
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
    endif()
    add_compile_options(-fsanitize=address,leak -fno-omit-frame-pointer -g -O1)
    add_link_options(-fsanitize=address,leak)
endif()

if(ENABLE_ASAN)
    message("=========================================")
    message(STATUS "AddressSanitizer + UBSan: ENABLED")
    message(STATUS "CMAKE_BINARY_DIR = ${CMAKE_BINARY_DIR}")

    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
        message(STATUS "CMAKE_BUILD_TYPE set to: ${CMAKE_BUILD_TYPE}")
    else()
        message(STATUS "CMAKE_BUILD_TYPE is: ${CMAKE_BUILD_TYPE}")
    endif()

    add_compile_options(-fsanitize=address,undefined)
    add_compile_options(-fno-sanitize=function)
    add_compile_options(-fno-omit-frame-pointer)
    add_compile_options(-g)
    add_compile_options(-fno-optimize-sibling-calls)

    add_link_options(-fsanitize=address,undefined)
    add_link_options(-fno-sanitize=function)

    set(ASAN_LOG_DIR "${CMAKE_BINARY_DIR}/asan_logs")
    file(MAKE_DIRECTORY ${ASAN_LOG_DIR})
    message(STATUS "ASAN logs directory: ${ASAN_LOG_DIR}")
    message(STATUS "To use ASAN at runtime, set:")
    message(STATUS "  export ASAN_OPTIONS='log_path=${ASAN_LOG_DIR}/asan_report:log_exe_name=1:log_pid=1:detect_leaks=1:leak_check_at_exit=1'")

    message("=========================================")

elseif(ENABLE_TSAN)
    message("=========================================")
    message(STATUS "ThreadSanitizer: ENABLED")
    message(STATUS "CMAKE_BINARY_DIR = ${CMAKE_BINARY_DIR}")

    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
        message(STATUS "CMAKE_BUILD_TYPE set to: ${CMAKE_BUILD_TYPE}")
    else()
        message(STATUS "CMAKE_BUILD_TYPE is: ${CMAKE_BUILD_TYPE}")
    endif()

    # TSan 推荐 -O1 或 -O2，配合 -g
    add_compile_options(-fsanitize=thread)
    add_compile_options(-fno-omit-frame-pointer)
    add_compile_options(-g)
    add_compile_options(-O1)   # TSan 推荐，避免 -O0 的漏报

    add_link_options(-fsanitize=thread)

    message(STATUS "To use TSan at runtime, set:")
    message(STATUS "  export TSAN_OPTIONS='halt_on_error=0:second_deadlock_stack=1'")

    message("=========================================")

else()
    message(STATUS "Sanitizers: DISABLED")
endif()


# 添加 Clang 特有编译选项
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(
        -fstandalone-debug      # 更好的调试信息
        -Wthread-safety         # 线程安全分析
        -Wno-unused-command-line-argument
    )
else()
    # GCC 下对应的合理替代（可选）
    add_compile_options(-Wno-unused-command-line-argument)
endif()

# 修改构建类型配置
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(
            -glldb                # LLDB 兼容格式（Clang 专属）
            -fno-limit-debug-info # Clang 专属
        )
    endif()
endif()

# 确保生成 compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

#设置 Qt5 以及其他模块 的路径
find_package(fmt REQUIRED)
find_package(spdlog REQUIRED)   
find_package(OpenSSL REQUIRED)
find_package(Backward REQUIRED)
find_package(Boost REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(CURL REQUIRED)
find_package(GTest REQUIRED)


# 兼容别名：CMake 标准 target 是 OpenSSL::SSL / OpenSSL::Crypto，
# 但项目历史代码用 openssl::openssl，这里补一个 alias，
# 避免改动 src/tests 里的 target_link_libraries。
if(NOT TARGET openssl::openssl)
    add_library(openssl::openssl INTERFACE IMPORTED)
    set_target_properties(openssl::openssl PROPERTIES
        INTERFACE_LINK_LIBRARIES "OpenSSL::SSL;OpenSSL::Crypto")
endif()

# 导入库
add_library(open62541_lib SHARED IMPORTED)
set_target_properties(open62541_lib PROPERTIES
    IMPORTED_LOCATION "${OPEN62541_BUILD}/bin/libopen62541.so"
    INTERFACE_INCLUDE_DIRECTORIES
        "${OPEN62541_BUILD}/src_generated;${OPEN62541_ROOT}/include;${OPEN62541_ROOT}/plugins/include"   
)
get_target_property(OPEN62541_INCLUDE_DIRS open62541_lib INTERFACE_INCLUDE_DIRECTORIES)
message(STATUS "open62541_lib INTERFACE_INCLUDE_DIRECTORIES: ${OPEN62541_INCLUDE_DIRS}")


#设置第三方依赖项 
add_library(third_party_deps INTERFACE)
target_link_libraries(third_party_deps INTERFACE 
    spdlog::spdlog
    Backward::Backward
    Boost::headers
    open62541_lib
    CURL::libcurl
    GTest::gtest_main
)

# set(CMAKE_AUTOMOC ON)
# set(CMAKE_AUTORCC ON)
# set(CMAKE_AUTOUIC ON)

# 设置构建类型为 Debug 模式。此模式下，CMake 会 自动启用编译器调试选项（如 -g）并 禁用优化（如 -O0）。
# 对于 GNU 编译器（gcc/g++），Debug 模式通常隐含以下行为：
# 生成调试符号（-g）。
# 禁用大部分优化（-O0），确保代码结构与源代码一致。
# 启用断言（-D_DEBUG）和其他调试宏。
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-O0)
endif()

#添加子目录 （自动执行子目录的 CMakeLists.txt ）
#${CMAKE_BINARY_DIR} 是 CMake 的内置变量，它会自动指向你执行 构建makefile 命令时所在的目录
add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OPCUAPacking/
    OPCUAPacking-build         
)
enable_testing()
add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/
    tests-build         
)


#设置头文件目录
set(CurrentFolder "${CMAKE_CURRENT_SOURCE_DIR}")
set(SRC_DIR "${CurrentFolder}/src")


#创建可执行文件 
add_executable(file_platform  src/main_uaPacking.cpp)

#限制头文件的传播范围，避免污染
target_include_directories(file_platform PRIVATE "${CurrentFolder}/include")

#设置动态库/静态库目录,里面包括了 .lib ，.include 的消息
target_link_libraries(file_platform PRIVATE 
 third_party_deps
 OPCUAPacking::OPCUAPacking
 )


#设置输出目录
set_target_properties(
    file_platform PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CurrentFolder}/bin"
)



