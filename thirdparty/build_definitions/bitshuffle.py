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

class BitShuffleDependency(Dependency):
    def __init__(self):
        super(BitShuffleDependency, self).__init__(
                'bitshuffle', '55f9b4c', None, BUILD_GROUP_COMMON)
        self.copy_sources = True

    def build(self, builder):
        log_prefix = builder.log_prefix(self)
        compile_command = [builder.get_c_compiler(), '-std=c99',
                           '-I{}/include'.format(builder.prefix),
                           '-O3', '-DNDEBUG', '-fPIC', '-c',
                           'src/bitshuffle_core.c', 'src/bitshuffle.c',
                           'src/iochain.c']
        compile_command += builder.compiler_flags + builder.c_flags
        log_output(log_prefix, compile_command)
        log_output(log_prefix, ['ar', 'rs', 'bitshuffle.a',
                                'bitshuffle_core.o', 'bitshuffle.o', 'iochain.o'])
        log_output(log_prefix, ['cp', '-f', 'bitshuffle.a', builder.prefix_lib])
        log_output(log_prefix, ['cp', '-f', 'src/bitshuffle.h', builder.prefix_include])
        log_output(log_prefix, ['cp', '-f', 'src/bitshuffle_core.h', builder.prefix_include])
