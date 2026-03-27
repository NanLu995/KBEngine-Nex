project(embed_python NONE)

# --------------------------------------------------------
# 0) Python 源码和构建目录
# --------------------------------------------------------
set(PYTHON_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(PYTHON_BUILD_DIR ${PYTHON_SRC_DIR}/build)

# 按配置区分不同的库文件名
set(PYTHON_LIB_RELEASE ${PYTHON_SRC_DIR}/libpython3.13.a)
set(PYTHON_LIB_DEBUG   ${PYTHON_SRC_DIR}/libpython3.13d.a)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(PYTHON_LIB ${PYTHON_LIB_DEBUG})
else()
    set(PYTHON_LIB ${PYTHON_LIB_RELEASE})
endif()

# --------------------------------------------------------
# 1) KBE Python 配置
# --------------------------------------------------------
if(NOT DEFINED KBE_ROOT)
    string(REGEX REPLACE "/kbe/src(/.*)?$" "" KBE_ROOT "${CMAKE_SOURCE_DIR}")
endif()

set(KBE_PYTHON_INCLUDE_DIRS
    ${KBE_ROOT}/kbe/src/lib/python/Include
    ${KBE_ROOT}/kbe/src/lib/python
    CACHE PATH "KBE Python include dirs"
)

set(KBE_PYTHON_LIB
    ${KBE_ROOT}/kbe/src/libs/libpython.a
    CACHE FILEPATH "KBE Python static library"
)

set(KBE_PYTHON_EXTRA_LIBS pthread util dl)

set(RES_SCRIPTS_DIR ${KBE_ROOT}/kbe/res/scripts/common)
set(LIB_DIR ${KBE_ROOT}/kbe/src/libs)

# Post-build helper scripts (runtime glob/copy is safer than hardcoded paths)
set(PYTHON_COPY_DYNLOAD_SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/copy_python_dynload.cmake)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/copy_python_dynload.cmake.in
    ${PYTHON_COPY_DYNLOAD_SCRIPT}
    @ONLY
)

set(PYTHON_COPY_LIB_SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/copy_python_lib.cmake)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/copy_python_lib.cmake.in
    ${PYTHON_COPY_LIB_SCRIPT}
    @ONLY
)

# --------------------------------------------------------
# 2) 编译 Python
# --------------------------------------------------------
# Release 版本
add_custom_command(
    OUTPUT ${PYTHON_LIB_RELEASE}
    COMMAND test -f ${PYTHON_LIB_RELEASE} || (
        chmod +x configure &&
        ${CMAKE_COMMAND} -E env CC=${CMAKE_C_COMPILER} CXX=${CMAKE_CXX_COMPILER}
        ./configure --without-ensurepip &&
        make -j
    )
    WORKING_DIRECTORY ${PYTHON_SRC_DIR}
    COMMENT "Building embedded Python (Release)..."
)

# Debug 版本
add_custom_command(
    OUTPUT ${PYTHON_LIB_DEBUG}
    COMMAND test -f ${PYTHON_LIB_DEBUG} || (
        chmod +x configure &&
        ${CMAKE_COMMAND} -E env CC=${CMAKE_C_COMPILER} CXX=${CMAKE_CXX_COMPILER}
        ./configure --with-pydebug --without-ensurepip &&
        make -j
    )
    WORKING_DIRECTORY ${PYTHON_SRC_DIR}
    COMMENT "Building embedded Python (Debug)..."
)

# 定义构建目标
add_custom_target(python_build ALL
    DEPENDS ${PYTHON_LIB}
)

# --------------------------------------------------------
# 3) 拷贝 libpython.a
# --------------------------------------------------------
add_custom_command(TARGET python_build POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${LIB_DIR}
    COMMAND ${CMAKE_COMMAND} -E remove -f
        ${LIB_DIR}/libpython$<$<CONFIG:Debug>:_d>.a
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${PYTHON_LIB}
        ${LIB_DIR}/libpython$<$<CONFIG:Debug>:_d>.a
    COMMENT "Copying libpython<$<CONFIG:Debug>:_d>.a to ${LIB_DIR}"
)

# --------------------------------------------------------
# 4) 拷贝 lib-dynload
# --------------------------------------------------------
add_custom_command(TARGET python_build POST_BUILD
    COMMAND ${CMAKE_COMMAND} -P ${PYTHON_COPY_DYNLOAD_SCRIPT}
    COMMENT "Copying Python extension modules"
)

# --------------------------------------------------------
# 5) 拷贝 Lib
# --------------------------------------------------------
add_custom_command(TARGET python_build POST_BUILD
    COMMAND ${CMAKE_COMMAND} -P ${PYTHON_COPY_LIB_SCRIPT}
    COMMENT "Copying Python Lib directory"
)

# --------------------------------------------------------
# 6) 定义 INTERFACE target
# --------------------------------------------------------
add_library(python_interface INTERFACE)
add_dependencies(python_interface python_build)

target_include_directories(python_interface INTERFACE ${KBE_PYTHON_INCLUDE_DIRS})

# Debug / Release 自动切换不同库
target_link_libraries(python_interface INTERFACE
    debug ${LIB_DIR}/libpython_d.a
    optimized ${LIB_DIR}/libpython.a
    ${KBE_PYTHON_EXTRA_LIBS}
)
