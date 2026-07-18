# Copyright (c) YugabyteDB, Inc.
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

import hashlib
from typing import Union, Optional, Any


def encode_if_needed(s: Union[bytes, str]) -> bytes:
    if isinstance(s, str):
        return s.encode('utf-8')
    return s


def compute_sha256(s: Union[bytes, str]) -> str:
    return hashlib.sha256(encode_if_needed(s)).hexdigest()


def none_to_empty_string(x: Optional[Any]) -> Any:
    if x is None:
        return ''
    return x


def matches_maybe_empty(a: Optional[str], b: Optional[str]) -> bool:
    """
    Returns True if a or b are equal, but treating all None values as empty strings.
    """
    return (a or '') == (b or '')
