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

import re


UBUNTU_OS_TYPE_RE = re.compile(r'^(ubuntu)([0-9]{2})\.?([0-9]{2})$')
RHEL_FAMILY_RE = re.compile(r'^(almalinux|centos|rhel)([0-9]+)$')


def adjust_os_type(os_type: str) -> str:
    match = UBUNTU_OS_TYPE_RE.match(os_type)
    if match:
        # Convert e.g. ubuntu2004 -> ubuntu20.04 for clarity.
        return f'{match.group(1)}{match.group(2)}.{match.group(3)}'
    return os_type


def is_compatible_os(archive_os: str, target_os: str, allow_older: bool) -> bool:
    """
    Check if two combinations of OS name and version are compatible.
    """
    if archive_os == target_os:
        return True

    rhel_archive = RHEL_FAMILY_RE.match(archive_os)
    rhel_target = RHEL_FAMILY_RE.match(target_os)
    if rhel_archive and rhel_target and rhel_archive.group(2) == rhel_target.group(2):
        return True
    if rhel_archive and rhel_target and allow_older:
        if int(rhel_archive.group(2)) < int(rhel_target.group(2)):
            return True

    ubuntu_archive = UBUNTU_OS_TYPE_RE.match(archive_os)
    ubuntu_target = UBUNTU_OS_TYPE_RE.match(target_os)
    if ubuntu_archive and ubuntu_target and ubuntu_archive.group(2) == ubuntu_target.group(2):
        # Redundant, since there is only one ubuntu OS in regexp, unless minor version diff.
        return True
    if ubuntu_archive and ubuntu_target and allow_older:
        # Ignoring minor version, we expect they are compatible (and we always use stable 04).
        if int(ubuntu_archive.group(2)) < int(ubuntu_target.group(2)):
            return True

    return False
