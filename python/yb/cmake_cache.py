# Copyright (c) Yugabyte, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations
# under the License.


import re
from typing import Dict, Tuple, Any


CMAKE_CACHE_VAR_RE = re.compile(r'^([a-zA-Z_0-9-]+):([A-Z]+)=(.*)$')


class CMakeCache:
    name_to_val_and_type: Dict[str, Tuple[str, str]] = {}

    def __init__(self, path: str) -> None:
        self.name_to_val_and_type = {}
        with open(path) as input_file:
            for line in input_file:
                line = line.strip()
                if line.startswith(('#', '//')) or not line:
                    continue
                m = CMAKE_CACHE_VAR_RE.match(line)
                if not m:
                    raise ValueError(f'Invalid line in CMake Cache at {path}: {line}')
                self.name_to_val_and_type[m.group(1)] = (m.group(3), m.group(2))

    def get(self, key: str, default_value: Any = None) -> Any:
        if key not in self.name_to_val_and_type:
            return default_value
        value, type_name = self.name_to_val_and_type[key]
        if type_name == 'BOOL':
            assert value in ['ON', 'OFF']
            return value == 'ON'
        return value
