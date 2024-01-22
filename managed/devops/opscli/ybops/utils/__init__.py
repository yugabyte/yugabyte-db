# !/usr/bin/env python
#
# Copyright 2019 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

from __future__ import print_function

import json
import logging
import os
import pipes
import platform
import random
import re
import socket
import string
import subprocess
import sys

from enum import Enum

from ybops.common.release import ReleasePackage
from ybops.common.colors import Colors
from ybops.common.exceptions import YBOpsRuntimeError
from ybops.utils.remote_shell import RemoteShell

BLOCK_SIZE = 4096
HOME_FOLDER = os.environ["HOME"]
YB_FOLDER_PATH = os.path.join(HOME_FOLDER, ".yugabyte")

RELEASE_VERSION_FILENAME = "version.txt"

# Home directory of node instances. Try to read home dir from env, else assume it's /home/yugabyte.
YB_HOME_DIR = os.environ.get("YB_HOME_DIR") or "/home/yugabyte"
# Sudo password for remote host.
YB_SUDO_PASS = os.environ.get("YB_SUDO_PASS")

# TTL in seconds for how long DNS records will be cached.
DNS_RECORD_SET_TTL = 5

# Minimum required resources on a VM.
MIN_MEM_SIZE_GB = 2
MIN_NUM_CORES = 2

DEFAULT_MASTER_HTTP_PORT = 7000
DEFAULT_MASTER_RPC_PORT = 7100
DEFAULT_TSERVER_HTTP_PORT = 9000
DEFAULT_TSERVER_RPC_PORT = 9100
DEFAULT_CQL_PROXY_HTTP_PORT = 12000
DEFAULT_CQL_PROXY_RPC_PORT = 9042
DEFAULT_YSQL_PROXY_HTTP_PORT = 13000
DEFAULT_YSQL_PROXY_RPC_PORT = 5433
DEFAULT_REDIS_PROXY_HTTP_PORT = 11000
DEFAULT_REDIS_PROXY_RPC_PORT = 6379
DEFAULT_NODE_EXPORTER_HTTP_PORT = 9300


def get_path_from_yb(path):
    return os.path.join(pipes.quote(YB_FOLDER_PATH), path)


# Home directory of the devops source tree. This is determined based on the yb_devops_home
# environment variable, which is set by wrapper shell scripts, or on the nearest directory that this
# Python codebase is part of that looks like the devops source directory. This is called
# YB_DEVOPS_HOME to distinguish it from the DEVOPS_HOME environment variable used in Yugaware.
YB_DEVOPS_HOME = None

# This variable is used inside provision_instance.py file.
# For yugabundle installations YB_DEVOPS_HOME contains version number, so we have to use a link
# to current devops folder. For the rest of cases this variable is equal to YB_DEVOPS_HOME.
YB_DEVOPS_HOME_PERM = None


def init_logging(log_level):
    """This method initializes ybops logging.

    Args:
        log_level (int): Log level that we want to initialize
    """
    logging.basicConfig(
        level=log_level,
        format="%(asctime)s %(levelname)s %(funcName)s:%(filename)s:%(lineno)d: %(message)s")


def is_devops_root_dir(devops_home):
    """
    Check if a particular directory looks like the root of the devops source root directory.  We
    don't assume that this directory is a git repository, as we may sometimes install a snapshot of
    the entire devops directory in a production location. This function is used when trying to
    determine the devops source root direcotry ("devops home" directory) based on the location of
    this Python file that is normally supposed to be installed inside a virtualenv located somewhere
    inside that source directory.
    """
    for subdir in ['bin', 'roles', 'vars']:
        if not os.path.isdir(os.path.join(devops_home, subdir)):
            return False
    return os.path.isfile(os.path.join(devops_home, 'ansible.cfg'))


def init_env(log_level):
    """This method initializes ybops environment variables.
    """
    init_logging(log_level)
    get_devops_home()


def get_devops_home():
    global YB_DEVOPS_HOME
    global YB_DEVOPS_HOME_PERM
    if YB_DEVOPS_HOME is None:
        devops_home = os.environ.get("yb_devops_home")
        if devops_home is None:
            this_file_dir = os.path.dirname(os.path.realpath(__file__))
            cur_dir = this_file_dir
            while cur_dir != '/':
                if is_devops_root_dir(cur_dir):
                    devops_home = cur_dir
                    break
                cur_dir = os.path.dirname(cur_dir)
            if devops_home is None:
                raise ValueError(
                    ("yb_devops_home environment variable is not set, and could not determine it " +
                     "from any of the parent directories of '{}'").format(this_file_dir))
        YB_DEVOPS_HOME = devops_home
        devops_home_link = os.environ.get("yb_devops_home_link")
        YB_DEVOPS_HOME_PERM = devops_home_link if devops_home_link is not None else YB_DEVOPS_HOME
    # If this is still None, we were not able to find it crawling up the tree, so let's fail to not
    # constantly be doing this...
    if YB_DEVOPS_HOME is None:
        raise YBOpsRuntimeError("Could not determine YB_DEVOPS_HOME")
    return YB_DEVOPS_HOME


