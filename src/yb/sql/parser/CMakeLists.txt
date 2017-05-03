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
SET(PARSER_GRAM_Y_CC_FINAL "${PARSER_GRAM_Y}.final.cc")
SET(PARSER_GRAM_Y_HH_FINAL "${PARSER_GRAM_Y}.final.hh")

SET(SCANNER_LEX_L "${CMAKE_CURRENT_BINARY_DIR}/scanner_lex.l")
SET(SCANNER_LEX_L_CC "${SCANNER_LEX_L}.cc")

# Regular expressions used with the sed tool.
set(PARSER_GRAM_Y_CC_SED_PATTERN
    "s%parser_gram.y.hh%parser_gram.y.final.hh%g\;")
set(PARSER_GRAM_Y_HH_SED_PATTERN
    "s%new [(]yyas_<T> [(][)][)] T\;%new (yyas_<T> ()) T()\;%g")

# FLEX_LIBRARIES points to libfl, causing an "undefined reference to yylex" error, and it looks like
# linking against libfl is not the right thing to do in recent versions of Flex.
# https://github.com/brianb/mdbtools/issues/47
set(FLEX_LIBRARIES "")

# Run BISON and FLEX
BISON_TARGET(YbSqlParser parser_gram.y ${PARSER_GRAM_Y_CC})
FLEX_TARGET(YbSqlScanner scanner_lex.l ${SCANNER_LEX_L_CC})
ADD_FLEX_BISON_DEPENDENCY(YbSqlScanner YbSqlParser)

# Finalize BISON files. Tell cmake to patch the files through this custom command.
# Note: BISON files (in BISON_YbSqlParser_OUTPUTS): parser_gram.y.cc/hh
add_custom_command(
  OUTPUT ${PARSER_GRAM_Y_CC_FINAL} ${PARSER_GRAM_Y_HH_FINAL}
  DEPENDS ${BISON_YbSqlParser_OUTPUTS}
  COMMAND sed -e '${PARSER_GRAM_Y_CC_SED_PATTERN}' ${PARSER_GRAM_Y_CC} > ${PARSER_GRAM_Y_CC_FINAL}
  COMMAND sed -e '${PARSER_GRAM_Y_HH_SED_PATTERN}' ${PARSER_GRAM_Y_HH} > ${PARSER_GRAM_Y_HH_FINAL})

set(PARSER_CUSTOM_FLAGS "-Wno-tautological-undefined-compare -Wno-implicit-fallthrough")

set_source_files_properties(${SCANNER_LEX_L_CC} ${PARSER_GRAM_Y_CC_FINAL} PROPERTIES
                            COMPILE_FLAGS ${PARSER_CUSTOM_FLAGS})

add_library(ybsql_parser
            parser.cc
            parse_context.cc
            scanner.cc
            scanner_util.cc
            ${PARSER_GRAM_Y_CC_FINAL} ${PARSER_GRAM_Y_HH_FINAL}
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
