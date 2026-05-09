
# 收集源文件
set(SRCS
    bots.cpp
	bots_interface.cpp
	clientobject.cpp
	create_and_login_handler.cpp
	profile.cpp
	main.cpp
	pybots.cpp
	kcp_packet_receiver_ex.cpp
	kcp_packet_sender_ex.cpp
	tcp_packet_receiver_ex.cpp
	tcp_packet_sender_ex.cpp
	bots_active_report_handler.cpp
)

# 创建可执行程序
add_executable(bots ${SRCS})


# 链接依赖的库
target_link_libraries(bots
    PRIVATE
        client_lib
        server
        resmgr
        entitydef
        network
        thread
        pyscript
        common
        helper
        math
        g3dlite
        log4cxx

)

target_compile_definitions(bots PUBLIC  LOG4CXX_STATIC)


# ---------------- Python ----------------
target_link_libraries(bots PRIVATE python_interface)
if(NOT APPLE)
    target_link_options(bots PRIVATE -Wl,-export-dynamic)
endif()

# ---------------- OpenSSL ----------------
target_link_libraries(bots PRIVATE KBEOpenSSL::SSL)


# 设置可执行文件输出目录
set_target_properties(bots PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${KBE_ROOT}/kbe/bin/server"
)