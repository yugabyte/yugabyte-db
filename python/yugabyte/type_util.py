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

# Utilities for type checking.


from typing import Any, Type, TypeVar, cast


T = TypeVar('T')


def checked_cast(expected_type: Type[T], v: Any) -> T:
    assert isinstance(v, expected_type), \
        f"Expected {v} to be of type {expected_type}, but it is of type {type(v)}"
    return cast(T, v)


def assert_type(expected_type: Type[T], v: Any) -> None:
    assert isinstance(v, expected_type), \
        f"Expected {v} to be of type {expected_type}, but it is of type {type(v)}"
