set(LIBRARY_NAME frontend)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS *.cpp)
add_library(${LIBRARY_NAME} ${SOURCES})

target_include_directories(${LIBRARY_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include        # frontend/include
        ${CMAKE_CURRENT_SOURCE_DIR}/..             # 上一级目录 src
)