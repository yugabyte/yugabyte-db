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

class SqueaselDependency(Dependency):
    def __init__(self):
        super(SqueaselDependency, self).__init__(
                'squeasel', '8ac777a122fccf0358cb8562e900f8e9edd9ed11', None, BUILD_GROUP_COMMON)
        self.copy_sources = True

    def build(self, builder):
        log_prefix = builder.log_prefix(self)
        compile_command = [builder.get_c_compiler(), '-std=c99', '-O3', '-DNDEBUG', '-fPIC', '-c',
                           'squeasel.c']
        compile_command += builder.compiler_flags + builder.c_flags
        log_output(log_prefix, compile_command)
        log_output(log_prefix, ['ar', 'rs', 'libsqueasel.a', 'squeasel.o'])
        log_output(log_prefix, ['cp', 'libsqueasel.a', builder.prefix_lib])
        log_output(log_prefix, ['cp', 'squeasel.h', builder.prefix_include])
