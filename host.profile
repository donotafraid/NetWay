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