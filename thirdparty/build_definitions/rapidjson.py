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

class RapidJsonDependency(Dependency):
    def __init__(self):
        super(RapidJsonDependency, self).__init__(
                'rapidjson', '1.1.0', 'https://github.com/Tencent/rapidjson/archive/v{0}.zip',
                BUILD_GROUP_COMMON)
        self.copy_sources = False

    def build(self, builder):
        log_prefix = builder.log_prefix(self)
        log_output(log_prefix,
                   ['rsync', '-av', '--delete',
                    os.path.join(builder.source_path(self), 'include', 'rapidjson/'),
                    os.path.join(builder.prefix_include, 'rapidjson')])
