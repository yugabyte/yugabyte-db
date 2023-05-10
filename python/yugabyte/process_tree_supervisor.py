#!/usr/bin/env python

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

# A tool for cleaning up stray child processes of a test case program.

import psutil
import time
import sys
import argparse
import signal
import os
import logging

from typing import List, Any, Optional, Set


# https://stackoverflow.com/questions/2549939/get-signal-names-from-numbers-in-python/35996948
SIGNAL_TO_NAME = dict(
    (k, v) for v, k in reversed(sorted(signal.__dict__.items()))
    if v.startswith('SIG') and not v.startswith('SIG_')
)


g_supervisor = None
g_stop_requested = False
g_signal_caught = None


def get_cmdline(process: Any) -> Optional[List[str]]:
    try:
        return process.cmdline()
    except psutil.NoSuchProcess as e:
        logging.warning("Newly added child process disappeared right away")
        return None
    except psutil.AccessDenied as e:
        logging.warning(
            "Access denied trying to get the command line for pid %d (%s), "
            "ignoring this process", process.pid, str(e))
        return None
    except OSError as e:
        logging.warning(
            "OSError trying to get the command line for pid %d (%s), ignoring this process",
            process.pid, str(e))
        return None


def get_cmdline_for_log(process: Any) -> List[str]:
    cmdline = get_cmdline(process)
    if cmdline:
        return cmdline
    return ['failed to get cmdline for pid %d' % process.pid]


def get_process_by_pid(pid: int) -> Optional[psutil.Process]:
    try:
        process = psutil.Process(pid)
        if process.status() == psutil.STATUS_ZOMBIE:
            return None
        return process
    except psutil.NoSuchProcess as e:
        return None


class ProcessTreeSupervisor():
    args: argparse.Namespace
    ancestor_pid: int
    pid_set: Set[int]
    my_pid: int

    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.ancestor_pid = args.pid
        self.pid_set = set([self.ancestor_pid])
        self.my_pid = os.getpid()

    def run(self) -> None:
        start_time_sec = time.time()
        while g_signal_caught is None:
            elapsed_time_sec = time.time() - start_time_sec
            if self.args.timeout_sec is not None and elapsed_time_sec > self.args.timeout_sec:
                logging.info(
                    "Timeout exceeded: %.1f > %d seconds, supervisor script will now exit.",
                    elapsed_time_sec, self.args.timeout_sec)
                break
            ancestor_terminated = False

            changes_made = True
            inner_loop_iterations = 0
            while changes_made:
                new_pids = set()
                removed_pids = set()
                changes_made = False
                for existing_pid in self.pid_set:
                    process = get_process_by_pid(existing_pid)
                    if process is None:
                        self.process_terminated(existing_pid)
                        removed_pids.add(existing_pid)
                        changes_made = True
                        if existing_pid == self.ancestor_pid:
                            ancestor_terminated = True
                        continue

                    try:
                        children = process.children(recursive=True)
                    except psutil.NoSuchProcess as e:
                        logging.warning(
                            "Process %d disappeared when trying to list its children",
                            existing_pid)
                        continue

                    for child_process in children:
                        if (child_process.pid not in self.pid_set and
                                child_process.pid not in new_pids and
                                # If this script is a child of the monitored process, don't track
                                # our own pid.
                                child_process.pid != self.my_pid):
                            if self.new_process_found(child_process):
                                new_pids.add(child_process.pid)
                                changes_made = True
                self.pid_set = self.pid_set.union(new_pids).difference(removed_pids)
                inner_loop_iterations += 1
                if inner_loop_iterations >= 100:
                    logging.warning("Inner loop spun for %d iterations, breaking")
                    break

            if ancestor_terminated:
                break
            time.sleep(1)
        self.handle_termination()

    def new_process_found(self, process: psutil.Process) -> bool:
        pid = process.pid
        cmdline = get_cmdline(process)
        if not cmdline:
            return False

        logging.info("Tracking a child pid: %s: %s", pid, cmdline)
        return True

    def process_terminated(self, existing_pid: int) -> None:
        logging.info("Process finished by itself: %d", existing_pid)

    def report_stray_process(self, process: psutil.Process, will_kill: bool = False) -> None:
        # YB_STRAY_PROCESS is something we'll grep for in an enclosing script.
        logging.info(
            "YB_STRAY_PROCESS: Found a stray process%s: pid: %d, command line: %s",
            (', killing' if will_kill else ', not killing'),
            process.pid, get_cmdline_for_log(process))

    def handle_termination(self) -> None:
        killed_pids = []
        for pid in self.pid_set:
            if pid != self.ancestor_pid:
                process = get_process_by_pid(pid)
                if process is None:
                    self.process_terminated(pid)
                else:
                    self.report_stray_process(process, will_kill=self.args.terminate_subtree)
                    if self.args.terminate_subtree:
                        try:
                            os.kill(process.pid, signal.SIGKILL)
                            killed_pids.append(process.pid)
                        except ProcessLookupError:
                            self.process_terminated(pid)

        num_still_running = 0
        for killed_pid in killed_pids:
            process = get_process_by_pid(killed_pid)
            if process is not None:
                num_still_running += 1
                logging.warning("Stray process still running: %d, cmd line: %s",
                                process.pid, get_cmdline_for_log(process))

        if killed_pids and num_still_running == 0:
            logging.info("All %d stray processes successfully killed", len(killed_pids))


def signal_handler(signum: int, unused_frame: Any) -> None:
    global g_signal_caught
    g_signal_caught = signum


def main() -> None:
    signal.signal(signal.SIGUSR1, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    signal.signal(signal.SIGINT, signal_handler)

    parser = argparse.ArgumentParser(
        description='Supervise the process subtree of a given process.')
    parser.add_argument(
        '--pid', '-p',
        help='PID of the process whose tree of child processes to supervise',
        type=int,
        required=True)
    parser.add_argument(
        '--terminate-subtree',
        help='Terminate all the child processes of the parent process recursively that are still '
             'alive, either when the parent process stops, or when this program receives a '
             'SIGTERM, SIGINT (^C), or SIGUSR1. This does not terminate the parent process itself.',
        action='store_true'
    )
    parser.add_argument(
        '--log-to-file',
        help='Write log to a file'
    )
    parser.add_argument(
        '--timeout-sec',
        help="This script will exit, and if --terminate-subtree is specified, all its children will"
             "be recursively terminated. The process itself will still be kept alive so it can "
             "do any necessary cleanup.",
        type=int
    )

    args = parser.parse_args()
    logging.basicConfig(
        level=logging.INFO,
        format="[%(filename)s:%(lineno)d] %(asctime)s %(levelname)s: %(message)s",
        filename=args.log_to_file)

    logging.info(
        "Monitoring process subtree of pid %d (this script's pid: %d)", args.pid, os.getpid())

    global g_supervisor
    g_supervisor = ProcessTreeSupervisor(args)
    g_supervisor.run()

    logging.info(
        "Supervisor of pid %d's process tree (this script's pid: %d) is terminating%s",
        args.pid, os.getpid(),
        ('' if g_signal_caught is None else
         ' (caught signal %d / %s)' % (
             g_signal_caught, SIGNAL_TO_NAME.get(g_signal_caught, 'unknown'))))


if __name__ == '__main__':
    main()
    exit_code = 0
    if g_signal_caught in [signal.SIGINT, signal.SIGTERM]:
        # For these signals, simulate normal UNIX behavior.
        exit_code = 128 + g_signal_caught
    # But for SIGUSR1 just exit with 0.
    logging.info("Supervisor process %d is exiting with code %d", os.getpid(), exit_code)
    sys.exit(exit_code)
