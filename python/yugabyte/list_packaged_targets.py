#!/usr/bin/env python3

# Copyright (c) YugabyteDB, Inc.

# Lists the targets that need to be built for one or more YB release packages.

import argparse

from yugabyte.release_util import (
    DEFAULT_PACKAGE_NAME,
    get_packaged_targets,
    get_release_package_names,
)
from yugabyte.optional_components import (
    add_optional_component_arguments,
    optional_components_from_args,
)


def main() -> None:
    parser = argparse.ArgumentParser()
    add_optional_component_arguments(parser)
    parser.add_argument('--package_name',
                        action='append',
                        choices=get_release_package_names(),
                        help='Release package to list the build targets of. May be repeated. '
                             'Defaults to %s.' % DEFAULT_PACKAGE_NAME)
    args = parser.parse_args()
    optional_components = optional_components_from_args(args)

    targets = get_packaged_targets(
        args.package_name or [DEFAULT_PACKAGE_NAME], optional_components)
    if not targets:
        raise RuntimeError("Could not find any targets to be packaged in the release archive")
    for target in targets:
        print(target)


if __name__ == '__main__':
    main()
