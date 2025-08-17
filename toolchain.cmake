# toolchain.cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# 设置 Clang 编译器
set(CMAKE_C_COMPILER "clang")
set(CMAKE_CXX_COMPILER "clang++")

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
    
# # 告诉 Clang 编译器使用 libc++ 作为 C++ 标准库实现（而不是默认的 libstdc++）。
# # 这里使用 "${CMAKE_CXX_FLAGS} -stdlib=libc++" 是为了保留原有的编译选项，避免覆盖。
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")

# # 确保链接阶段能找到 libc++ 和 libc++abi 库文件
# # -L${LIBCXX_LIBRARY}：指定链接器搜索库文件的路径

# # CMAKE_EXE_LINKER_FLAGS：适用于可执行文件。
# # CMAKE_SHARED_LINKER_FLAGS：适用于动态库（.so）。
# # CMAKE_MODULE_LINKER_FLAGS：适用于模块库（如插件）。
# set(COMMON_LINKER_FLAGS "-L${LIBCXX_LIBRARY} -stdlib=libc++ -lc++abi")
# set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${COMMON_LINKER_FLAGS}")
# set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${COMMON_LINKER_FLAGS}")
# set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} ${COMMON_LINKER_FLAGS}")

# # 显式指定项目依赖的标准库（覆盖 CMake 的默认值）
# # CACHE STRING "Standard libraries" FORCE：将变量存入 CMake 缓存，允许用户在后续配置中修改。
# set(CMAKE_CXX_STANDARD_LIBRARIES "-lc++ -lc++abi" CACHE STRING "Standard libraries" FORCE)
