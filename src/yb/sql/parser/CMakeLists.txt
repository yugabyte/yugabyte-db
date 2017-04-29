# Copyright (c) YugaByte, Inc.

if (APPLE)
  set(CMAKE_PREFIX_PATH "/usr/local/opt/flex" ${CMAKE_PREFIX_PATH})
  set(CMAKE_PREFIX_PATH "/usr/local/opt/bison" ${CMAKE_PREFIX_PATH})
endif()

find_package(BISON)
find_package(FLEX)

SET(PARSER_GRAM_Y "${CMAKE_CURRENT_BINARY_DIR}/parser_gram.y")
SET(PARSER_GRAM_Y_CC "${PARSER_GRAM_Y}.cc")
SET(PARSER_GRAM_Y_HH "${PARSER_GRAM_Y}.hh")

SET(SCANNER_LEX_L "${CMAKE_CURRENT_BINARY_DIR}/scanner_lex.l")
SET(SCANNER_LEX_L_CC "${SCANNER_LEX_L}.cc")

# FLEX_LIBRARIES points to libfl, causing an "undefined reference to yylex" error, and it looks like
# linking against libfl is not the right thing to do in recent versions of Flex.
# https://github.com/brianb/mdbtools/issues/47
set(FLEX_LIBRARIES "")

# Run BISON and FLEX
BISON_TARGET(YbSqlParser parser_gram.y ${PARSER_GRAM_Y_CC})
FLEX_TARGET(YbSqlScanner scanner_lex.l ${SCANNER_LEX_L_CC})
ADD_FLEX_BISON_DEPENDENCY(YbSqlScanner YbSqlParser)

set(PARSER_CUSTOM_FLAGS "-Wno-tautological-undefined-compare -Wno-implicit-fallthrough")

set_source_files_properties(${SCANNER_LEX_L_CC} ${PARSER_GRAM_Y_CC} PROPERTIES
                            COMPILE_FLAGS ${PARSER_CUSTOM_FLAGS})

add_library(ybsql_parser
            parser.cc
            parse_context.cc
            scanner.cc
            scanner_util.cc
            ${PARSER_GRAM_Y_CC} ${PARSER_GRAM_Y_HH}
            ${SCANNER_LEX_L_CC})

FIND_PATH(FLEX_INCLUDE_DIR FlexLexer.h)
target_include_directories(ybsql_parser BEFORE PUBLIC ${FLEX_INCLUDE_DIR})
target_link_libraries(ybsql_parser
                      ${FLEX_LIBRARIES}
                      ${BISON_LIBRARIES}
                      ybsql_ptree
                      ybsql_util
                      yb_util)
add_dependencies(ybsql_parser ybsql_ptree)
