#!/usr/bin/env python

# Copyright (c) YugaByte, Inc.

# Lists the targets that need to be built for a YB release.

import os
import json
import re


YB_SRC_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LATEST_BINARY_RE = re.compile('^build/latest/bin/([^/]+)$')


def main():
    with open(os.path.join(YB_SRC_ROOT, 'yb_release_manifest.json')) as manifest_input_file:
        release_manifest = json.loads(manifest_input_file.read())
    for bin_path in release_manifest['bin']:
        match = LATEST_BINARY_RE.match(bin_path)
        if match:
            print match.group(1)


if __name__ == '__main__':
    main()
