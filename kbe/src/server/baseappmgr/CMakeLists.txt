
# 收集源文件
set(SRCS
	baseapp.cpp
	baseappmgr.cpp
	baseappmgr_interface.cpp
	main.cpp
)

# 创建可执行程序
add_executable(baseappmgr ${SRCS})



# link_directories(${KBE_ROOT}/kbe/src/build/vcpkg_installed/x64-linux/lib)

# 链接依赖的库
target_link_libraries(baseappmgr
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

target_compile_definitions(baseappmgr PUBLIC  LOG4CXX_STATIC)

# ---------------- Python ----------------
target_link_libraries(baseappmgr PRIVATE python_interface)
if(NOT APPLE)
    target_link_options(baseappmgr PRIVATE -Wl,-export-dynamic)
endif()

# ---------------- OpenSSL ----------------
target_link_libraries(baseappmgr PRIVATE KBEOpenSSL::SSL)


# 设置可执行文件输出目录
set_target_properties(baseappmgr PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${KBE_ROOT}/kbe/bin/server"
)