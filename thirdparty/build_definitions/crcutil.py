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

class CRCUtilDependency(Dependency):
    def __init__(self):
        super(CRCUtilDependency, self).__init__(
                'crcutil', '440ba7babeff77ffad992df3a10c767f184e946e', None,
                BUILD_GROUP_INSTRUMENTED)
        self.copy_sources = True
        self.patch_version = 2
        self.patch_strip = 0
        self.patches = ['crcutil-fix-libtoolize-on-osx.patch', 'crcutil-fix-offsetof.patch']

    def build(self, builder):
        log_prefix = builder.log_prefix(self)
        log_output(log_prefix, ['./autogen.sh'])
        builder.build_with_configure(log_prefix)
