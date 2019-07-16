# Copyright 2019 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

import os
import sys


class Colors(object):
    """ ANSI color codes. """

    def __on_tty(x):
        if not os.isatty(sys.stdout.fileno()):
            return ""
        return x

    RED = __on_tty("\x1b[31m")
    GREEN = __on_tty("\x1b[32m")
    YELLOW = __on_tty("\x1b[33m")
    RESET = __on_tty("\x1b[m")
