# Copyright (c) 2009-2010 Volvox Development Team
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#
# Author: Konstantin Lepa <konstantin.lepa@gmail.com>
#
# Find the Google Test Framework
#
# This module defines
# GTEST_INCLUDE_DIR, where to find gtest include files, etc.
# GTest_FOUND, If false, do not try to use gtest.
# GTEST_STATIC_LIBRARY, Location of libgtest.a
# GTEST_SHARED_LIBRARY, Location of libttest's shared library

# also defined, but not for general use are
# GTEST_LIBRARY, where to find the GTest library.

set(GTEST_SEARCH_PATH ${CMAKE_SOURCE_DIR}/thirdparty/gtest-1.7.0)

set(GTEST_H gtest/gtest.h)

find_path(GTEST_INCLUDE_DIR ${GTEST_H}
  PATHS ${GTEST_SEARCH_PATH}/include
        NO_DEFAULT_PATH
  DOC   "Path to the ${GTEST_H} file"
)

find_library(GTEST_LIBRARY
  NAMES gtest
  PATHS ${GTEST_SEARCH_PATH}
        NO_DEFAULT_PATH
  DOC   "Google's framework for writing C++ tests (gtest)"
)

# Kudu does not use the gtest_main library (we have kudu_test_main).
#find_library(GTEST_MAIN_LIBRARY_PATH
#  NAMES gtest_main
#  PATHS GTEST_SEARCH_PATH
#        NO_DEFAULT_PATH
#  DOC   "Google's framework for writing C++ tests (gtest_main)"
#)
set(GTEST_LIB_NAME libgtest)
if(GTEST_INCLUDE_DIR AND GTEST_LIBRARY)
  set(GTEST_STATIC_LIBRARY ${GTEST_SEARCH_PATH}/${GTEST_LIB_NAME}.a)
  if(EXISTS "${GTEST_STATIC_LIBRARY}")
    set(GTEST_FOUND TRUE)
  endif()

  set(GTEST_SHARED_LIBRARY ${GTEST_SEARCH_PATH}/${GTEST_LIB_NAME}${CMAKE_SHARED_LIBRARY_SUFFIX})
  if(EXISTS "${GTEST_SHARED_LIBRARY}")
    set(GTEST_FOUND TRUE)
  endif()
else()
  set(GTEST_FOUND FALSE)
endif()

if(GTEST_FOUND)
  if(NOT GTest_FIND_QUIETLY)
    message(STATUS "Found the GTest library: ${GTEST_STATIC_LIBRARY} ${GTEST_SHARED_LIBRARY}")
  endif(NOT GTest_FIND_QUIETLY)
else(GTEST_FOUND)
  if(NOT GTest_FIND_QUIETLY)
    if(GTest_FIND_REQUIRED)
      message(FATAL_ERROR "Could not find the GTest library")
    else(GTest_FIND_REQUIRED)
      message(STATUS "Could not find the GTest library")
    endif(GTest_FIND_REQUIRED)
  endif(NOT GTest_FIND_QUIETLY)
endif(GTEST_FOUND)

mark_as_advanced(
  GTEST_INCLUDE_DIR
  GTEST_STATIC_LIBRARY
  GTEST_SHARED_LIBRARY)

