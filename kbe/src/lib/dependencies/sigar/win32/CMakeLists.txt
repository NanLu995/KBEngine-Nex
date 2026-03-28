project(sigar C CXX)

set(SRCS
    sigar.c
    sigar_cache.c
    sigar_fileinfo.c
    sigar_format.c
    sigar_getline.c
    sigar_ptql.c
    sigar_signal.c
    sigar_util.c
    win32_sigar.c
    peb.c
    wmi.cpp
)

add_library(sigar STATIC ${SRCS})

target_include_directories(sigar PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
    target_compile_definitions(sigar PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(sigar PUBLIC iphlpapi psapi wbemuuid ws2_32)
endif()
