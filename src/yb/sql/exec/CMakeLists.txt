# Copyright (c) YugaByte, Inc.

add_library(ybsql_exec
            executor.cc
            exec_context.cc
            eval_expr.cc)

target_link_libraries(ybsql_exec
                      ybsql_parser
                      yb_client
                      yb_util)
add_dependencies(ybsql_exec ybsql_parser)
