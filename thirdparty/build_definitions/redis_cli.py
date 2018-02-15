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

class RedisCliDependency(Dependency):
    def __init__(self):
        super(RedisCliDependency, self).__init__(
                'redis_cli', '4.0.1', 'https://github.com/YugaByte/redis/archive/{0}.tar.gz',
                BUILD_GROUP_COMMON)
        self.dir = 'redis-{}'.format(self.version)
        self.copy_sources = True

    def build(self, builder):
        log_prefix = builder.log_prefix(self)
        log_output(log_prefix, ['make', '-j{}'.format(multiprocessing.cpu_count()), 'redis-cli'])
        log_output(log_prefix, ['cp', 'src/redis-cli', builder.prefix_bin])
