# toolchain.cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# 强制设置编译器（使用完整路径）
set(CMAKE_C_COMPILER /usr/bin/clang-17 CACHE PATH "C compiler" FORCE)
set(CMAKE_CXX_COMPILER /usr/bin/clang++-17 CACHE PATH "C++ compiler" FORCE)

# 设置 C++ 标准库路径
set(LIBCXX_INCLUDE_DIR "/usr/lib/llvm-17/include/c++/v1")
set(LIBCXX_LIBRARY "/usr/lib/llvm-17/lib")

# 添加系统包含目录
include_directories(SYSTEM
${LIBCXX_INCLUDE_DIR}
"/usr/include"
"/usr/include/x86_64-linux-gnu"
"/usr/local/include"
)

# 添加库路径和 rpath
link_directories(${LIBCXX_LIBRARY_DIR})
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L${LIBCXX_LIBRARY_DIR} -Wl,-rpath,${LIBCXX_LIBRARY_DIR}")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -L${LIBCXX_LIBRARY_DIR} -Wl,-rpath,${LIBCXX_LIBRARY_DIR}")

