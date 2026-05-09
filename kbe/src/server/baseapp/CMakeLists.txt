
# 收集源文件
set(SRCS
	archiver.cpp
	entity.cpp
	entity_component.cpp
	baseapp.cpp
	baseapp_interface.cpp
	backuper.cpp
	entity_messages_forward_handler.cpp
	data_download.cpp
	data_downloads.cpp
	entity_autoloader.cpp
	entity_remotemethod.cpp
	forward_message_over_handler.cpp
	initprogress_handler.cpp
	restore_entity_handler.cpp
	sync_entitystreamtemplate_handler.cpp
	space.cpp
	main.cpp
	proxy.cpp
	proxy_forwarder.cpp
	profile.cpp
)

# 创建可执行程序
add_executable(baseapp ${SRCS})

# 链接依赖的库
target_link_libraries(baseapp
    PRIVATE
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

target_compile_definitions(baseapp PUBLIC  LOG4CXX_STATIC)


# ---------------- Python ----------------
target_link_libraries(baseapp PRIVATE python_interface)
if(NOT APPLE)
    target_link_options(baseapp PRIVATE -Wl,-export-dynamic)
endif()

# ---------------- OpenSSL ----------------
target_link_libraries(baseapp PRIVATE KBEOpenSSL::SSL)

# 设置可执行文件输出目录
set_target_properties(baseapp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${KBE_ROOT}/kbe/bin/server"
)