def log_message(type, message):
    """This method lets you color code your log messages, based on the
    type of log (Info, Warning, Error).
    Args:
        type (int): Logging Type
        message (str: Log message
    """
    import inspect
    caller = inspect.currentframe().f_back.f_code
    message_with_file = "[{}:{}] {}".format(
        os.path.basename(caller.co_filename), caller.co_firstlineno, message)

    if type == logging.WARNING:
        logging.warning(Colors.YELLOW + message_with_file + Colors.RESET)
    elif type == logging.ERROR:
        logging.error(Colors.RED + message_with_file + Colors.RESET)
    else:
        logging.info(Colors.GREEN + message_with_file + Colors.RESET)


def confirm_prompt(prompt):
    """This method get a user to confirm y/n for a given prompt
    and returns appropriate boolean value.
    Args:
        prompt (str): Prompt message
    Returns:
        (boolean): Prompt response
    """
    if not os.isatty((sys.stdout.fileno())):
        print("Not running interactively. Assuming 'N'.", file=sys.stderr)
        return False

    # str(input) for py2-py3 compatbility.
    prompt_input = str(input("{} [Y/n]: ".format(prompt)).strip().lower())
    if prompt_input not in ['y', 'yes', '']:
        sys.exit(1)


def get_checksum(file_path, hasher):
    """This method calculates the checksum for a given file and the hasher
    method (which takes haslib hasher methods like (sha1, md5).
    Returns:
        (string): hex digest based on the hasher provided
    """
    with open(file_path, "rb") as f:
        # Read the file in 4KB chunks until EOF.
        for chunk in iter(lambda: f.read(BLOCK_SIZE), b''):
            hasher.update(chunk)
        return hasher.hexdigest()


def get_internal_datafile_path(file_name):
    """This method returns the data file path, based on where
    the package is installed, for an internal metadata file.

    This assumes a data/ folder sibling to the one of this script.
    This also assumes an internal/ folder, under the data/ folder.

    Args:
        file_name (str): data file name
    Returns:
        (str): datafile file path
    """
    package_dir = os.path.dirname(__file__)
    return os.path.realpath(os.path.join(package_dir, "..", "data", "internal", file_name))


def get_datafile_path(file_name):
    """This method returns the data file path, based on where
    the package is installed.

    Args:
        file_name (str): data file name
    Returns:
        (str): datafile file path
    """
    package_dir = os.path.dirname(__file__)
    return os.path.realpath(os.path.join(package_dir, "..", "data", file_name))


def get_default_release_version(repo_path=None):
    if not repo_path:
        repo_path = get_devops_home()
    version_file = os.path.join(repo_path, RELEASE_VERSION_FILENAME)
    if not os.path.isfile(version_file):
        raise YBOpsRuntimeError("Could not file version file: {}".format(version_file))
    version = open(version_file).read().strip()
    match = re.match(r"([.0-9]+(?:-rc\d+)?)(?:-b|\+)(\d+)", version)
    if not match:
        raise YBOpsRuntimeError("Invalid version format {}".format(version))
    return match.group(1)


def get_release_file(repository, release_name, build_type=None, os_type=None, arch_type=None):
    """This method checks the git commit sha and constructs
       the filename based on that and returns it.
    Args:
        repository (str): repository folder path where the release file exists
        release_file (str): release file name
        build_type (str): build type release/debug
        os_type (str): Type of os for cross-compilers like go
        arch_type (str): Type of arch for cross-compilers like go
    Returns:
        (str): Tar Filename
    """
    # Get the repo version information.
    base_version = get_default_release_version(repository)
    # Prepare the path for the release file.
    build_dir = os.path.join(repository, "build")
    if not os.path.exists(build_dir):
        # TODO: why are we mkdir-ing during a function that's supposed to return a path...
        os.makedirs(build_dir)

    cur_commit = str(subprocess.check_output(["git", "rev-parse", "HEAD"]).strip().decode('utf-8'))
    release = ReleasePackage.from_pieces(release_name, base_version, cur_commit, build_type,
                                         os_type, arch_type)
    file_name = release.get_release_package_name()
    return os.path.join(build_dir, file_name)


def is_valid_ip_address(ip_addr):
    """This method checks if string provided is a valid IP address
    Args:
        ip_addr (str): IP Address String
    Returns:
        (boolean): True/False
    """
    try:
        socket.inet_aton(ip_addr)
        return True
    except (socket.error, TypeError):
        return False


def generate_random_password(size=32):
    """This method would generate random alpha numeric password for the size provided
    Args:
        size(int): Size of the password, defaults to 32.
    Returns:
        password(str): Random alpha numeric password string.
    """
    return ''.join([random.choice(string.ascii_letters + string.digits) for _ in range(size)])


class ValidationResult(Enum):
    """Basically an enum for possible results.
    """
    VALID = "Valid"
    UNREACHABLE = "Could not find SSH running at the specified location"
    INVALID_SSH_KEY = "SSH Key is invalid"
    INVALID_MOUNT_POINTS = "Could not find specified mount points"
    INVALID_OS = "OS was not CentOS7"

    def __str__(self):
        return json.dumps({"state": self.name, "message": self.value})


