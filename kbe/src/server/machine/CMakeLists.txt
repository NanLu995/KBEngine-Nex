
# 收集源文件
set(SRCS
	machine.cpp
	machine_interface.cpp
	main.cpp
)

# 创建可执行程序
add_executable(machine ${SRCS})


# 链接依赖的库
target_link_libraries(machine
    PRIVATE
        server
        resmgr
        network
        thread
        pyscript
        common
        helper
        math
        g3dlite
        log4cxx

)

target_compile_definitions(machine PUBLIC  LOG4CXX_STATIC)


# ---------------- Python ----------------
target_link_libraries(machine PRIVATE python_interface)
if(NOT APPLE)
    target_link_options(machine PRIVATE -Wl,-export-dynamic)
endif()

# ---------------- OpenSSL ----------------
target_link_libraries(machine PRIVATE KBEOpenSSL::SSL)


# 设置可执行文件输出目录
set_target_properties(machine PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${KBE_ROOT}/kbe/bin/server"
)