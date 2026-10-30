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
# The toolchain is passed explicitly by quickstart.sh / CMake; do not embed an
# absolute path to one developer's checkout here.
tools.build:cflags=["-fno-lto"]
tools.build:cxxflags=["-fno-lto"]
tools.build:exelinkflags=["-fno-lto"]
tools.build:sharedlinkflags=["-fno-lto"]

[buildenv]
CC=clang-17
CXX=clang++-17
