# This file is part of Ansible
#
# Ansible is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Ansible is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Ansible.  If not, see <http://www.gnu.org/licenses/>.
#
# The following only applies to changes made to this file as part of YugabyteDB development.
#
# Portions copyright 2019 YugabyteDB, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

# Make coding more python3-ish
from __future__ import (absolute_import, division, print_function)
__metaclass__ = type

from ansible import constants as C
from ansible.plugins.callback.default import CallbackModule as CallbackModule_default


class CallbackModule(CallbackModule_default):

    '''
    This is the pipe_to_stderr callback plugin, which reuses the default
    callback plugin but sends error output to stderr.
    '''

    CALLBACK_VERSION = 2.0
    CALLBACK_TYPE = 'stdout'
    CALLBACK_NAME = 'pipe_to_stderr'
    CALLBACK_NEEDS_WHITELIST = False

    def __init__(self):

        self.super_ref = super(CallbackModule, self)
        self.super_ref.__init__()

    def v2_runner_on_failed(self, result, ignore_errors=False):

        delegated_vars = result._result.get('_ansible_delegated_vars', None)
        self._clean_results(result._result, result._task.action)

        if self._play.strategy == 'free' and self._last_task_banner != result._task._uuid:
            self._print_task_banner(result._task)

        self._handle_exception(result._result, errors_to_stderr=True)
        self._handle_warnings(result._result)

        if result._task.loop and 'results' in result._result:
            self._process_items(result)

        else:
            msg = "fatal: "
            if delegated_vars:
                msg += "[%s -> %s]: FAILED! => %s" % (result._host.get_name(),
                                                      delegated_vars['ansible_host'],
                                                      self._dump_results(result._result))
            else:
                msg += "[%s] FAILED! => %s" % (result._host.get_name(),
                                               self._dump_results(result._result))
            # Send to both stdout and stderr.
            self._display.display(msg, color=C.COLOR_ERROR)
            self._display.display(msg, color=C.COLOR_ERROR, stderr=True)

        if ignore_errors:
            self._display.display("...ignoring", color=C.COLOR_SKIP)

    def _handle_exception(self, result, errors_to_stderr=False):

        if 'exception' in result:
            msg = "An exception occurred during task execution. "
            if self._display.verbosity < 3:
                # extract just the actual error message from the exception text
                error = result['exception'].strip().split('\n')[-1]
                msg += "To see the full traceback, use -vvv. The error was: %s" % error
            else:
                msg = "The full traceback is:\n" + result['exception']
                del result['exception']

            # Always send to stdout. If error, also send to stderr.
            self._display.display(msg, color=C.COLOR_ERROR)
            if errors_to_stderr:
                self._display.display(msg, color=C.COLOR_ERROR, stderr=errors_to_stderr)
