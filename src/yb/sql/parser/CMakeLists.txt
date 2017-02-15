# Copyright (c) YugaByte, Inc.

set(CMAKE_PREFIX_PATH "/usr/local/opt/flex" ${CMAKE_PREFIX_PATH})
set(CMAKE_PREFIX_PATH "/usr/local/opt/bison" ${CMAKE_PREFIX_PATH})

find_package(BISON)
find_package(FLEX)

# Define variables for quoted generated file names and use them in commands.
SET(PARSER_GRAM_Y_CC "${CMAKE_CURRENT_BINARY_DIR}/parser_gram.y.cc")
SET(PARSER_GRAM_FINAL_Y_CC "${CMAKE_CURRENT_BINARY_DIR}/parser_gram_final.y.cc")
SET(SCANNER_LEX_L_CC "${CMAKE_CURRENT_BINARY_DIR}/scanner_lex.l.cc")

# FLEX_LIBRARIES points to libfl, causing an "undefined reference to yylex" error, and it looks like
# linking against libfl is not the right thing to do in recent versions of Flex.
# https://github.com/brianb/mdbtools/issues/47
set(FLEX_LIBRARIES "")

# Run BISON and FLEX
BISON_TARGET(YbSqlParser parser_gram.y ${PARSER_GRAM_Y_CC})
FLEX_TARGET(YbSqlScanner scanner_lex.l ${SCANNER_LEX_L_CC})
ADD_FLEX_BISON_DEPENDENCY(YbSqlScanner YbSqlParser)

# Finalize BISON files. Tell cmake to create a new file PARSER_GRAM_FINAL_Y_CC, and trigger this
# custom command by making libybsql_parser depend on this new file.
list(REMOVE_ITEM BISON_YbSqlParser_OUTPUTS ${PARSER_GRAM_Y_CC})
add_custom_command(
  OUTPUT ${PARSER_GRAM_FINAL_Y_CC}
  DEPENDS ${PARSER_GRAM_Y_CC}
  COMMAND sed -e 's%// Fall through[.]%FALLTHROUGH_INTENDED\;%g' ${PARSER_GRAM_Y_CC}
            > ${PARSER_GRAM_FINAL_Y_CC})

add_library(ybsql_parser
            parser.cc
            parse_context.cc
            scanner.cc
            scanner_util.cc
            ${BISON_YbSqlParser_OUTPUTS}
            ${FLEX_YbSqlScanner_OUTPUTS}
            ${PARSER_GRAM_FINAL_Y_CC})

FIND_PATH(FLEX_INCLUDE_DIR FlexLexer.h)
target_include_directories(ybsql_parser BEFORE PUBLIC ${FLEX_INCLUDE_DIR})
target_link_libraries(ybsql_parser
                      ${FLEX_LIBRARIES}
                      ${BISON_LIBRARIES}
                      ybsql_ptree
                      ybsql_util
                      yb_util)
