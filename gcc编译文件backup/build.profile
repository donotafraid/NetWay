# profiles/gcc_debug_profile

[settings]
os=Linux
arch=x86_64
compiler=gcc
compiler.version=11
compiler.libcxx=libstdc++11
compiler.cppstd=gnu17
build_type=Debug

[conf]
# ★ 不要引用 clang 的 toolchain.cmake
# ★ GCC 用系统默认即可，不传 user_toolchain
tools.cmake.cmaketoolchain:user_toolchain=["/root/Cross_platform_file_transfer_tool-main/Cross_platform_file_transfer_tool-main/toolchain.cmake"]
tools.build:cflags=["-fno-lto"]
tools.build:cxxflags=["-fno-lto"]
tools.build:exelinkflags=["-fno-lto"]
tools.build:sharedlinkflags=["-fno-lto"]

[buildenv]
# ★ 明确指定 GCC（避免 shell 里残留 CC/CXX=clang）
CC=gcc
CXX=g++