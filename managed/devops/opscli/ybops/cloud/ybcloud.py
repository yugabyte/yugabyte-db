#!/usr/bin/env python
#
# Copyright 2019 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

import argparse
import json
import logging
import sys

from ybops.cloud.aws.cloud import AwsCloud
from ybops.cloud.gcp.cloud import GcpCloud
from ybops.cloud.onprem.cloud import OnPremCloud
from ybops.cloud.azure.cloud import AzureCloud
from ybops.cloud.common.base import AbstractCommandParser
from ybops.utils import init_env, init_logging
from ybops.common.exceptions import YBOpsExitCodeException


class YbCloud(AbstractCommandParser):
    """Top-level entry point into YugaByte ops CLI tool.
    """
    def __init__(self):
        super(YbCloud, self).__init__("ybcloud")
        # Default INFO logging for pre argument parsing logging.
        init_logging(logging.INFO)

    def add_subcommands(self):
        """Setting up the available top level commands, in particular the sub commands for the
        various clouds we operate on.
        """
        self.add_subcommand(AwsCloud())
        self.add_subcommand(GcpCloud())
        self.add_subcommand(OnPremCloud())
        self.add_subcommand(AzureCloud())

    def add_extra_args(self):
        """Setting up the top level flags for the entire program.
        """
        self.parser.add_argument("-l", "--log_level",
                                 default="INFO",
                                 choices=("INFO", "DEBUG", "WARNING", "ERROR"))

    def exception_hook(self, except_type, except_value, tb):
        """Handler for uncaught exception to dump well-formed error messages to stdout.
        """
        try:
            cause_tb = tb
            while cause_tb.tb_next:
                cause_tb = cause_tb.tb_next
            filename = cause_tb.tb_frame.f_code.co_filename
            name = cause_tb.tb_frame.f_code.co_name
            line_no = cause_tb.tb_lineno
            output = {
                "type": "{}.{}".format(except_type.__module__, except_type.__name__),
                "message": str(except_value),
                "file": filename,
                "name": name,
                "line": line_no
            }
            # Write to stdout with markers so that other messages do not interfere.
            print("<yb-python-error>{}</yb-python-error>".format(json.dumps(output)))
        except Exception as e:
            logging.error("Error processing exception. Error: ".format(str(e)))
        # Propagate the exception.
        sys.__excepthook__(except_type, except_value, tb)

        if isinstance(except_value, YBOpsExitCodeException):
            sys.exit(except_value.exitcode())

    def run(self):
        sys.excepthook = self.exception_hook
        self.register(argparse.ArgumentParser())
        self.options = self.parser.parse_args()

        log_level = getattr(logging, self.options.log_level.upper())
        init_env(log_level)
        """Execute the relevant callback function, according to the finalized subcommand that was
        pointed to, after full CLI parsing.
        """
        self.options.func(self.options)
