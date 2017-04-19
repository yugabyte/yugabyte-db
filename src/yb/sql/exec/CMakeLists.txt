# Copyright (c) YugaByte, Inc.

add_library(ybsql_exec
            eval_bcall.cc
            eval_const.cc
            eval_expr.cc
            eval_logic.cc
            eval_op.cc
            exec_context.cc
            executor.cc)

target_link_libraries(ybsql_exec
                      ybsql_parser
                      yb_client
                      yb_util)
add_dependencies(ybsql_exec ybsql_parser)
