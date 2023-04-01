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
import os

from typing import Dict, Tuple, Any, Set, FrozenSet, Optional


CMAKE_CACHE_VAR_RE = re.compile(r'^([a-zA-Z_0-9-]+):([A-Z]+)=(.*)$')

# Support some subset of true-like and false-like values from
# https://cmake.org/cmake/help/latest/command/if.html
CMAKE_TRUE_VALUES: FrozenSet[str] = frozenset({'1', 'ON', 'YES', 'TRUE', 'Y'})
CMAKE_FALSE_VALUES: FrozenSet[str] = frozenset(
    {'0', 'OFF', 'NO', 'FALSE', 'N', 'IGNORE', 'NOTFOUND', ''}
)
CMAKE_BOOLEAN_VALUES: FrozenSet[str] = CMAKE_TRUE_VALUES | CMAKE_FALSE_VALUES


def convert_boolean_value(key: str, value: str) -> bool:
    value_upper = value.upper()
    assert value_upper in CMAKE_BOOLEAN_VALUES, \
        "Unrecognized value of a CMake boolean variable %s: %s (allowed: %s)" % (
            key, value, ', '.join(sorted(CMAKE_BOOLEAN_VALUES)))
    return value_upper in CMAKE_TRUE_VALUES


class CMakeCache:
    path: str
    name_to_val_and_type: Dict[str, Tuple[str, str]] = {}

    def __init__(self, path: str) -> None:
        self.name_to_val_and_type = {}
        self.path = os.path.abspath(path)
        with open(self.path) as input_file:
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

        if value.startswith("'") and value.endswith("'") and "'" not in value[1:-1]:
            # Remove the surrounding single quotes that CMake puts around e.g. values containing
            # spaces.
            # TODO: what happens if the original value contains both single quotes and spaces?
            value = value[1:-1]

        if type_name == 'BOOL':
            return convert_boolean_value(key, value)

        return value

    def get_bool(self, key: str, default_value: Any = None) -> Optional[bool]:
        """
        Similar to get(), but always returns a boolean value for a boolean CMake variable, or
        raises an exception if the variable is not a boolean.
        """
        value = self.get(key, default_value)
        if value is None:
            return None
        if isinstance(value, bool):
            return value
        return convert_boolean_value(key, value)

    def get_or_raise(self, key: str) -> Any:
        value = self.get(key)
        if value is None:
            raise KeyError(f'Key {key} does not exist in CMake cache at {self.path}.')
        return value


def load_cmake_cache(build_root: str) -> CMakeCache:
    return CMakeCache(os.path.join(build_root, 'CMakeCache.txt'))
