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
#

import re
import subprocess

from typing import Optional, List

from sys_detection import local_sys_conf, OsReleaseVars

from yugabyte.common_util import find_executable
from yugabyte.linuxbrew import get_linuxbrew_home, LinuxbrewHome


g_chrpath_path_initialized: bool = False
g_chrpath_path: Optional[str]

g_patchelf_path_initialized: bool = False
g_patchelf_path: Optional[str] = find_executable('patchelf')

READELF_RPATH_RE = re.compile(r'^.*Library (?:rpath|runpath): \[(.*)\]$')


def get_patchelf_path() -> Optional[str]:
    global g_patchelf_path_initialized, g_patchelf_path

    if g_patchelf_path_initialized:
        return g_patchelf_path

    linuxbrew_home: Optional[LinuxbrewHome] = get_linuxbrew_home()
    if linuxbrew_home is not None:
        g_patchelf_path = linuxbrew_home.patchelf_path
    if not g_patchelf_path:
        g_patchelf_path = find_executable('patchelf')

    g_patchelf_path_initialized = True
    return g_patchelf_path


def is_elf(file_path: str) -> bool:
    file_tool_output = subprocess.check_output(['file', '--brief', file_path])
    return file_tool_output.decode('utf-8').startswith('ELF')


def get_rpath(file_path: str) -> Optional[str]:
    readelf_output = subprocess.check_output(['readelf', '-d', file_path])
    for line in readelf_output.decode('utf-8').split('\n'):
        line = line.rstrip()
        re_match = READELF_RPATH_RE.match(line)
        if re_match:
            return re_match.group(1)
    return None


def get_chrpath_path() -> Optional[str]:
    global g_chrpath_path_initialized, g_chrpath_path

    if g_chrpath_path_initialized:
        return g_chrpath_path
    g_chrpath_path = find_executable('chrpath')
    g_chrpath_path_initialized = True
    return g_chrpath_path


def set_rpath(file_path: str, rpath: str) -> None:
    if not is_elf(file_path):
        return
    linux_os_release: Optional[OsReleaseVars] = local_sys_conf().linux_os_release
    chrpath_path: Optional[str] = get_chrpath_path()

    cmd_line: List[str]

    if linux_os_release is not None and linux_os_release.id == 'amzn' and chrpath_path is not None:
        # On Amazon Linux, we prefer chrpath, because patchelf version 0.9 installed on Amazon
        # Linux 2 silently fails to set rpath in our experience.
        cmd_line = [chrpath_path, '-r', rpath, file_path]
    else:
        patchelf_path = get_patchelf_path()
        assert patchelf_path is not None
        cmd_line = [patchelf_path, '--set-rpath', rpath, file_path]

    subprocess.check_call(cmd_line)

    # Verify that we have succeeded in setting rpath.
    actual_rpath = get_rpath(file_path)
    if actual_rpath != rpath:
        raise ValueError(
            f"Failed to set rpath on file {file_path} to {rpath}: it is now {actual_rpath}")


def remove_rpath(file_path: str) -> None:
    patchelf_path = get_patchelf_path()
    assert patchelf_path is not None
    subprocess.check_call([patchelf_path, '--remove-rpath', file_path])
