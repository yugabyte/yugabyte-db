# Copyright (c) YugaByte, Inc.

add_subdirectory(util)
add_subdirectory(ptree)
add_subdirectory(parser)
add_subdirectory(sem)
add_subdirectory(exec)
add_subdirectory(test)

add_library(ybsql_api statement.cc sql_processor.cc ybsql.cc)
target_link_libraries(ybsql_api
                      ybsql_parser
                      ybsql_sem
                      ybsql_exec
                      ybsql_util)
