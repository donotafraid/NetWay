[settings]
os=Linux
arch=x86_64
compiler=clang
compiler.version=17
compiler.libcxx=libc++
compiler.cppstd=gnu17
build_type=Debug

[conf]
tools.build:compiler_executables={"c": "/usr/bin/clang", "cpp": "/usr/bin/clang++"}

# 👇 关键：禁用 LTO
tools.build:cflags=["-fno-lto"]
tools.build:cxxflags=["-fno-lto"]
tools.build:linker_scripts=[]