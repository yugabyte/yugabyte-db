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

from yugabyte.file_util import read_file

from typing import Optional, Set


class BuildPaths:
    build_root: str
    llvm_path: Optional[str]
    thirdparty_path: Optional[str]
    linuxbrew_path: Optional[str]

    file_existence_checking_cache: Set[str]

    def _read_path_file(self, path_file_name: str) -> Optional[str]:
        path_file_path = os.path.join(self.build_root, path_file_name)
        if not os.path.exists(path_file_path):
            return None

        dir_path = read_file(path_file_path).strip()
        if not os.path.exists(dir_path):
            raise IOError(
                "Path contained in file %s does not exist or is not a directory: '%s'" % (
                    path_file_path, dir_path))
        return dir_path

    def __init__(self, build_root: str) -> None:
        self.build_root = build_root

        if not os.path.exists(build_root):
            raise IOError("Build root directory does not exist: %s" % build_root)

        self.llvm_path = self._read_path_file('llvm_path.txt') or os.getenv('YB_LLVM_TOOLCHAIN_DIR')
        self.thirdparty_path = self._read_path_file('thirdparty_path.txt')
        self.linuxbrew_path = self._read_path_file('linuxbrew_path.txt')

        self.file_existence_checking_cache = set()

    def get_llvm_path(self) -> str:
        assert self.llvm_path is not None
        return self.llvm_path

    def get_linuxbrew_path(self) -> str:
        assert self.linuxbrew_path is not None
        return self.linuxbrew_path

    def get_thirdparty_path(self) -> str:
        assert self.thirdparty_path is not None
        return self.thirdparty_path

    def _check_file_existence(self, file_path: str) -> None:
        if file_path in self.file_existence_checking_cache:
            return
        if not os.path.exists(file_path):
            raise IOError("File does not exist: %s" % file_path)
        self.file_existence_checking_cache.add(file_path)

    def get_llvm_tool_path(self, tool_name: str) -> str:
        tool_path = os.path.join(self.get_llvm_path(), 'bin', tool_name)
        self._check_file_existence(tool_path)
        return tool_path
