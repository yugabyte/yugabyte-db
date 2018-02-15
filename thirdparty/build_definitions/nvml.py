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

import multiprocessing
import os
import sys

sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from build_definitions import *

class NVMLDependency(Dependency):
    def __init__(self):
        super(NVMLDependency, self).__init__('nvml', '0.4-b2', None, BUILD_GROUP_COMMON)
        self.copy_sources = True

    def build(self, builder):
        log_prefix = builder.log_prefix(self)

        os.chdir('src')

        # The embedded jemalloc build doesn't pick up the EXTRA_CFLAGS environment
        # variable, so we have to stick our flags into this config file.
        with open('jemalloc/jemalloc.cfg', 'rt') as inp:
            lines = inp.readlines()

        with open('jemalloc/jemalloc.cfg', 'wt') as out:
            for line in lines:
                if not line.startswith('EXTRA_CFLAGS'):
                    out.write(line)
            out.write('EXTRA_CFLAGS="-DJEMALLOC_LIBVMEM {}"'.format(
                    " ".join(builder.compiler_flags + builder.c_flags)))

        log_output(log_prefix, ['make', 'libvmem', 'DEBUG=0',
                                '-j{}'.format(multiprocessing.cpu_count())])

        # NVML doesn't allow configuring PREFIX -- it always installs into
        # DESTDIR/usr/lib. Additionally, the 'install' target builds all of
        # the NVML libraries, even though we only need libvmem.
        # So, we manually install the built artifacts.
        log_output(log_prefix, ['cp', '-a', 'include/libvmem.h', builder.prefix_include])
        log_output(log_prefix, ['cp', '-a', 'nondebug/libvmem.a', builder.prefix_lib])
        log_output(log_prefix, ['cp', '-a', 'nondebug/libvmem.so', builder.prefix_lib])
        log_output(log_prefix, ['cp', '-a', 'nondebug/libvmem.so.1', builder.prefix_lib])
        log_output(log_prefix, ['cp', '-a', 'nondebug/libvmem.so.1.0.0', builder.prefix_lib])
