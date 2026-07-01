#!/usr/bin/env python3
# Copyright (c) YugabyteDB, Inc.

import argparse
import logging
import os
import re
import shutil
import subprocess
import sys

"""Packages the BYOC API proxy release tarball for upload to a release bucket."""

PACKAGE_NAME = "byoc_api_proxy"
VERSION_PATTERN = re.compile(r"^\d+\.\d+\.\d+$")
PACKAGE_VERSION_PATTERN = re.compile(r"^\d+\.\d+\.\d+(-SNAPSHOT)?$")


def read_gradle_property(source_dir, key):
    props_file = os.path.join(source_dir, "gradle.properties")
    with open(props_file, "r") as file:
        for line in file:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            prop_key, prop_value = line.split("=", 1)
            if prop_key.strip() == key:
                return prop_value.strip()
    raise ValueError("Property {} not found in {}".format(key, props_file))


def read_release_version(source_dir, local=False):
    version = read_gradle_property(source_dir, "version")
    if local:
        if not PACKAGE_VERSION_PATTERN.match(version):
            raise ValueError(
                "Invalid version in gradle.properties: expected semver X.Y.Z or "
                "X.Y.Z-SNAPSHOT, got '{}'".format(version)
            )
        return version

    gradle_version = version
    if version.endswith("-SNAPSHOT"):
        version = version[: -len("-SNAPSHOT")]
    if not VERSION_PATTERN.match(version):
        raise ValueError(
            "Invalid version in gradle.properties: expected semver X.Y.Z or "
            "X.Y.Z-SNAPSHOT, got '{}'".format(gradle_version)
        )
    if gradle_version != version:
        logging.info(
            "Using release version %s from Gradle version %s", version, gradle_version
        )
    return version


def staged_package_path(source_dir, version):
    return os.path.join(
        source_dir,
        "build",
        "{}-{}.tar.gz".format(PACKAGE_NAME, version),
    )


def main():
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")

    parser = argparse.ArgumentParser()
    parser.add_argument("--source_dir", help="Source code directory.", required=True)
    parser.add_argument(
        "--local",
        action="store_true",
        help="Package using the Gradle version as-is (including -SNAPSHOT).",
    )
    parser.add_argument(
        "--destination", help="Directory to copy release tarball(s) into.", required=True
    )
    args = parser.parse_args()

    source_dir = os.path.abspath(args.source_dir)
    destination = os.path.abspath(args.destination)
    os.makedirs(destination, exist_ok=True)

    version = read_release_version(source_dir, local=args.local)
    build_script = os.path.join(source_dir, "build.sh")

    logging.info("Building and packaging version %s", version)
    subprocess.check_call(
        [build_script, "clean", "build", "test", "package", version],
    )

    package_path = staged_package_path(source_dir, version)
    if not os.path.isfile(package_path):
        raise FileNotFoundError("Expected package not found: {}".format(package_path))

    os.makedirs(destination, exist_ok=True)
    dest_path = os.path.join(destination, os.path.basename(package_path))
    if os.path.abspath(package_path) == os.path.abspath(dest_path):
        logging.info("Release package already at %s", dest_path)
        return

    logging.info("Copying %s to %s", package_path, dest_path)
    shutil.copy2(package_path, dest_path)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        logging.error("%s", error)
        sys.exit(1)
