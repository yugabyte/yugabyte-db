# Copyright (c) YugaByte, Inc.

add_library(ybsql_util
            memory_context.cc
            errcodes.cc
            base_types.cc
            rows_result.cc
            sql_env.cc)

target_link_libraries(ybsql_util
                      yb_client
                      yb_util)
