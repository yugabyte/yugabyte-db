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


UBUNTU_OS_TYPE_RE = re.compile(r'^(ubuntu)([0-9]{2})([0-9]{2})$')
AMZN_OS_TYPE_RE = re.compile(r'^(amzn|amazonlinux)([0-9]+)$')
RHEL_FAMILY_RE = re.compile(r'^(almalinux|centos|rhel)([0-9]+)$')


def adjust_os_type(os_type: str) -> str:
    match = UBUNTU_OS_TYPE_RE.match(os_type)
    if match:
        # Convert e.g. ubuntu2004 -> ubuntu20.04 for clarity.
        return f'{match.group(1)}{match.group(2)}.{match.group(3)}'
    match = AMZN_OS_TYPE_RE.match(os_type)
    if match:
        # Convert e.g. amazonlinux2 -> amzn2 to match OS ID name
        return f'amzn{match.group(2)}'
    return os_type


def is_compatible_os(archive_os: str, target_os: str) -> bool:
    """
    Check if two combinations of OS name and version are compatible.
    """
    rhel_like1 = RHEL_FAMILY_RE.match(archive_os)
    rhel_like2 = RHEL_FAMILY_RE.match(target_os)
    if rhel_like1 and rhel_like2 and rhel_like1.group(2) == rhel_like2.group(2):
        return True
    return archive_os == target_os
