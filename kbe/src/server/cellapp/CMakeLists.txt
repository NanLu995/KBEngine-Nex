
# 收集源文件
set(SRCS
	all_clients.cpp
	view_trigger.cpp
	cell.cpp
	cells.cpp
	cellapp.cpp
	cellapp_interface.cpp
	clients_remote_entity_method.cpp
	controller.cpp
	controllers.cpp
	client_entity.cpp
	client_entity_method.cpp
	forward_message_over_handler.cpp
	entity.cpp
	entityref.cpp
	entity_remotemethod.cpp
	entity_coordinate_node.cpp
	entity_component.cpp
	ghost_manager.cpp
	history_event.cpp
	initprogress_handler.cpp
	loadnavmesh_threadtasks.cpp
	main.cpp
	space.cpp
	spacememory.cpp
	spacememorys.cpp
	space_viewer.cpp
	move_controller.cpp
	moveto_entity_handler.cpp
	moveto_point_handler.cpp
	navigate_handler.cpp
	profile.cpp
	proximity_controller.cpp
	coordinate_node.cpp
	coordinate_system.cpp
	rotator_handler.cpp
	range_trigger.cpp
	range_trigger_node.cpp
	real_entity_method.cpp
	trap_trigger.cpp
	turn_controller.cpp
	updatable.cpp
	updatables.cpp
	watch_obj_pools.cpp
	witness.cpp
	witnessed_timeout_handler.cpp
)

# 创建可执行程序
add_executable(cellapp ${SRCS})



# link_directories(${KBE_ROOT}/kbe/src/build/vcpkg_installed/x64-linux/lib)

# 链接依赖的库
target_link_libraries(cellapp
    PRIVATE
        server
        resmgr
        entitydef
        network
        thread
        pyscript
        common
        helper
		navigation
		tmxparser
        math
        g3dlite
        log4cxx
)

target_compile_definitions(cellapp PUBLIC  LOG4CXX_STATIC)


# ---------------- Python ----------------
target_link_libraries(cellapp PRIVATE python_interface)
if(NOT APPLE)
    target_link_options(cellapp PRIVATE -Wl,-export-dynamic)
endif()

# ---------------- OpenSSL ----------------
target_link_libraries(cellapp PRIVATE KBEOpenSSL::SSL)


# 设置可执行文件输出目录
set_target_properties(cellapp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${KBE_ROOT}/kbe/bin/server"
)