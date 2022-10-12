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
# Find the Google Test Framework, heavily cribbed from FindGTest.cmake.
#
# This module defines
# GMOCK_INCLUDE_DIR, where to find gmock include files, etc.
# GTEST_INCLUDE_DIR, where to find gtest include files
# GMOCK_SHARED_LIBRARY, Location of libgmock's shared library
# GMOCK_STATIC_LIBRARY, Location of libgmock's static library
# GMOCK_FOUND, If false, do not try to use gmock.

#
# The following only applies to changes made to this file as part of YugaByte development.
#
# Portions Copyright (c) YugaByte, Inc.
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

find_library(GTEST_SHARED_LIBRARY gtest
             DOC   "Google's framework for writing C++ tests (gtest)"
             NO_CMAKE_SYSTEM_PATH
             NO_SYSTEM_ENVIRONMENT_PATH)

find_library(GTEST_STATIC_LIBRARY libgtest.a
             DOC   "Google's framework for writing C++ tests (gtest) static"
             NO_CMAKE_SYSTEM_PATH
             NO_SYSTEM_ENVIRONMENT_PATH)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GTEST REQUIRED_VARS
  GTEST_SHARED_LIBRARY GTEST_STATIC_LIBRARY
  GMOCK_SHARED_LIBRARY GMOCK_STATIC_LIBRARY
  GMOCK_INCLUDE_DIR GTEST_INCLUDE_DIR)
