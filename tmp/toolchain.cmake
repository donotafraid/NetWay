# tmp/toolchain.cmake —— GCC 11 + libstdc++11 工具链
# 与 profiles/gcc_debug_profile 配对使用
# ------------------------------------------------------------

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# ★ 显式指定 GCC（覆盖 shell 里可能残留的 CC/CXX=clang）
set(CMAKE_C_COMPILER   /usr/bin/gcc   CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER /usr/bin/g++   CACHE FILEPATH "" FORCE)

# ★ GCC 默认就用 libstdc++，这里显式写出防止被别的 toolchain 污染
#   注意：不要加 -stdlib=libc++，那是 clang 的选项
set(CMAKE_CXX_FLAGS_INIT
    "-D_GLIBCXX_USE_CXX11_ABI=1")   # 强制 libstdc++ 新 ABI（与 Conan libstdc++11 一致）

# ★ 确保链接期也用系统的 libstdc++，不要拖进 libc++
#   （GCC 默认行为，显式留空避免继承外部设置）
set(CMAKE_EXE_LINKER_FLAGS_INIT    "")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "")

# ★ 不要 link_directories(LIBCXX_LIBRARY) —— 那是 clang/libc++ 的东西
#   如果你要指定 libstdc++ 路径，可以用下面这行（一般不需要）
# link_directories(/usr/lib/x86_64-linux-gnu)

# ---- 可选：保证 Conan 找到的库 ABI 与项目一致 ----
# 这一段是给 Conan 2.x 的 toolchain 打补丁用的：
# 当 Conan 的 conan_toolchain.cmake 先被 include 时，
# 这里显式再确认一遍编译器，避免被 Conan 里残留的 clang 设置覆盖。
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR
        "检测到 CMAKE_CXX_COMPILER_ID=${CMAKE_CXX_COMPILER_ID}，"
        "但 gcc_debug_profile 要求 GCC。"
        "请检查 Conan profile 或 shell 里的 CC/CXX 环境变量。")
endif()