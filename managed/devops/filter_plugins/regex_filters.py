# Copyright (c) YugaByte, Inc.
#
# Copyright 2019 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

import re
import os
from jinja2.runtime import Undefined
from jinja2.filters import do_default
from ybops.common.release import ReleasePackage
from ybops.common.exceptions import YBOpsRuntimeError


def _get_release_from_archive(archive_filename):
    release = None
    try:
        # This will fail for local / itest packages.
        release = ReleasePackage.from_package_name(archive_filename, is_official_release=True)
    except YBOpsRuntimeError as e:
        release = ReleasePackage.from_package_name(archive_filename, is_official_release=False)
    return release


def extract_release_version(archive_filename):
    release = _get_release_from_archive(archive_filename)
    return release.version


def extract_archive_folder_name(archive_filename):
    return re.sub("[.]tar[.]gz$", "", archive_filename)


def extract_yugabyte_release_folder(archive_filename):
    if "yugabyte" in archive_filename:
        release = _get_release_from_archive(archive_filename)
        # In case of yugabyte package alone, we would put it under
        # /home/yugabyte/yb-software/releases/.
        return os.path.join("releases", release.get_release_name())


def yugabyte_directories(mount_points, yb_process_type, dir_type):
    return map(lambda m: m + os.path.join('/yugabyte', yb_process_type, dir_type), mount_points)


def strip_whitespace(s):
    return s.strip()


def get_value(dict, key):
    if isinstance(dict, Undefined) or isinstance(dict[key], Undefined):
        return do_default(None)
    return dict[key]


class FilterModule(object):
    def filters(self):
        return {
            "extract_release_version": extract_release_version,
            "extract_archive_folder_name": extract_archive_folder_name,
            "extract_yugabyte_release_folder": extract_yugabyte_release_folder,
            "yugabyte_directories": yugabyte_directories,
            "strip": strip_whitespace,
            "get_value": get_value
        }
