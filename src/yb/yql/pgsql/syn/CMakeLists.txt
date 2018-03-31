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

if (APPLE)
  set(CMAKE_PREFIX_PATH "/usr/local/opt/flex" ${CMAKE_PREFIX_PATH})
  set(CMAKE_PREFIX_PATH "/usr/local/opt/bison" ${CMAKE_PREFIX_PATH})
endif()

find_package(BISON)
find_package(FLEX)

SET(PG_GRAM_Y "${CMAKE_CURRENT_BINARY_DIR}/pg_gram.y")
SET(PG_GRAM_Y_CC "${PG_GRAM_Y}.cc")
SET(PG_GRAM_Y_HH "${PG_GRAM_Y}.hh")
SET(PG_GRAM_Y_CC_FINAL "${PG_GRAM_Y}.final.cc")
SET(PG_GRAM_Y_HH_FINAL "${PG_GRAM_Y}.final.hh")

SET(PG_LEX_L "${CMAKE_CURRENT_BINARY_DIR}/pg_lex.l")
SET(PG_LEX_L_CC "${PG_LEX_L}.cc")

# Regular expressions used with the sed tool.
set(PG_GRAM_Y_CC_SED_PATTERN
    "s%pg_gram.y.hh%pg_gram.y.final.hh%g\;")
set(PG_GRAM_Y_HH_SED_PATTERN
    "s%new [(]yyas_<T> [(][)][)] T\;%new (yyas_<T> ()) T()\;%g")

# FLEX_LIBRARIES points to libfl, causing an "undefined reference to yylex" error, and it looks like
# linking against libfl is not the right thing to do in recent versions of Flex.
# https://github.com/brianb/mdbtools/issues/47
set(FLEX_LIBRARIES "")

# Run BISON and FLEX
BISON_TARGET(PGPARSER pg_gram.y ${PG_GRAM_Y_CC})
FLEX_TARGET(PGSCANNER pg_lex.l ${PG_LEX_L_CC})
ADD_FLEX_BISON_DEPENDENCY(PGSCANNER PGPARSER)

# Finalize BISON files. Tell cmake to patch the files through this custom command.
# Note: BISON files (in BISON_PGPARSER_OUTPUTS): pg_gram.y.cc/hh
add_custom_command(
  OUTPUT ${PG_GRAM_Y_CC_FINAL} ${PG_GRAM_Y_HH_FINAL}
  DEPENDS ${BISON_PGPARSER_OUTPUTS}
  COMMAND sed -e '${PG_GRAM_Y_CC_SED_PATTERN}' ${PG_GRAM_Y_CC} > ${PG_GRAM_Y_CC_FINAL}
  COMMAND sed -e '${PG_GRAM_Y_HH_SED_PATTERN}' ${PG_GRAM_Y_HH} > ${PG_GRAM_Y_HH_FINAL})

set(PARSER_CUSTOM_FLAGS
    "-Wno-tautological-undefined-compare -Wno-implicit-fallthrough -Wno-null-conversion")

set_source_files_properties(${PG_LEX_L_CC} ${PG_GRAM_Y_CC_FINAL} PROPERTIES
                            COMPILE_FLAGS ${PARSER_CUSTOM_FLAGS})

add_library(pgsql-syn
            pg_parser.cc
            pg_scanner.cc
            pg_scan_util.cc
            ${PG_GRAM_Y_CC_FINAL} ${PG_GRAM_Y_HH_FINAL}
            ${PG_LEX_L_CC})

FIND_PATH(FLEX_INCLUDE_DIR FlexLexer.h)
target_include_directories(pgsql-syn PUBLIC ${FLEX_INCLUDE_DIR})
target_link_libraries(pgsql-syn
                      ${FLEX_LIBRARIES}
                      ${BISON_LIBRARIES}
                      pgsql-ptree
                      pgsql-util
                      yb_util)
add_dependencies(pgsql-syn pgsql-ptree)
