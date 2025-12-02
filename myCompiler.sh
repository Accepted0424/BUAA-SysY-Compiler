#!/bin/bash

IR_FILE="llvm_ir.txt"
IO_SRC="io.c"
IO_LIB="libio.so"

if [ "$1" == "release" ]; then
  if [ "$2" == "hw2" ]; then
    zip -r hw2.zip src CMakeLists.txt config.json
  fi

  if [ "$2" == "hw3" ]; then
    zip -r hw3.zip src CMakeLists.txt config.json myCompiler.sh test
  fi

  if [ "$2" == "hw4" ]; then
    zip -r hw4.zip src CMakeLists.txt config.json myCompiler.sh
  fi

  if [ "$2" == "hw5" ]; then
    zip -r hw5.zip src CMakeLists.txt config.json myCompiler.sh scripts
  fi
fi

if [ "$1" == "run" ]; then
  # 编译为共享库
  echo "编译内建 IO 库..."
  clang -shared -o $IO_LIB $IO_SRC

  if [ $? -ne 0 ]; then
    echo "编译失败！"
    exit 1
  fi

  # 运行 IR
  echo "运行 IR..."
  lli -load=./$IO_LIB $IR_FILE
fi