# ---------------- 源文件 ----------------
set(SRCS
    crashhandler.cpp
    debug_helper.cpp
    debug_option.cpp
    eventhistory_stats.cpp
    profile.cpp
    profiler.cpp
    profile_handler.cpp
    script_loglevel.cpp
    sys_info.cpp
    watch_pools.cpp
    watcher.cpp
)

add_library(helper STATIC ${SRCS})

target_include_directories(helper
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

# ---------------- 依赖库 ----------------
target_link_libraries(helper PUBLIC fmt::fmt sigar)


# ---------------- log4cxx ----------------
if(NOT NO_USE_LOG4CXX)
    target_link_libraries(helper PUBLIC
        log4cxx
        ${APR_LIBRARIES}
        ${APRUTIL_LIBRARIES}
		expat::expat
    )
    target_compile_definitions(helper PUBLIC LOG4CXX_STATIC)
else()
    target_compile_definitions(helper PUBLIC NO_USE_LOG4CXX)
endif()
