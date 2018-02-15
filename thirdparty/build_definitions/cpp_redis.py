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

class CppRedisDependency(Dependency):
    def __init__(self):
        super(CppRedisDependency, self).__init__(
                'cpp_redis', '4.3.0', "https://github.com/Cylix/cpp_redis/archive/{0}.zip",
                BUILD_GROUP_INSTRUMENTED)
        self.copy_sources = False

    def build(self, builder):
        out_dir = os.path.join(builder.prefix, 'cpp_redis')
        builder.build_with_cmake(self,
                                 ['-DCMAKE_BUILD_TYPE=Release',
                                  '-DCMAKE_INSTALL_PREFIX={}'.format(out_dir),
                                  '-DCMAKE_PREFIX_PATH={}'.format(builder.prefix)])
        os.rename(os.path.join(out_dir, 'lib', 'lib', 'libcpp_redis.a'),
                  os.path.join(builder.prefix_lib, 'libcpp_redis.a'))
        remove_path(os.path.join(builder.prefix_include, 'cpp_redis'))
        os.rename(os.path.join(out_dir, 'include', 'cpp_redis'),
                  os.path.join(builder.prefix_include, 'cpp_redis'))
