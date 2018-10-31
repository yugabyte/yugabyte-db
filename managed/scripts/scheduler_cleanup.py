#!/usr/bin/env python
# Copyright (c) YugaByte, Inc.

import glob
import os
import shutil

VERSIONS_TO_KEEP = 3

GLOB_BUILDS = "/opt/builds/20*"
GLOB_SOFTWARE_YUGABYTE = "/opt/yugabyte/yb-software/yugabyte-*"
GLOB_SOFTWARE_YUGAWARE = "/opt/yugabyte/yb-software/yugaware-*"
GLOB_SOFTWARE_DEVOPS = "/opt/yugabyte/yb-software/devops-*"
GLOB_RELEASES = "/opt/yugabyte/releases/*"
GLOB_TAR_GZ_YUGABYTE = "/opt/builds/yugabyte/build/release-*/*.tar.gz"
GLOB_TAR_GZ_YUGAWARE = "/opt/builds/yugaware/build/*.tar.gz"
GLOB_TAR_GZ_DEVOPS = "/opt/builds/devops/build/*.tar.gz"

ALL_GLOBS = [
    GLOB_BUILDS,
    GLOB_SOFTWARE_YUGABYTE,
    GLOB_SOFTWARE_YUGAWARE,
    GLOB_SOFTWARE_DEVOPS,
    GLOB_RELEASES,
    GLOB_TAR_GZ_YUGABYTE,
    GLOB_TAR_GZ_YUGAWARE,
    GLOB_TAR_GZ_DEVOPS
]


def delete_last_n(glob_pattern, n=None):
    files = glob.glob(glob_pattern)
    # Default num versions.
    n = n if n is not None else VERSIONS_TO_KEEP
    # Sort by time, keep most recent.
    files.sort(key=os.path.getmtime)
    # Figure out how many to delete.
    num_files_to_delete = len(files) - n
    # Ensure this is actually > 0.
    to_delete = files[:num_files_to_delete] if num_files_to_delete > 0 else []
    for f in to_delete:
        print "Deleting path {}".format(f)
        if os.path.isfile(f):
            os.remove(f)
        elif os.path.isdir(f):
            shutil.rmtree(f)


for g in ALL_GLOBS:
    delete_last_n(g)
