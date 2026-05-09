
# 收集源文件
set(SRCS
    logger.cpp
	logger_interface.cpp
	logwatcher.cpp
	profile.cpp
	main.cpp
)

# 创建可执行程序
add_executable(logger ${SRCS})


# 链接依赖的库
target_link_libraries(logger
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

target_compile_definitions(logger PUBLIC  LOG4CXX_STATIC)


# ---------------- Python ----------------
target_link_libraries(logger PRIVATE python_interface)
if(NOT APPLE)
    target_link_options(logger PRIVATE -Wl,-export-dynamic)
endif()

# ---------------- OpenSSL ----------------
target_link_libraries(logger PRIVATE KBEOpenSSL::SSL)


# 设置可执行文件输出目录
set_target_properties(logger PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${KBE_ROOT}/kbe/bin/server"
)