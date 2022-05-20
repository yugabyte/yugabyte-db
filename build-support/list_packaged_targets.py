#!/usr/bin/env python3

# Copyright (c) YugaByte, Inc.

# Lists the targets that need to be built for a YB release.

import json
import os
import re


YB_SRC_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LATEST_BINARY_RE = re.compile('^\$BUILD_ROOT/bin/([^/]+)$')


def main():
    with open(os.path.join(YB_SRC_ROOT, 'yb_release_manifest.json')) as manifest_input_file:
        release_manifest = json.loads(manifest_input_file.read())['yugabyte']
    found_matches = False
    for bin_path in release_manifest['bin']:
        match = LATEST_BINARY_RE.match(bin_path)
        if match:
            print(match.group(1))
            found_matches = True

    if not found_matches:
        raise RuntimeError("Could not find any targets to be packaged in the release archive")


if __name__ == '__main__':
    main()
