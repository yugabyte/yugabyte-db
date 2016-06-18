#!/usr/bin/env python
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
# This script generates a header file which contains definitions
# for the current Kudu build (eg timestamp, git hash, etc)

import os
import subprocess
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


def check_output(*popenargs, **kwargs):
  r"""Run command with arguments and return its output as a byte string.
  Backported from Python 2.7 as it's implemented as pure python on stdlib.
  >>> check_output(['/usr/bin/python', '--version'])
  Python 2.6.2
  """
  process = subprocess.Popen(stdout=subprocess.PIPE, *popenargs, **kwargs)
  output, unused_err = process.communicate()
  retcode = process.poll()
  if retcode:
    cmd = kwargs.get("args")
    if cmd is None:
      cmd = popenargs[0]
    error = subprocess.CalledProcessError(retcode, cmd)
    error.output = output
    raise error
  return output


def confirm_prompt(prompt):
  """
  Issue the given prompt, and ask the user to confirm yes/no. Returns true
  if the user confirms.
  """
  while True:
    print prompt, "[Y/n]:",

    if not os.isatty(sys.stdout.fileno()):
      print "Not running interactively. Assuming 'N'."
      return False
      pass

    r = raw_input().strip().lower()
    if r in ['y', 'yes', '']:
      return True
    elif r in ['n', 'no']:
      return False


def get_my_email():
  """ Return the email address in the user's git config. """
  return check_output(['git', 'config', '--get', 'user.email']).strip()
