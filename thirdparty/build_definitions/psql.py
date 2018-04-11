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

class PSQLDependency(Dependency):
    def __init__(self):
        super(PSQLDependency, self).__init__(
                'psql', '10.3', 'https://ftp.postgresql.org/pub/source/v{0}/postgresql-{0}.tar.gz',
                BUILD_GROUP_COMMON)
        self.dir = 'postgresql-{}'.format(self.version)
        self.copy_sources = True

    def build(self, builder):
        log_prefix = builder.log_prefix(self)
        args = ['./configure', '--prefix={}'.format(builder.prefix)]
        log_output(log_prefix, args)
        # Parallel build doesn't work for postgresql, fails intermittently.
        log_output(log_prefix, ['make', '-C', 'src/interfaces/libpq', 'install'])
        log_output(log_prefix, ['make', '-C', 'src/bin/psql', 'install'])
