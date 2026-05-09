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

		target_compile_options(db_mysql PRIVATE ${MYSQL_CFLAGS})
		target_link_libraries(db_mysql PUBLIC ${MYSQL_LIBS})

		# Homebrew mysql-client may emit -lzstd but not provide the zstd search path.
		if(APPLE)
			find_library(ZSTD_LIBRARY NAMES zstd
				PATHS /opt/homebrew/lib /usr/local/lib
			)
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
