# profiles/clang_debug_profile

[settings]
os=Linux
arch=x86_64
compiler=clang
compiler.version=17
compiler.libcxx=libc++  
compiler.cppstd=gnu17   
build_type=Debug

[conf]
# 包含你的自定义工具链文件
tools.cmake.cmaketoolchain:user_toolchain=["/root/Cross_platform_file_transfer_tool-main/Cross_platform_file_transfer_tool-main/toolchain.cmake"]
tools.build:cflags=["-fno-lto"]
tools.build:cxxflags=["-fno-lto"]
tools.build:exelinkflags=["-fno-lto"]
tools.build:sharedlinkflags=["-fno-lto"]

# 可选：添加其他编译选项
# tools.build:cxxflags.append=["-Wall", "-Wextra", "-Werror"]

[buildenv]
# 也可以在这里设置环境变量
CC=clang
CXX=clang++

# build 机器：运行 Conan 命令、执行编译过程的机器。

# host 机器：最终可执行文件/库将要运行的平台。

# target 机器（极少用）：例如调试器运行在另一台机器时才需单独指定。

# 在常见的本地编译（不交叉编译）时，build 与 host 相同，因此可以只用一个 profile。