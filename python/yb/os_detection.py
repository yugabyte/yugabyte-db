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

import platform
import os

from yb.common_util import read_file


def get_short_os_name() -> str:
    platform_system = platform.system()
    os_name = ''
    os_version = ''
    if platform_system == 'Linux':
        os_release_path = '/etc/os-release'
        if os.path.exists(os_release_path):
            os_release_str = read_file(os_release_path)
            os_release_lines = [line.strip() for line in os_release_str.split('\n') if line.strip()]
            os_release_tuples = [line.split('=') for line in os_release_lines]
            kvs = {t[0]: t[1].strip('"') for t in os_release_tuples if len(t) == 2}
            name = kvs.get('NAME')
            version_id = kvs.get('VERSION_ID')
            if name and version_id:
                os_name = name.split()[0].lower()
                if os_name == 'ubuntu':
                    # For Ubuntu we will keep the full version, such as 18.04.
                    os_version = version_id
                else:
                    os_version = version_id.split('.')[0]
            else:
                raise ValueError(
                    f"Could not determine OS name and version from the contents of "
                    f"{os_release_path}. Parsed data: {kvs}. Raw contents:\n{os_release_str}")

        else:
            raise ValueError(
                f"Cannot identify OS release. File {os_release_path} is not present.")
    elif platform_system == 'Darwin':
        os_name = 'macos'
    else:
        raise ValueError(f"Unsupported platform: {platform_system}")

    return os_name + os_version
