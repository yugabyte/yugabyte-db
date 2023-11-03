#!/usr/bin/env python3

#
# Copyright (c) YugaByte, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations
# under the License.
#

"""
Kill long-running mini-cluster daemons. Used on Mac Mini Jenkins hosts.
"""

import subprocess
import re
import logging
import os
from datetime import datetime

# Parsing a line of the form:
# 6181 Mon Apr  2 15:50:48 2018 <command_line>
PS_OUTPUT_LINE_RE = re.compile(r'^\s*(\d+)\s+\S+\s+(\S+\s+\d+\s\d+:\d+:\d+ \d+)\s+(.*)$')


def main() -> None:
    logging.basicConfig(
        level=logging.INFO,
        format="[%(filename)s:%(lineno)d] %(asctime)s %(levelname)s: %(message)s")

    ps_output = subprocess.check_output(['ps', '-e', '-o', 'pid,lstart,command']).decode('utf-8')
    current_time = datetime.now()
    threshold_sec = 3600

    num_killed = 0
    num_failed_to_kill = 0
    num_processes_found = 0
    num_daemons_found = 0
    for line in ps_output.split("\n"):
        line = line.strip()
        if line.split() == ['PID', 'STARTED', 'COMMAND']:
            continue
        if not line:
            continue

        match = PS_OUTPUT_LINE_RE.match(line)
        if not match:
            logging.warn("Could not parse ps output line: %s", line)
            continue

        num_processes_found += 1

        # Need to find commands of the form
        # [...]/var/lib/jenkins/.../yb-(master|tserver) <parameters>
        command_line = match.group(3)
        command_path = command_line.split()[0]
        command_basename = os.path.basename(command_path)
        if command_basename not in ['yb-master', 'yb-tserver']:
            continue

        num_daemons_found += 1

        if '/var/lib/jenkins/' not in command_path:
            continue

        pid = int(match.group(1))
        start_time = datetime.strptime(match.group(2), '%b %d %H:%M:%S %Y')
        elapsed_time_sec = (current_time - start_time).total_seconds()
        if elapsed_time_sec < threshold_sec:
            continue

        logging.info(
            "Killing a long-running master/tserver process. PID: %d, running time: %d second, "
            "command line: %s", pid, elapsed_time_sec, command_line)
        if os.system('kill -9 %d' % pid) >> 8 == 0:
            num_killed += 1
        else:
            logging.info("Failed to kill process %d", pid)
            num_failed_to_kill += 1

    logging.info(
        "Examined %d processes, found %d masters/tservers, killed %d long-running masters/tservers"
        ", failed to kill %d processes.", num_processes_found, num_daemons_found, num_killed,
        num_failed_to_kill)


if __name__ == '__main__':
    main()
