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
from yb.command_util import run_program


linuxbrew_dir = None

YB_SRC_ROOT = os.path.join(os.path.dirname(os.path.realpath(__file__)), '..', '..')


def get_linuxbrew_dir():
    if not sys.platform.startswith('linux'):
        return None

    global linuxbrew_dir
    if not linuxbrew_dir:
        find_script_result = run_program(os.path.join(
            YB_SRC_ROOT, 'build-support', 'find_linuxbrew.sh'))
        linuxbrew_dir = find_script_result.stdout.strip()
        if not os.path.isdir(linuxbrew_dir) and os.path.exists('/etc/centos-release'):
            raise RuntimeError(
                    ("Directory returned by the '{}' script does not exist: '{}'. " +
                     "This is only an error on CentOS. Details: {}").format(
                            find_script_result.program_path,
                            linuxbrew_dir,
                            find_script_result))
    return linuxbrew_dir
