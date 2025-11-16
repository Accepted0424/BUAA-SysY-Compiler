cmake_minimum_required(VERSION 3.10)
project(myCompiler)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 添加子目录
add_subdirectory(src)

# 添加可执行文件
add_executable(Compiler src/main.cpp)

# 链接组件库
target_link_libraries(Compiler PRIVATE frontend llvm)
