
# 收集源文件
set(SRCS
	cell.cpp
	cells.cpp
	cellapp.cpp
	cellappmgr.cpp
	cellappmgr_interface.cpp
	main.cpp
	space.cpp
	spaces.cpp
	space_viewer.cpp
)

# 创建可执行程序
add_executable(cellappmgr ${SRCS})



# link_directories(${KBE_ROOT}/kbe/src/build/vcpkg_installed/x64-linux/lib)

# 链接依赖的库
target_link_libraries(cellappmgr
    PRIVATE
        server
        resmgr
        network
        thread
        common
        helper
        math
        g3dlite
        log4cxx

)

target_compile_definitions(cellappmgr PUBLIC  LOG4CXX_STATIC)


# ---------------- Python ----------------
target_link_libraries(cellappmgr PRIVATE python_interface)
if(NOT APPLE)
    target_link_options(cellappmgr PRIVATE -Wl,-export-dynamic)
endif()

# ---------------- OpenSSL ----------------
target_link_libraries(cellappmgr PRIVATE KBEOpenSSL::SSL)


# 设置可执行文件输出目录
set_target_properties(cellappmgr PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${KBE_ROOT}/kbe/bin/server"
)