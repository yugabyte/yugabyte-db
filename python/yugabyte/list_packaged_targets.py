#!/usr/bin/env python3

# Copyright (c) YugaByte, Inc.

# Lists the targets that need to be built for a YB release.

import re
import argparse

from yugabyte.release_util import read_release_manifest, filter_bin_items
from yugabyte.optional_components import (
    add_optional_component_arguments,
    optional_components_from_args,
)

LATEST_BINARY_RE = re.compile(r'^[$]BUILD_ROOT/bin/([^/]+)$')


def main() -> None:
    parser = argparse.ArgumentParser()
    add_optional_component_arguments(parser)
    args = parser.parse_args()
    optional_components = optional_components_from_args(args)

    release_manifest = read_release_manifest('yugabyte')
    found_matches = False
    for bin_path in filter_bin_items(release_manifest['bin'], optional_components):
        match = LATEST_BINARY_RE.match(bin_path)
        if match:
            print(match.group(1))
            found_matches = True

    if not found_matches:
        raise RuntimeError("Could not find any targets to be packaged in the release archive")


if __name__ == '__main__':
    main()
