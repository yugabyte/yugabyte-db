#!/usr/bin/env python2.7

# Kill all stuck yugabyte processes for a test invocation id.
# Uses exit code 3 if stuck processes were found (and killed).

import getopt
import psutil
import sys
import os
import signal

YB_PROCS = {"yb-master", "yb-tserver", "postgres", "initdb"}


def main():
    # Get the invocation id for the test whose processes we should clean up.
    test_invocation_id = os.environ.get('YB_TEST_INVOCATION_ID')

    # Kill any yb-processes that have this invocation id in their environment
    killed_stuck_processes = False
    for proc in psutil.process_iter():
        try:
            env = proc.environ()
            proc_invocation_id = env.get("YB_TEST_INVOCATION_ID")
            if test_invocation_id == proc_invocation_id:
                name = proc.name()
                if name in YB_PROCS:
                    proc.send_signal(signal.SIGKILL)
                    sys.stderr.write("Killed stuck process {} (pid={}) with SIGKILL.\n".format(
                        proc.cmdline(), proc.pid))
                    killed_stuck_processes = True
        except (psutil._exceptions.AccessDenied, psutil._exceptions.ZombieProcess), e:
            pass

    # If we found stuck processes error out so the caller process can handle it.
    if killed_stuck_processes:
        sys.exit(3)


if __name__ == '__main__':
    main()
