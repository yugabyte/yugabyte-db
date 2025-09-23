# Copyright 2019 YugabyteDB, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

import six


class YBOpsException(Exception):
    """Base exception class which we override we any exception within our YBOps process
    and we basically pass the exception type and message.
    """

    def __init__(self, type, message):
        """Method initializes exception type and exception message
        Args:
            type (str): exception type
            message (str): exception message
        """
        super().__init__(message)
        self.type = type

    def __str__(self):
        """Method returns the string representation for the exception."""
        return "{}: {}".format(self.type, get_exception_message(super(YBOpsException, self)))


class YBOpsRuntimeError(YBOpsException):
    """Runtime Error class is a subclass of YBOpsException, used to throw Runtime exceptions.
    """
    EXCEPTION_TYPE = "Runtime error"

    def __init__(self, message):
        super().__init__(self.EXCEPTION_TYPE, message)


class YBOpsExitCodeException(YBOpsException):
    def __init__(self, message):
        super().__init__("Custom exit code exception", message)

    "override in subclasses"
    def exitcode(self):
        return 1


class YBOpsRecoverableError(YBOpsExitCodeException):
    def __init__(self, message):
        super().__init__(message)

    def exitcode(self):
        return 3  # also see ShellResponse#ERROR_CODE_RECOVERABLE_ERROR in Java


class YBOpsFaultInjectionError(YBOpsException):
    def __init__(self, message):
        super().__init__("Fault injected", message)


def get_exception_message(exc):
    """Method to get exception message independent from python version.
    """
    return exc.message if six.PY2 else str(exc.args[0])
