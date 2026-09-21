message("tests CMake DIR: " ${CMAKE_CURRENT_SOURCE_DIR})

#获取特定文件夹下的.c  .cpp 源文件
set(
    SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/test1.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test2.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test3.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test4.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test5.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test6.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test7.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test8.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test9.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test10.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test11.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test12.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test13.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test14.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test15.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/util/ClientTestFixture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/compile/RuleOfFiveAsserts.cpp
)

#设置源头文件目录
#创建动态库/静态库文件名，将源文件添加到动态库/静态库文件里
set(appleName "tests")
add_executable(${appleName} ${SOURCES})

#将源头文件的路径传递给建立的动态库/静态库文件里
target_include_directories(${appleName} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

set_target_properties(tests PROPERTIES
    BUILD_RPATH "${OPEN62541_BUILD}/bin"
    INSTALL_RPATH "${OPEN62541_BUILD}/bin")


#对本地动态库/静态库文件链接进应用程序里
target_link_libraries(${appleName}
    PRIVATE
    third_party_deps
    OPCUAPacking::OPCUAPacking
)

include(GoogleTest)
gtest_discover_tests(${appleName}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST   # ← 关键：把发现延迟到 ctest 执行时
)

# ============================================================
# 封装完整性探针（报告 §6.4 / CT-2）
# ------------------------------------------------------------
# 目的：把“ua.h 是否泄漏 open62541”做成一个独立的编译期探针。
#
# 原理：
#   · only_ua_h 是 OBJECT 库，只编译、不链接；
#   · 它的 include 路径**只有** include/，故意不带 open62541；
#   · 若 ua.h 自洽（只前向声明 UA_Client/ISdk，不 include SDK 头），
#     本目标编译通过；
#   · 若 ua.h 仍直接/间接拖入 open62541，本目标编译失败，
#     从而把“封装完整性”这条目标变成可证伪的构建产物。
#
# 为什么不用 add_executable：
#   · 链接期会把 OPCUAPacking 的传递 include 路径带回来，
#     破坏“不带 open62541”的前提；
#   · 也不需要 main，OBJECT 库更贴合“只编译”的语义。
# ============================================================
add_library(only_ua_h OBJECT
    ${CMAKE_CURRENT_SOURCE_DIR}/compile/EncapsulationProbe.cpp
)

# ★ 关键：故意不添加 ${open62541_INCLUDE_DIRS} / third_party_deps
target_include_directories(only_ua_h PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(only_ua_h PRIVATE cxx_std_17)

# ★ ALL：让 only_ua_h 在默认 make 时被编；
#   DEPENDS：保证依赖关系正确，且失败时整体构建失败。
add_custom_target(check_encapsulation ALL DEPENDS only_ua_h)