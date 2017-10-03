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
# Find the Google Mock Framework, heavily cribbed from FindGTest.cmake.
# gmock ships a copy of gtest and bundles it in its libraries, so this also
# finds the gtest headers.
#
# This module defines
# GMOCK_INCLUDE_DIR, where to find gmock include files, etc.
# GTEST_INCLUDE_DIR, where to find gtest include files
# GMOCK_SHARED_LIBRARY, Location of libgmock's shared library
# GMOCK_STATIC_LIBRARY, Location of libgmock's static library
# GMOCK_FOUND, If false, do not try to use gmock.

find_path(GMOCK_INCLUDE_DIR gmock/gmock.h
          DOC   "Path to the gmock header file"
          NO_CMAKE_SYSTEM_PATH
          NO_SYSTEM_ENVIRONMENT_PATH)

find_path(GTEST_INCLUDE_DIR gtest/gtest.h
          DOC   "Path to the gtest header file"
          NO_CMAKE_SYSTEM_PATH
          NO_SYSTEM_ENVIRONMENT_PATH)

find_library(GMOCK_SHARED_LIBRARY gmock
             DOC   "Google's framework for writing C++ tests (gmock)"
             NO_CMAKE_SYSTEM_PATH
             NO_SYSTEM_ENVIRONMENT_PATH)

find_library(GMOCK_STATIC_LIBRARY libgmock.a
             DOC   "Google's framework for writing C++ tests (gmock) static"
             NO_CMAKE_SYSTEM_PATH
             NO_SYSTEM_ENVIRONMENT_PATH)


include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GMOCK REQUIRED_VARS
  GMOCK_SHARED_LIBRARY GMOCK_STATIC_LIBRARY GMOCK_INCLUDE_DIR GTEST_INCLUDE_DIR)
