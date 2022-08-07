#!/usr/bin/env python3
# Copyright (c) YugaByte, Inc.

import argparse
import logging
import os
import shutil

from ybops.utils import init_env, log_message, get_release_file
from ybops.common.exceptions import YBOpsRuntimeError

"""This script packages the Ybanystaller go linux binary executable (ybanystaller) into the
   Yugabundle archive (might be modified in the future to also package related bundled dependencies
   such as Postgres and Nginx).
   Needed in order to perform end to end testing of Ybanystaller.
"""

parser = argparse.ArgumentParser()
parser.add_argument('--destination', help='Copy release to Destination folder.')
parser.add_argument('--binary', help='Yba-installer binary to copy to destination.')
parser.add_argument('--config', help='Yba-installer config yml to copy to destination.')
parser.add_argument('--input_config', help='Yba-installer input config to copy to destination.')
parser.add_argument('--json_schema', help='Yba-installer schema json to copy to destination.')

args = parser.parse_args()

try:
    init_env(logging.INFO)
    script_dir = os.path.dirname(os.path.realpath(__file__))
    release_file = get_release_file(script_dir, "yba-installer")
    # Uncomment/Extend the line below if want to generate a Ybanystaller archive in the future
    # (which would not be just the binary, but rather additional dependencies such as Postgres and
    # Nginx).

    # shutil.copyfile(args.binary, release_file)
    # shutil.copyfile(args.config, release_file)
    # shutil.copyfile(args.input_config, release_file)
    # shutil.copyfile(args.json_schema, release_file)
    if args.destination:
        if not os.path.exists(args.destination):
            raise YBOpsRuntimeError("Destination {} not a directory.".format(args.destination))
        shutil.copy(args.binary, args.destination)
        shutil.copy(args.config, args.destination)
        shutil.copy(args.input_config, args.destination)
        shutil.copy(args.json_schema, args.destination)

except (OSError, shutil.SameFileError) as e:
    log_message(logging.ERROR, e)
    raise e
