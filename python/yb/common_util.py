"""
Copyright (c) YugaByte, Inc.

This module provides common utility functions.
"""

import logging
import os
import sys


class Colors(object):
    """ ANSI color codes. """

    def __on_tty(x):
        if not os.isatty(sys.stdout.fileno()):
            return ""
        return x

    @classmethod
    def for_level(cls, log_level):
        if log_level == logging.ERROR:
            return cls.RED
        elif log_level == logging.WARNING:
            return cls.YELLOW
        elif log_level == logging.INFO:
            return cls.GREEN
        else:
            return cls.RESET

    RED = __on_tty("\x1b[31m")
    GREEN = __on_tty("\x1b[32m")
    YELLOW = __on_tty("\x1b[33m")
    RESET = __on_tty("\x1b[m")


def init_env(verbose):
    logging.basicConfig(
        level=logging.DEBUG if verbose else logging.INFO,
        format="%(asctime)s %(levelname)s: %(message)s")


def log_message(level, message):
    log_level = logging.getLogger().getEffectiveLevel()
    if level < log_level:
        return
    co_filename, co_firstlineno, _ = logging.getLogger().findCaller()
    message_with_file = "{}[{}:{}] {}{}".format(
        Colors.for_level(level), os.path.basename(co_filename),
        co_firstlineno, message, Colors.RESET)
    logging.log(level, message_with_file)
