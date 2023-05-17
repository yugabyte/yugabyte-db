# Copyright (c) YugaByte, Inc.
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
#

"""
Helps find the Linuxbrew installation.
"""

import os
import sys
import subprocess

from yugabyte.common_util import safe_path_join, YB_SRC_ROOT, shlex_join

from typing import Optional


g_build_root: Optional[str] = None

g_linuxbrew_home: Optional['LinuxbrewHome'] = None
g_linuxbrew_home_initialized: bool = False


class LinuxbrewHome:
    linuxbrew_dir: Optional[str]
    linuxbrew_link_target: Optional[str]
    cellar_glibc_dir: Optional[str]
    ldd_path: Optional[str]
    ld_so_path: Optional[str]
    patchelf_path: Optional[str]

    def __init__(self, build_root: Optional[str] = None) -> None:
        self.linuxbrew_dir = None
        self.linuxbrew_link_target = None
        self.cellar_glibc_dir = None
        self.ldd_path = None
        self.ld_so_path = None
        self.patchelf_path = None

        find_linuxbrew_cmd_line = [
            os.path.join(YB_SRC_ROOT, 'build-support', 'find_linuxbrew.sh')
        ]
        if build_root:
            find_linuxbrew_cmd_line.extend(['--build-root', build_root])

        linuxbrew_dir = subprocess.check_output(find_linuxbrew_cmd_line).decode('utf-8').strip()
        # If the find_linuxbrew.sh script returns nothing, Linuxbrew is not being used.
        if not linuxbrew_dir:
            return

        if not os.path.isdir(linuxbrew_dir):
            raise IOError(
                f"Linuxbrew directory does not exist: {linuxbrew_dir}. "
                f"Command used to find it: {find_linuxbrew_cmd_line}")
        self.linuxbrew_dir = os.path.realpath(linuxbrew_dir)

        # Directories derived from the Linuxbrew top-level one.
        self.linuxbrew_link_target = os.path.realpath(linuxbrew_dir)
        self.cellar_glibc_dir = safe_path_join([self.linuxbrew_dir, 'Cellar', 'glibc'])
        self.ldd_path = safe_path_join([self.linuxbrew_dir, 'bin', 'ldd'])
        self.ld_so_path = safe_path_join([self.linuxbrew_dir, 'lib', 'ld.so'])
        self.patchelf_path = safe_path_join([self.linuxbrew_dir, 'bin', 'patchelf'])

    def path_is_in_linuxbrew_dir(self, path: str) -> bool:
        if not self.is_enabled():
            return False
        path = os.path.abspath(path)
        assert self.linuxbrew_dir is not None
        assert self.linuxbrew_link_target is not None
        return (path.startswith(self.linuxbrew_dir + '/') or
                path.startswith(self.linuxbrew_link_target + '/'))

    def get_human_readable_dirs(self) -> str:
        assert self.linuxbrew_dir is not None
        assert self.linuxbrew_link_target is not None
        return ', '.join("'%s'" % d for d in
                         sorted(set([self.linuxbrew_dir, self.linuxbrew_link_target])))

    def is_enabled(self) -> bool:
        return self.linuxbrew_dir is not None


def set_build_root(build_root: str) -> None:
    global g_build_root
    g_build_root = build_root


def get_linuxbrew_home() -> Optional[LinuxbrewHome]:
    global g_linuxbrew_home
    global g_linuxbrew_home_initialized

    if g_linuxbrew_home_initialized:
        return g_linuxbrew_home

    if not sys.platform.startswith('linux'):
        g_linuxbrew_home = None
        g_linuxbrew_home_initialized = True
        return None

    linuxbrew_home = LinuxbrewHome(g_build_root)
    if not linuxbrew_home.is_enabled():
        g_linuxbrew_home = None
        g_linuxbrew_home_initialized = True
        return None

    g_linuxbrew_home = linuxbrew_home
    g_linuxbrew_home_initialized = True
    return g_linuxbrew_home


def get_linuxbrew_dir() -> Optional[str]:
    linuxbrew_home = get_linuxbrew_home()
    if linuxbrew_home is None:
        return None
    return linuxbrew_home.linuxbrew_dir


def using_linuxbrew() -> bool:
    return get_linuxbrew_home() is not None
