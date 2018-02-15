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
import subprocess
import sys

sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from build_definitions import *

class GMockDependency(Dependency):
    def __init__(self):
        super(GMockDependency, self).__init__(
                'gmock', '1.8.0', 'https://github.com/google/googletest/archive/release-{0}.tar.gz',
                BUILD_GROUP_INSTRUMENTED)
        self.dir = "googletest-release-{}".format(self.version)
        self.copy_sources = False

    def build(self, builder):
        self.do_build(builder, 'static')
        log("Installing gmock (static)")
        lib_dir = builder.prefix_lib
        include_dir = builder.prefix_include
        subprocess.check_call(['cp', '-a', 'static/googlemock/libgmock.a', lib_dir])
        self.do_build(builder, 'shared')
        log("Installing gmock (shared)")
        subprocess.check_call(
                ['cp', '-a', 'shared/googlemock/libgmock.{}'.format(builder.dylib_suffix), lib_dir])

        src_dir = builder.source_path(self)
        subprocess.check_call(
                ['rsync', '-av', os.path.join(src_dir, 'googlemock', 'include/'), include_dir])
        subprocess.check_call(
                ['rsync', '-av', os.path.join(src_dir, 'googletest', 'include/'), include_dir])

    def do_build(self, builder, mode):
        build_dir = os.path.join(os.getcwd(), mode)
        mkdir_if_missing(build_dir)
        with PushDir(build_dir):
            builder.build_with_cmake(
                    self,
                    ['-DCMAKE_BUILD_TYPE=Debug',
                     '-DCMAKE_POSITION_INDEPENDENT_CODE=On',
                     '-DBUILD_SHARED_LIBS={}'.format('ON' if mode == 'shared' else 'OFF')],
                    install=False)
