# ---------------- 源文件 ----------------
set(SRCS
	common.cpp
	db_exception.cpp
	db_interface_mongodb.cpp
	db_transaction.cpp
	entity_table_mongodb.cpp
	kbe_table_mongodb.cpp
)
# ---------------- 创建静态库 ----------------
add_library(db_mongodb STATIC ${SRCS})

target_link_libraries(db_mongodb PUBLIC 
	fmt::fmt
	mongo::static
	python_interface
)



