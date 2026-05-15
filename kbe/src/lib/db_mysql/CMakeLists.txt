# ---------------- 源文件 ----------------
set(SRCS
	common.cpp
	db_exception.cpp
	db_transaction.cpp
	db_interface_mysql.cpp
	entity_table_mysql.cpp
	entity_sqlstatement_mapping.cpp
	kbe_table_mysql.cpp
)
# ---------------- 创建静态库 ----------------
add_library(db_mysql STATIC ${SRCS})

target_link_libraries(db_mysql PUBLIC 
	fmt::fmt
	python_interface
)

function(kbe_find_static_library OUT_VAR LIB_NAME)
	set(_old_suffixes ${CMAKE_FIND_LIBRARY_SUFFIXES})
	set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
	find_library(_static_lib_${LIB_NAME}
		NAMES ${LIB_NAME}
		PATHS ${ARGN}
		NO_DEFAULT_PATH
	)
	if(NOT _static_lib_${LIB_NAME})
		find_library(_static_lib_${LIB_NAME}
			NAMES ${LIB_NAME}
		)
	endif()
	set(CMAKE_FIND_LIBRARY_SUFFIXES ${_old_suffixes})
	set(${OUT_VAR} "${_static_lib_${LIB_NAME}}" PARENT_SCOPE)
endfunction()

# MySQL client link setup:
# - Windows keeps existing pragma/lib behavior in source.
# - Unix/macOS resolves client libs via mysql_config/mariadb_config, then pkg-config fallback.
if(NOT WIN32)
	find_program(MYSQL_CONFIG_EXECUTABLE
		NAMES mysql_config mariadb_config
		PATHS
			/opt/homebrew/opt/mysql-client/bin
			/usr/local/opt/mysql-client/bin
			/opt/homebrew/bin
			/usr/local/bin
			/usr/bin
	)

	if(MYSQL_CONFIG_EXECUTABLE)
		execute_process(
			COMMAND ${MYSQL_CONFIG_EXECUTABLE} --cflags
			OUTPUT_VARIABLE MYSQL_CFLAGS
			OUTPUT_STRIP_TRAILING_WHITESPACE
		)
		execute_process(
			COMMAND ${MYSQL_CONFIG_EXECUTABLE} --libs
			OUTPUT_VARIABLE MYSQL_LIBS
			OUTPUT_STRIP_TRAILING_WHITESPACE
		)

		separate_arguments(MYSQL_CFLAGS UNIX_COMMAND "${MYSQL_CFLAGS}")
		separate_arguments(MYSQL_LIBS UNIX_COMMAND "${MYSQL_LIBS}")

		set(MYSQL_LIBRARY_DIRS)
		set(MYSQL_LINK_LIBS)
		foreach(MYSQL_LIB ${MYSQL_LIBS})
			if(MYSQL_LIB MATCHES "^-L(.+)")
				list(APPEND MYSQL_LIBRARY_DIRS "${CMAKE_MATCH_1}")
			endif()
		endforeach()

		foreach(MYSQL_LIB ${MYSQL_LIBS})
			if(MYSQL_LIB MATCHES "^-L(.+)")
				continue()
			elseif(MYSQL_LIB MATCHES "^-l(.+)")
				set(_lib_name "${CMAKE_MATCH_1}")
				if(_lib_name MATCHES "^(ssl|crypto)$")
					# OpenSSL is already provided by KBEOpenSSL::SSL. Linking the
					# system static OpenSSL from mysql_config in addition to vcpkg
					# OpenSSL causes duplicate symbols on Linux.
					continue()
				elseif(_lib_name MATCHES "^(mysqlclient|mariadb|zstd)$")
					kbe_find_static_library(_static_lib "${_lib_name}" ${MYSQL_LIBRARY_DIRS})
					if(_static_lib)
						list(APPEND MYSQL_LINK_LIBS "${_static_lib}")
					else()
						list(APPEND MYSQL_LINK_LIBS "${MYSQL_LIB}")
					endif()
				else()
					list(APPEND MYSQL_LINK_LIBS "${MYSQL_LIB}")
				endif()
			else()
				list(APPEND MYSQL_LINK_LIBS "${MYSQL_LIB}")
			endif()
		endforeach()

		target_compile_options(db_mysql PRIVATE ${MYSQL_CFLAGS})
		target_link_directories(db_mysql PUBLIC ${MYSQL_LIBRARY_DIRS})
		target_link_libraries(db_mysql PUBLIC ${MYSQL_LINK_LIBS})

		# Homebrew mysql-client may emit -lzstd but not provide the zstd search path.
		if(APPLE)
			kbe_find_static_library(ZSTD_LIBRARY zstd /opt/homebrew/lib /usr/local/lib)
			if(ZSTD_LIBRARY)
				get_filename_component(ZSTD_LIBRARY_DIR "${ZSTD_LIBRARY}" DIRECTORY)
				target_link_directories(db_mysql PUBLIC ${ZSTD_LIBRARY_DIR})
				target_link_libraries(db_mysql PUBLIC ${ZSTD_LIBRARY})
			endif()
		endif()
	else()
		find_package(PkgConfig QUIET)
		if(PkgConfig_FOUND)
			pkg_check_modules(MYSQLCLIENT QUIET mysqlclient)
			if(NOT MYSQLCLIENT_FOUND)
				pkg_check_modules(MYSQLCLIENT QUIET libmariadb)
			endif()
			if(NOT MYSQLCLIENT_FOUND)
				pkg_check_modules(MYSQLCLIENT QUIET mariadb)
			endif()
		endif()

		if(MYSQLCLIENT_FOUND)
			target_include_directories(db_mysql PUBLIC ${MYSQLCLIENT_INCLUDE_DIRS})
			target_compile_options(db_mysql PRIVATE ${MYSQLCLIENT_CFLAGS_OTHER})
			target_link_libraries(db_mysql PUBLIC ${MYSQLCLIENT_LINK_LIBRARIES})
			target_link_options(db_mysql PUBLIC ${MYSQLCLIENT_LDFLAGS_OTHER})
		else()
			message(FATAL_ERROR "MySQL client library not found. Install mysql-client (or MariaDB connector) and ensure mysql_config or pkg-config is available.")
		endif()
	endif()
endif()
