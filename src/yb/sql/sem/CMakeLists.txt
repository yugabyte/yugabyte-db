# Copyright (c) YugaByte, Inc.

add_library(ybsql_sem
            analyzer.cc)

target_link_libraries(ybsql_sem
                      ybsql_parser
                      yb_util)
