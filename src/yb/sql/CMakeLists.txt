# Copyright (c) YugaByte, Inc.

set(CMAKE_PREFIX_PATH "/usr/local/opt/flex" ${CMAKE_PREFIX_PATH})
set(CMAKE_PREFIX_PATH "/usr/local/opt/bison" ${CMAKE_PREFIX_PATH})

find_package(BISON)
find_package(FLEX)

BISON_TARGET(YbSqlParser parser/parser_gram.y ${CMAKE_CURRENT_SOURCE_DIR}/parser_gram.cpp)
FLEX_TARGET(YbSqlScanner parser/scanner_lex.l ${CMAKE_CURRENT_SOURCE_DIR}/scanner_lex.cpp)
ADD_FLEX_BISON_DEPENDENCY(YbSqlScanner YbSqlParser)

add_library(ybsql
  ${BISON_YbSqlParser_OUTPUTS}
  ${FLEX_YbSqlScanner_OUTPUTS}
  ybsql.cc
  parser/parser.cc
  parser/scanner.cc
  parser/scanner_util.cc)

target_link_libraries(ybsql yb_util ${FLEX_LIBRARIES} ${BISON_LIBRARIES})
