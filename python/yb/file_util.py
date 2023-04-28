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

import os
import pathlib


def mkdir_p(dir_path) -> None:
    """
    Similar to the "mkdir -p ..." shell command. Creates the given directory and all enclosing
    directories. No-op if the directory already exists.
    """
    pathlib.Path(dir_path).mkdir(parents=True, exist_ok=True)


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


def read_file(file_path: str) -> str:
    with open(file_path) as input_file:
        return input_file.read()


def write_file(content: str, output_file_path: str) -> None:
    with open(output_file_path, 'w') as output_file:
        output_file.write(content)
