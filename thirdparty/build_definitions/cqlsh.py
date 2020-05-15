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

class CQLShDependency(Dependency):
    def __init__(self):
        super(CQLShDependency, self).__init__(
                'cqlsh', '3.10-yb-6', 'https://github.com/YugaByte/cqlsh/archive/v{0}.tar.gz',
                BUILD_GROUP_COMMON)
        self.copy_sources = True

    def build(self, builder):
        self.sync(builder, 'bin')
        self.sync(builder, 'lib')
        self.sync(builder, 'pylib')

    def sync(self, builder, subdir):
        log_prefix = builder.log_prefix(self)
        out_dir = os.path.join(builder.prefix, 'cqlsh', subdir)
        mkdir_if_missing(out_dir)
        log_output(log_prefix, ['rsync', '-av', subdir + '/', out_dir + '/'])
