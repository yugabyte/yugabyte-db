#!/usr/bin/env python2.7

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

import os
import sys


RPATH_ARG_PREFIX = '-Wl,-rpath,'


def path_to_components(p):
    items = p.split('/')
    return [item for item in items if len(item) > 0]


def num_common_path_entries(path1, path2):
    """
    :return The number of common initial path entries for the two given paths.
    """
    items1 = path_to_components(path1)
    items2 = path_to_components(path2)
    n = 0
    for i in range(min(len(items1), len(items2))):
        if items1[i] != items2[i]:
            break
        n += 1
    return n


class RpathRelativizer:
    def __init__(self, output_path):
        self.output_dir_abspath = os.path.dirname(os.path.abspath(output_path))

    def relativize_arg(self, rpath_arg):
        """
        :param rpath_arg: a compiler argument of the '-Wl,-rpath,...' form.
        """

        if not rpath_option.startswith(RPATH_ARG_PREFIX):
            raise ValueError('RPATH option does not start with %s: %s' % (
                RPATH_ARG_PREFIX, rpath_option
            ))

        relative_rpath_entries = [
            self.relativize_rpath_entry(entry)
            for entry in rpath_option[len(RPATH_ARG_PREFIX):].split(',')
            if entry
        ]

        return RPATH_ARG_PREFIX + ','.join(relative_rpath_entries)

    def relativize_rpath_entry(self, entry):
        if not entry:
            entry

        if os.path.isdir(entry):
            # Try both absolute path and "real" path to support cases when the entire source
            # directory is under a symlink.
            for abs_entry in [os.path.abspath(entry), os.path.realpath(entry)]:
                if (num_common_path_entries(abs_entry, self.output_dir_abspath) >= 2 and
                        '/.linuxbrew-yb-build/' not in abs_entry):
                    # E.g. both paths are under /home/some_user, and not related to Linuxbrew.
                    # Making Linuxbrew RPATHs relative sometimes breaks linking of some targets
                    # because a library like libresolv gets picked up from the system location
                    # instead of Linuxbrew.
                    return os.path.join(
                        '$ORIGIN',
                        os.path.relpath(abs_entry, self.output_dir_abspath))

        if ':' in entry:
            return ':'.join([
                self.relativize_rpath_entry(sub_entry) for sub_entry in entry.split(':')
            ])

        return entry


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise RuntimeError("Exactly 2 arguments expected: output path and rpath linker option")
    output_path, rpath_option = sys.argv[1:]

    relativizer = RpathRelativizer(output_path)
    print(relativizer.relativize_arg(rpath_option))
