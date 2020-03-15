#
# Copyright (c) YugaByte, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License. You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied. See the License for the specific language governing permissions and limitations
# under the License.
#

import os
import sys

sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from build_definitions import *

# C++ Cassandra driver
class CassandraCppDriverDependency(Dependency):
    def __init__(self):
        super(CassandraCppDriverDependency, self).__init__(
                'cassandra-cpp-driver', '2.9.0-yb-6',
                'https://github.com/YugaByte/cassandra-cpp-driver/archive/{0}.tar.gz',
                BUILD_GROUP_INSTRUMENTED)
        self.copy_sources = False
        self.patch_version = 0
        self.patch_strip = 1

    def build(self, builder):
        cxx_flags = []
        if not is_mac():
            cxx_flags = builder.compiler_flags + builder.cxx_flags + builder.ld_flags
            builder.add_checked_flag(cxx_flags, '-Wno-error=implicit-fallthrough')
            builder.add_checked_flag(cxx_flags, '-Wno-error=class-memaccess')
            builder.add_checked_flag(cxx_flags, '-Wno-error=pedantic')

        builder.build_with_cmake(
                self,
                ['-DCMAKE_BUILD_TYPE={}'.format(builder.cmake_build_type()),
                 '-DCMAKE_POSITION_INDEPENDENT_CODE=On',
                 '-DCMAKE_INSTALL_PREFIX={}'.format(builder.prefix),
                 '-DBUILD_SHARED_LIBS=On'] +
                (['-DCMAKE_CXX_FLAGS=' + ' '.join(cxx_flags)] if not is_mac() else []) +
                    get_openssl_related_cmake_args())

        if is_mac():
          lib_file = 'libcassandra.' + builder.dylib_suffix
          path = os.path.join(builder.prefix_lib, lib_file)
          log_output(builder.log_prefix(self),
                     ['install_name_tool', '-id', '@rpath/' + lib_file, path])