def validate_instance(connect_options, mount_paths, **kwargs):
    """This method tries to connect to the host with the username provided on the port, executes a
    simple ls statement on the provided mount path and checks that the OS is centos-7. It returns
    0 if succeeded, 1 if ssh failed, 2 if mount path failed, or 3 if the OS was incorrect.
    Args:
        connect_options (dict): See RemoteShell for details.
    Returns:
        (dict): return success/failure code and corresponding message (0 = success, 1-3 = failure)
    """
    try:
        remote_shell = RemoteShell(connect_options)
        for path in [mount_path.strip() for mount_path in mount_paths]:
            path = '"' + re.sub('[`"]', '', path) + '"'
            stdout = remote_shell.exec_command("ls -a " + path + "", output_only=True)
            if len(stdout) == 0:
                return ValidationResult.INVALID_MOUNT_POINTS

        os_check_cmd = "source /etc/os-release && echo \"$NAME $VERSION_ID\""
        _, output, _ = remote_shell.exec_command(os_check_cmd)
        if len(output) == 0 or output[0].strip().lower() != "centos linux 7":
            return ValidationResult.INVALID_OS

        # If we get this far, then we succeeded
        return ValidationResult.VALID
    except YBOpsRuntimeError as ex:
        logging.error("[app] Failed to execute remote command: {}".format(ex))
        return ValidationResult.UNREACHABLE
    finally:
        if remote_shell:
            remote_shell.close()


def validate_cron_status(connect_options, **kwargs):
    """This method tries to connect to the host with the username provided on the port, checks if
    our expected cronjobs are present, and returns true if they are. Any failure, including
    connection issues will cause it to return false.
    Args:
        connect_options (dict): See RemoteShell for details.
    Returns:
        bool: true if all cronjobs are present, false otherwise (or if errored)
    """
    try:
        remote_shell = RemoteShell(connect_options)
        stdout = remote_shell.exec_command("crontab -l", output_only=True)
        cronjobs = ["clean_cores.sh", "zip_purge_yb_logs.sh", "yb-server-ctl.sh tserver"]
        return all(c in stdout for c in cronjobs)
    except YBOpsRuntimeError as ex:
        logging.error("Failed to validate cronjobs: {}".format(ex))
        return False
    finally:
        if remote_shell:
            remote_shell.close()


def remote_exec_command(connect_options, cmd, **kwargs):
    """This method will execute the given cmd on remote host and return the output.
    Args:
        connect_options (dict): See RemoteShell for details.
    Returns:
        rc (int): returncode
        stdout (str): output log
        stderr (str): error logs
    """
    try:
        remote_shell = RemoteShell(connect_options)
        rc, stdout, stderr = remote_shell.exec_command(cmd)
        return rc, stdout, stderr
    except YBOpsRuntimeError as e:
        logging.error("Failed to execute remote command: {}".format(e))
        return 1, None, None  # treat this as a non-zero return code
    finally:
        if remote_shell:
            remote_shell.close()


def get_or_create(getter):
    """This decorator would basically return a instance if already exists based on
    the getter method, if not it will call the create method to create new and returns."""
    def wrap_get(create_func):
        def _get_or_create(**kwargs):
            result = getter(**kwargs)
            return create_func(**kwargs) if not result else result
        return _get_or_create
    return wrap_get


def get_and_cleanup(getter):
    """This decorator would basically return None if no matching record found based on the
    getter method. If there is a record, we would call cleanup routine to delete that object.
    """
    def wrap_get(cleanup_func):
        def _get_and_cleanup(**kwargs):
            instance = getter(**kwargs)
            return cleanup_func(instance, **kwargs) if instance else None
        return _get_and_cleanup
    return wrap_get


class OSType(Enum):
    """Basically an enum for possible results.
    """
    LINUX = "Linux"
    OSX = "MacOS"
    INVALID = "Unsupported OS"

    def __str__(self):
        return json.dumps({"state": self.name, "message": self.value})


def _get_ostype():
    system = platform.system()
    if system == 'Darwin':
        return OSType.OSX
    elif system == 'Linux':
        return OSType.LINUX
    else:
        return OSType.INVALID


def is_linux():
    return _get_ostype() == OSType.LINUX


def is_mac():
    return _get_ostype() == OSType.OSX


def linux_get_ip_address(ifname):
    """Get the inet ip address of this machine (as shown by ifconfig). Assumes linux env.
    """
    return str(subprocess.check_output(["hostname", "--ip-address"]).decode("utf-8").strip())


# Given a comma separated string of paths on a remote host
# and connect_options to connect to the remote host
# returns a comma separated string of the root mount paths for those paths
def get_mount_roots(connect_options, paths):
    remote_shell = RemoteShell(connect_options)
    remote_cmd = 'df --output=target {}'.format(" ".join(paths.split(",")))
    # Example output of the df cmd
    # $ df --output=target /bar/foo/rnd /storage/abc
    # Mounted on
    # /bar
    # /storage

    mount_roots = remote_shell.run_command(remote_cmd).stdout.split('\n')[1:]
    return ",".join(
        [mroot.strip() for mroot in mount_roots if mroot.strip()]
    )
