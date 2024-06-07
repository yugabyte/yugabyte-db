#!/usr/bin/env python

import time
import errno
import logging
import os
import shutil
import signal
import subprocess
import sys
import glob
import traceback
import threading
# Stop event to signal the background thread to stop.:
bg_thread_stop_event = threading.Event()
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
# Check if environment overrides it, if not use the default.
PG_UNIX_SOCKET_LOCK_FILE_PATH_GLOB = os.environ.get(
    "PG_UNIX_SOCKET_LOCK_FILE_PATH_GLOB",
    "/tmp/.yb.*/.s.PGSQL.*.lock")   # Default pattern
MASTER_BINARY = "/home/yugabyte/bin/yb-master"


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


def delete_pg_lock_files():
    file_pattern = PG_UNIX_SOCKET_LOCK_FILE_PATH_GLOB
    files_to_delete = glob.glob(file_pattern)
    # Check if any files were found
    if not files_to_delete:
        logging.info(f"No files found matching the pattern: {file_pattern}")
        return
    # Log a list of files to delete.
    logging.info(f"Files to delete after expanded glob: {files_to_delete}")

    # We don't want to catch exceptions but raise them as this will
    # in debugging; pod goes into crashloop.
    for file_path in files_to_delete:
        logging.info(f"Trying to delete {file_path}")
        os.remove(file_path)
        logging.info(f"Deleted: {file_path}")


def background_copy_cores_wrapper(dst, sleep_time_seconds):
    logging.info("Starting Collection")
    global bg_thread_stop_event
    copy_count = None
    while not bg_thread_stop_event.is_set():
        logging.info("Invoking copy_cores")
        try:
            copy_count = copy_cores(dst)
        except Exception as e:
            logging.error(
                "Error while copying cores: {}, traceback: {}".format(
                    e, traceback.format_exc()))
        logging.info("Copied {} core files".format(copy_count))
        logging.info("Sleeping for {} seconds".format(sleep_time_seconds))
        time.sleep(sleep_time_seconds)


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
    core_files = glob.glob(core_glob)

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


def invoke_hook(hook_stage=None):
    """
    Invokes the kubernetes configmap hook with associated hook_stage.
    """
    if hook_stage not in ("pre", "post"):
        raise Exception("NotImplemented")
    hostname = os.getenv("HOSTNAME")

    hostname_parsed = "-".join(hostname.split("-")[-3:])
    hook_filename = "{}-{}_debug_hook.sh".format(hostname_parsed, hook_stage)
    op_name = "{}_{}_{}".format(hostname, hook_stage, "debug_hook")
    hook_filepath = os.path.join("/opt/debug_hooks_config/", hook_filename)
    if os.path.exists(hook_filepath):
        logging.info("Executing operation: {} filepath: {}".format(op_name, hook_filepath))
        # TODO: Do we care about capturing ret code,
        # exception since exceptions will be logged by default
        output = subprocess.check_output(hook_filepath, shell=True)
        logging.info("Output from hook {}".format(output))


def is_master(command):
    if MASTER_BINARY in command:
        return True
    return False


if __name__ == "__main__":
    logging.basicConfig(
        format="%(asctime)s [%(levelname)s] %(filename)s: %(message)s",
        level=logging.INFO,
    )
    core_collection_interval = 30  # Seconds
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
    # Copy cores once in 30s
    copy_cores_thread = threading.Thread(
        target=background_copy_cores_wrapper, args=(cores_dir, core_collection_interval))
    copy_cores_thread.daemon = True
    copy_cores_thread.start()
    # Delete PG Unix Socket Lock files that can be left after a previous
    # ungraceful exit of the container.
    if not is_master(command):
        _delete_pg_lock_file = os.environ.get(
            "YB_DEL_PG_LOCK_FILE", "true").lower() in ('true', '1', 't')
        logging.info("Deleting PG socket lock files: %s" % _delete_pg_lock_file)
        if _delete_pg_lock_file:
            delete_pg_lock_files()
    # invoking the prehook.
    invoke_hook(hook_stage="pre")

    child_process = subprocess.Popen(command)

    # TODO/RFC: how to handle the failures which happen after
    # this point? Need some way to terminate the DB process?

    # Set signal handler.
    for sig in PROPAGATE_SIGS:
        signal.signal(sig, signal_handler)

    child_process.wait()

    # invoking the Post hook
    invoke_hook(hook_stage="post")
    bg_thread_stop_event.set()
    # Give some time for the thread to finish its job,
    # we do a 2x core collection interval.
    copy_cores_thread.join(2*core_collection_interval)
    # There may be left over cores when we crash, so copy them now.
    # Do the core copy, and exit with child return code.
    files_copied = copy_cores(cores_dir)
    logging.info("Copied {} core files to '{}'".format(files_copied, cores_dir))
    sys.exit(child_process.returncode)
