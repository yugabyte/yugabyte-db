#!/usr/bin/env python

import errno
import logging
import os
import shutil
import signal
import subprocess
import sys
from glob import glob

child_process = None

# Set of signals which are propagated to the child process from this
# script. Aim here is to make sure we propagate at least the ones
# which container runtime is supposed to send us. Explanation for some
# of the signals which are not handled/propagated:
#
# SIGCHLD: This script is supposed to be run inside Tini which takes
#          care of adopting the child processes, so we leave that
#          responsibility to Tini itself.
#
# SIGKILL:
# SIGSTOP: These cannot be blocked/handled.
#
# SIGTTIN:
# SIGTTOU:
# SIGFPE :
# SIGILL :
# SIGSEGV:
# SIGBUS :
# SIGABRT:
# SIGTRAP:
# SIGSYS : Tini does not propagate these signals to child process:
#          https://github.com/krallin/tini/blob/378bbbc8909/src/tini.c#L465
PROPAGATE_SIGS = {
    signal.SIGTERM,
    signal.SIGHUP,
    signal.SIGINT,
    signal.SIGQUIT,
    signal.SIGUSR1,
    signal.SIGUSR2,
}
MAX_FILES_FROM_GLOB = 5
CORE_GLOB = "*core*"


def signal_handler(signum, frame):
    logging.info("sending {} to the child process".format(signum))
    if child_process is not None:
        child_process.send_signal(signum)


def get_core_dump_dir():
    core_pattern = None
    with open("/proc/sys/kernel/core_pattern") as core_pattern_file:
        core_pattern = core_pattern_file.readline()
    logging.info("core_pattern is: {}".format(core_pattern.rstrip()))
    if core_pattern.startswith("|"):
        raise ValueError("core_pattern starts with |, can't do anything useful")
    # abspath resolves any relative path from core_pattern. This
    # script and the child process have same CWD, so the following
    # call resolves to correct directory.
    return os.path.abspath(os.path.dirname(core_pattern))


def create_core_dump_dir():
    """This function tries to create the base directory from the
    core_pattern.

    Any failures in creating the directory are logged as warnings.

    """
    try:
        core_dump_dir = get_core_dump_dir()
        os.makedirs(core_dump_dir)
    except Exception as error:
        # TODO: use os.makedirs(core_dump_dir, exist_ok=True) when we
        # move to newer enterprise Linux with Python 3.
        if isinstance(error, OSError) and error.errno == errno.EEXIST:
            # Don't warn if the directory already exist.
            pass
        else:
            logging.warning(
                "Core dumps might not get collected: "
                + "failure while creating the core dump directory: "
                + "{}: {}".format(type(error).__name__, error)
            )


def copy_cores(dst):
    # os.makedirs(dst, exist_ok=True)
    try:
        os.makedirs(dst)
    except OSError as error:
        # Don't raise error if the directory already exist.
        if error.errno != errno.EEXIST:
            raise error

    total_files_copied = 0
    dir_path = get_core_dump_dir()

    if os.path.samefile(dir_path, dst):
        logging.info(
            "Skipping copy of core files: '{}' and '{}' are the same directories".format(
                dir_path, dst
            )
        )
        return total_files_copied

    # TODO: parse the core_pattern to generate glob pattern instead of
    # using simple glob

    basename_glob = CORE_GLOB
    core_glob = os.path.join(dir_path, basename_glob)
    logging.info(
        "Copying latest {} core files to '{}' using glob '{}'".format(
            MAX_FILES_FROM_GLOB, dst, core_glob
        )
    )
    core_files = glob(core_glob)

    # TODO: handle cases where this list is huge, this is less likely
    # to happen with the current glob *core*, but can happen with a
    # generated one.
    # Sort the files, latest first.
    core_files.sort(key=os.path.getmtime, reverse=True)
    for core_file in core_files:
        if total_files_copied == MAX_FILES_FROM_GLOB:
            logging.info("Reached max allowed core files, skipping the rest")
            break
        if not os.path.isfile(core_file):
            logging.info("'{}' is not a regular file, skipping".format(core_file))
            continue
        logging.info("Copying core file '{}'".format(core_file))
        shutil.copy(core_file, dst)
        total_files_copied += 1
    return total_files_copied


if __name__ == "__main__":
    logging.basicConfig(
        format="%(asctime)s [%(levelname)s] %(filename)s: %(message)s",
        level=logging.INFO,
    )

    command = sys.argv[1:]
    if len(command) < 1:
        logging.critical("No command to run")
        sys.exit(1)

    cores_dir = os.getenv("YBDEVOPS_CORECOPY_DIR")
    if cores_dir is None:
        logging.critical("YBDEVOPS_CORECOPY_DIR environment variable must be set")
        sys.exit(1)
    logging.info("Core files will be copied to '{}'".format(cores_dir))

    # Make sure the directory from core_pattern is present, otherwise
    # core dumps are not collected.
    create_core_dump_dir()

    child_process = subprocess.Popen(command)

    # TODO/RFC: how to handle the failures which happen after
    # this point? Need some way to terminate the DB process?

    # Set signal handler.
    for sig in PROPAGATE_SIGS:
        signal.signal(sig, signal_handler)

    child_process.wait()

    # Do the core copy, and exit with child return code.
    files_copied = copy_cores(cores_dir)
    logging.info("Copied {} core files to '{}'".format(files_copied, cores_dir))
    sys.exit(child_process.returncode)
