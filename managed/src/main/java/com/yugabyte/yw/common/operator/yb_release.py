#!/usr/bin/env python3
# Copyright (c) YugaByte, Inc.

import argparse
import sys
import logging
import os
import shutil
import traceback


"""This script is builds and packages the crd bundled with yugaware.
  - The package is per build with the same name, its versioned internally so we do not add a
    version of the build in the file name.
  - A higher level user, such as itest, will upload all release packages to s3 release bucket
"""

# Configure logging to output to standard output
logging.basicConfig(level=logging.DEBUG,  # Set minimum log level to DEBUG
                    # Set the format of log messages
                    format='%(asctime)s - %(levelname)s - %(message)s',
                    stream=sys.stdout)  # Use sys.stdout to print logs


def concatenate_yaml_files_with_separator(file_list, output_file):
    with open(output_file, 'w') as outfile:
        for fname in file_list:
            with open(fname) as infile:
                outfile.write('---\n')  # Document separator for YAML
                outfile.write(infile.read())
                outfile.write('\n\n')  # Optional: Adds extra newline for readability


def main():
    script_dir = os.path.dirname(os.path.realpath(__file__))
    # List your YAML files here
    yaml_files = os.listdir(
        os.path.join(script_dir, "resources"))
    # convert relative paths to full paths.
    yaml_files = [os.path.join(script_dir, "resources", x) for x in yaml_files]
    tag_name = args.tag
    if tag_name:
        output_yaml_filename = "yugabyte_concatenated_crd-%s.yaml" % (tag_name)
    else:
        output_yaml_filename = "yugabyte_concatenated_crd.yaml"

    if args.destination:
        output_yaml = os.path.join(args.destination, output_yaml_filename)
    else:
        output_yaml = os.path.join(script_dir, output_yaml_filename)
    # Generate the output file
    concatenate_yaml_files_with_separator(yaml_files, output_yaml)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('--destination', default="",
                        help='Copy release to Destination folder.')
    parser.add_argument('--tag', default="", help='Release tag name')
    # IMPORTANT: DO NOT REMOVE THIS FLAG. REQUIRED BY INTERNAL INFRA.
    parser.add_argument('--force', help='Force no user input', action="store_true")
    args = parser.parse_args()
    try:
        main()
    except Exception as e:
        logging.error("Got Exception %s, traceback %s" % (str(e), traceback.format_exc()))
        raise RuntimeError("Failed to generate crd")
