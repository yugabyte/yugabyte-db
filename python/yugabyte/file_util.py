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

import hashlib
import os
import pathlib

from typing import Optional, List, Dict, Tuple, Any, Union


def to_path(path: Union[str, pathlib.Path]) -> pathlib.Path:
    if isinstance(path, pathlib.Path):
        return path
    return pathlib.Path(path)


def mkdir_p(dir_path: Union[str, pathlib.Path]) -> None:
    """
    Similar to the "mkdir -p ..." shell command. Creates the given directory and all enclosing
    directories. No-op if the directory already exists.
    """
    to_path(dir_path).mkdir(parents=True, exist_ok=True)


def delete_file_if_exists(path: str) -> None:
    """
    Remove the given file or symbolic link if it exists. No-op if the file does not exist.
    """
    try:
        os.remove(path)
    except FileNotFoundError:
        pass


def assert_absolute_path(dir_path: str) -> None:
    assert os.path.isabs(dir_path), "Expected an absolute path, got %s" % dir_path


def path_to_str(path: Union[str, pathlib.Path]) -> str:
    if isinstance(path, str):
        return path
    return str(path)


def read_file(file_path: Union[str, pathlib.Path]) -> str:
    path_str = path_to_str(file_path)
    if path_str.endswith('.gz'):
        import gzip
        with gzip.open(path_str, 'rt') as input_file:
            return input_file.read()
    with open(path_str) as input_file:
        return input_file.read()


def write_file(
        content: Union[str, List[str]], output_file_path: Union[str, pathlib.Path]) -> None:
    if isinstance(content, list):
        content = '\n'.join(content) + '\n'
    with open(path_to_str(output_file_path), 'w') as output_file:
        output_file.write(content)


def compute_file_sha256(file_path: str) -> str:
    """
    Compute the SHA-256 checksum of the given file.
    """
    buf_size = 1048576
    sha256 = hashlib.sha256()
    with open(file_path, 'rb') as f:
        while True:
            data = f.read(buf_size)
            if not data:
                break
            sha256.update(data)
    return sha256.hexdigest()


def clean_path_join(base_path: str, rel_path: str) -> str:
    """
    >>> clean_path_join('foo', 'bar')
    'foo/bar'
    >>> clean_path_join('foo', '.')
    'foo'
    """
    if rel_path == '.':
        return base_path
    return os.path.join(base_path, rel_path)
