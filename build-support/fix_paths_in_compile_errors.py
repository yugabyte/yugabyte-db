#!/usr/bin/env python

# Copyright (c) YugaByte, Inc.

# This fixes file paths in compilation error output so that they become clickable in CLion.
# This became necessary when we started setting CCACHE_BASEDIR. In that case, it looks like ccache
# substitutes file paths relative to the current directory (which might be arbitrarily nested
# inside the build directory) into the compiler command line.

import sys
import re
import os

PATH_RE = re.compile(r'([a-zA-Z0-9_./-]+)\s*(?:\s*|$)')
YB_SRC_ROOT = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))


if __name__ == '__main__':
    def rewrite_path(match):
        original_path = match.group()
        if os.path.exists(original_path):
            real_path = os.path.realpath(original_path)
            if real_path.startswith(YB_SRC_ROOT + '/'):
                return os.path.relpath(real_path, YB_SRC_ROOT)
        return original_path

    for line in sys.stdin:
        # We're using .rstrip() and not .strip() because we want to preserve original indentation.
        print PATH_RE.sub(rewrite_path, line.rstrip())
