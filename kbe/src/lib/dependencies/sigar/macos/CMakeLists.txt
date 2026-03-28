project(sigar C)

set(SRCS
    sigar_macos.c
)

add_library(sigar STATIC ${SRCS})

target_include_directories(sigar PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
