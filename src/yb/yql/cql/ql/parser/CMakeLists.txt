# Copyright (c) YugaByte, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations
# under the License.
#

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
BISON_TARGET(QLParser parser_gram.y ${PARSER_GRAM_Y_CC})
FLEX_TARGET(QLScanner scanner_lex.l ${SCANNER_LEX_L_CC})
ADD_FLEX_BISON_DEPENDENCY(QLScanner QLParser)

# Finalize BISON files. Tell cmake to patch the files through this custom command.
# Note: BISON files (in BISON_QLParser_OUTPUTS): parser_gram.y.cc/hh
add_custom_command(
  OUTPUT ${PARSER_GRAM_Y_CC_FINAL} ${PARSER_GRAM_Y_HH_FINAL}
  DEPENDS ${BISON_QLParser_OUTPUTS}
  COMMAND sed -e '${PARSER_GRAM_Y_CC_SED_PATTERN}' ${PARSER_GRAM_Y_CC} > ${PARSER_GRAM_Y_CC_FINAL}
  COMMAND sed -e '${PARSER_GRAM_Y_HH_SED_PATTERN}' ${PARSER_GRAM_Y_HH} > ${PARSER_GRAM_Y_HH_FINAL})

set(PARSER_CUSTOM_FLAGS
    "-Wno-tautological-undefined-compare -Wno-implicit-fallthrough -Wno-null-conversion")

set(PARSER_CUSTOM_FLAGS "${PARSER_CUSTOM_FLAGS} -Wno-shorten-64-to-32")
if(IS_CLANG AND "${COMPILER_VERSION}" VERSION_GREATER_EQUAL "14.0.0")
  set(PARSER_CUSTOM_FLAGS "${PARSER_CUSTOM_FLAGS} -Wno-unused-but-set-variable")
endif()
if(IS_GCC AND "${COMPILER_VERSION}" MATCHES "^13[.].*$")
  # To work around this issue:
  # https://gist.githubusercontent.com/mbautin/199bb4a5011148ae93afe38887bc120a/raw
  string(APPEND PARSER_CUSTOM_FLAGS " -Wno-nonnull-compare")
endif()

set_source_files_properties(${SCANNER_LEX_L_CC} ${PARSER_GRAM_Y_CC_FINAL} PROPERTIES
                            COMPILE_FLAGS ${PARSER_CUSTOM_FLAGS})

add_custom_target(
    ql_parser_flex_bison_output
    DEPENDS ${PARSER_GRAM_Y_CC_FINAL} ${PARSER_GRAM_Y_HH_FINAL} ${SCANNER_LEX_L_CC})

add_library(ql_parser
            parser.cc
            parse_context.cc
            scanner.cc
            scanner_util.cc
            ${PARSER_GRAM_Y_CC_FINAL} ${PARSER_GRAM_Y_HH_FINAL}
            ${SCANNER_LEX_L_CC})

FIND_PATH(FLEX_INCLUDE_DIR FlexLexer.h)
target_include_directories(ql_parser PUBLIC ${FLEX_INCLUDE_DIR})
target_link_libraries(ql_parser
                      ${FLEX_LIBRARIES}
                      ${BISON_LIBRARIES}
                      ql_ptree
                      ql_util
                      yb_util)
add_dependencies(ql_parser ql_ptree)

yb_use_pch(ql_parser ql)
