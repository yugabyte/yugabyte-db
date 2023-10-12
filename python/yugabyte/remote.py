"""
Copyright (c) YugaByte, Inc.

This module provides utility and helper functions to work with a remote server through SSH.
"""

import argparse
import json
import logging
import os
import re
import shlex
import subprocess
import sys
import time

from typing import Sequence, Union, Tuple, List, Set, Optional, Dict, Any

from yugabyte.common_util import shlex_join

REMOTE_BUILD_HOST_ENV_VAR = 'YB_REMOTE_BUILD_HOST'
DEFAULT_UPSTREAM = 'origin'
CONFIG_FILE_PATH = '~/.yb_remote_build.json'

# Allow to prefix the branch name with e.g. "2.18_" so we can auto-detect the upstream branch to
# use.
LOCAL_BRANCH_PREFIX_RE = re.compile(r'^([0-9](?:[.][0-9]+)+)_.*')


def check_output(args: List[str]) -> str:
    logging.debug("Running command: %s", shlex_join(args))
    bytes = subprocess.check_output(args)
    return bytes.decode('utf-8')


def check_output_line(args: List[str]) -> str:
    return check_output(args).strip()


def check_output_lines(args: List[str]) -> List[str]:
    return [file.strip() for file in check_output(args).split('\n')]


def parse_git_diff_name_status(lines: List[str]) -> Tuple[List[str], List[str]]:
    files: Tuple[List[str], List[str]] = ([], [])
    for line in lines:
        tokens = [x.strip() for x in line.split('\t')]
        if len(tokens) == 0 or tokens[0] == '':
            continue
        if tokens[0] == 'D':
            files[1].append(tokens[1])
            continue
        if tokens[0].startswith('R'):
            name = tokens[2]
        else:
            name = tokens[1]
        files[0].append(name)
    return files


def get_ssh_cmd_line(host: str, extra_ssh_args: List[str], remote_command: List[str]) -> List[str]:
    return ['ssh'] + extra_ssh_args + [host] + remote_command


def remote_communicate(
      host: str,
      remote_command: Union[str, Sequence[str]],
      extra_ssh_args: List[str],
      error_ok: bool = False) -> bool:
    remote_command_args: List[str]
    if isinstance(remote_command, list):
        remote_command_args = remote_command
    elif isinstance(remote_command, str):
        remote_command_args = [remote_command]
    else:
        raise ValueError("Invalid remote command specified: %s" % remote_command)

    args = get_ssh_cmd_line(
        host=host,
        extra_ssh_args=extra_ssh_args,
        remote_command=remote_command_args)

    logging.info("Running command: %s", shlex_join(args))
    proc = subprocess.Popen(args, shell=False)
    proc.communicate()
    if proc.returncode != 0:
        if error_ok:
            return False
        raise RuntimeError('ssh terminated with code {}'.format(proc.returncode))
    return True


def check_remote_files(
        escaped_remote_path: str,
        host: str,
        remote_path: str,
        extra_ssh_args: List[str],
        files: List[str]) -> None:
    remote_command_str = 'cd {0} && git diff --name-status'.format(escaped_remote_path)
    remote_changed, remote_deleted = parse_git_diff_name_status(
        check_output_lines(get_ssh_cmd_line(
            host=host,
            extra_ssh_args=extra_ssh_args,
            remote_command=[remote_command_str])))
    unexpected = []
    for changed in remote_changed:
        if changed not in files:
            unexpected.append(changed)
    if unexpected:
        command = 'cd {0}'.format(remote_path)
        message = 'Reverting:\n'
        for file_path in unexpected:
            message += '  {0}\n'.format(file_path)
            command += ' && git checkout -- {0}'.format(shlex.quote(file_path))
        print(message)
        remote_communicate(
            host=host,
            remote_command=command,
            extra_ssh_args=extra_ssh_args)


def remote_output_line(
        host: str,
        remote_path: str,
        command: str,
        extra_ssh_args: List[str]) -> str:
    return check_output_line(get_ssh_cmd_line(
        host=host,
        extra_ssh_args=extra_ssh_args,
        remote_command=['cd {0} && {1}'.format(remote_path, command)]
    ))


def fetch_remote_commit(
        host: str,
        remote_path: str,
        extra_ssh_args: List[str]) -> str:
    return remote_output_line(
        host=host,
        remote_path=remote_path,
        command='git rev-parse HEAD',
        extra_ssh_args=extra_ssh_args)


def read_config_file() -> Optional[Dict[str, Any]]:
    conf_file_path = os.path.expanduser(CONFIG_FILE_PATH)
    if not os.path.exists(conf_file_path):
        logging.debug("Configuration file not found at %s", CONFIG_FILE_PATH)
        return None
    with open(conf_file_path) as conf_file:
        logging.debug("Reading configuratino file at %s", CONFIG_FILE_PATH)
        return json.load(conf_file)


def apply_default_host_value(host: Optional[str]) -> str:
    """
    Process the host argument if it's not defined, setting it to env var
    and raising a RuntimeError if it's undefined too.
    :param host: host input argument
    :return: host to use
    """
    if host is None and REMOTE_BUILD_HOST_ENV_VAR in os.environ:
        host = os.environ[REMOTE_BUILD_HOST_ENV_VAR]

    if host is None:
        raise RuntimeError("Please specify host with --host option or {} variable\n".format(
            REMOTE_BUILD_HOST_ENV_VAR))

    return host


def load_profile(
        args: argparse.Namespace,
        profile_name: Optional[str]) -> None:
    """
    Loads the profile from config file if it is defined, initializing some arguments in the command
    line args map with the ones from profile, if they were not specified on the command line.
    Also appends 'extra_args' from profile to 'build_args' in args map.

    :param args: parsed CLI arguments map to init missing values with the loaded args
    :param profile_name: name of the profile to load
    """
    conf: Optional[Dict[str, Any]] = read_config_file()

    if not conf:
        return None

    if not profile_name:
        profile_name = conf.get("default_profile", profile_name)

    if profile_name is None:
        return None

    profiles = conf['profiles']
    profile = profiles.get(profile_name)
    if profile:
        logging.info("Using profile %s", profile_name)
    else:
        # Match profile using the remote host.
        for profile_name_to_try in profiles:
            if profiles[profile_name_to_try].get('host') == profile_name:
                logging.info("Using profile %s based on the host name", profile_name_to_try)
                profile = profiles[profile_name_to_try]
                break

    if profile is None:
        raise RuntimeError("Unknown profile '%s'" % profile_name)

    if 'remote_path' not in profile and args.remote_path is None:
        # Automatically figure out the remote path based on directory substitutions specified in the
        # profile. E.g. one could specify
        # "code_directory_substitutions": [{
        #   "local": "~/code",
        #   "remote": "/home/centos/code"
        # }]
        # and then run remote_build.py in ~/code/yugabyte-db and the build will run in
        # /home/centos/code/yugabyte-db on the remote host.
        cur_dir = os.getcwd()
        cur_dir_variants = [os.path.abspath(cur_dir), os.path.realpath(cur_dir)]
        for substitution in profile.get('code_directory_substitutions', []):
            local_code_dir = os.path.expanduser(substitution['local'])
            remote_code_dir = os.path.expanduser(substitution['remote'])

            for cur_dir_variant in cur_dir_variants:
                for local_code_dir_variant in [
                        os.path.abspath(local_code_dir), os.path.realpath(local_code_dir)]:
                    if cur_dir_variant == local_code_dir_variant:
                        args.remote_path = remote_code_dir
                    elif cur_dir_variant.startswith(local_code_dir_variant + '/'):
                        args.remote_path = os.path.join(
                            remote_code_dir,
                            os.path.relpath(cur_dir_variant, local_code_dir_variant))

                if args.remote_path is not None:
                    break

            if args.remote_path is not None:
                logging.info(
                    "Auto-detected remote path as %s based on current directory %s using the "
                    "substitution %s in the profile",
                    args.remote_path, cur_dir, json.dumps(substitution))
                break

    for arg_name in ['host', 'remote_path', 'branch', 'upstream']:
        if getattr(args, arg_name) is None:
            setattr(args, arg_name, profile.get(arg_name))
    if args.build_args is None:
        args.build_args = ''
    extra_args = shlex_join(profile.get('extra_args', []))
    if extra_args:
        args.build_args += ' ' + extra_args


def sync_changes(
      host: str,
      branch: str,
      remote_path: str,
      wait_for_ssh: bool,
      upstream: str,
      extra_ssh_args: List[str]) -> str:
    """
    Push local changes to a remote server.

    :param host: remote host
    :param branch: branch used as a base for local changes
    :param remote_path: path to yugabyte directory on a remote server
    :param wait_for_ssh: whether script should wait for host to become accessible via SSH
    :return: escaped remote path made absolute
    """
    commit = check_output_line(['git', 'merge-base', branch, 'HEAD'])
    logging.info("Base commit: {0}".format(commit))

    if wait_for_ssh:
        while not remote_communicate(
                host=host,
                remote_command='true',
                error_ok=True,
                extra_ssh_args=extra_ssh_args):
            logging.info("Remote host is unavailabe, re-trying")
            time.sleep(1)

    can_clone_and_retry = True
    remote_commit = None

    # This will retry on exception at most once, after trying to clone the repository.
    while True:
        try:
            remote_commit = fetch_remote_commit(
                host=host,
                remote_path=remote_path,
                extra_ssh_args=extra_ssh_args)
            break
        except subprocess.CalledProcessError as called_process_error:
            if not can_clone_and_retry:
                raise called_process_error
            logging.warning(
                "Failed to determine head commit in the remote repository. It is posisble that "
                "the remote repository directory does not exist. Trying to clone it.")

        can_clone_and_retry = False
        logging.info(
            "Trying to clone the remote repository on host %s at path %s",
            host, remote_path)
        remote_communicate(
            host=host,
            remote_command=(
                """
                    repo_dir=%s;
                    if [[ ${repo_dir} =~ ^[~]/ ]]; then
                      repo_dir=${HOME}/${repo_dir:2}
                    fi
                    echo "Attempting to clone the code on $(hostname) at ${repo_dir}"
                    if [[ ! -e "${repo_dir}" ]]; then
                        ( set -x
                          git clone https://github.com/yugabyte/yugabyte-db.git "$repo_dir" )
                    fi
                """ % shlex.quote(remote_path)
            ).strip(),
            extra_ssh_args=extra_ssh_args)

    if remote_path.startswith('~/'):
        escaped_remote_path = '$HOME/' + shlex.quote(remote_path[2:])
    else:
        escaped_remote_path = shlex.quote(remote_path)

    if remote_commit != commit:
        logging.info("Remote commit mismatch, syncing")
        remote_command = ' && '.join([
            'cd {0}'.format(escaped_remote_path),
            'git checkout -- .',
            'git clean -f .',
            'git fetch {0}'.format(upstream),
            'git checkout {0}'.format(commit)
        ])
        remote_communicate(host=host, remote_command=remote_command, extra_ssh_args=extra_ssh_args)
        remote_commit = fetch_remote_commit(
            host=host, remote_path=remote_path, extra_ssh_args=extra_ssh_args)
        if remote_commit != commit:
            raise RuntimeError("Failed to sync remote commit to: {0}, it is still: {1}".format(
                commit, remote_commit))

    files, del_files = parse_git_diff_name_status(
        check_output_lines(['git', 'diff', commit, '--name-status']))
    logging.info("Total files: {0}, deleted files: {1}".format(len(files), len(del_files)))

    if files:
        # From this StackOverflow thread: https://goo.gl/xzhBUC
        # The -a option is equivalent to -rlptgoD. You need to remove the -t. -t tells rsync to
        # transfer modification times along with the files and update them on the remote system.
        #
        # Another relevant one -- how to make rsync preserve timestamps of unchanged files:
        # https://goo.gl/czD96F
        #
        # We are using "rlpcgoD" instead of "rlptgoD" (with "t" replaced with "c").
        # The goal is to use checksums for deciding what files to skip.
        rsync_args = ['rsync', '-rlpcgoDvR']
        rsync_args += files
        rsync_args += ['--exclude', '.git']
        if extra_ssh_args:
            # TODO: what if some of the extra SSH arguments contain spaces?
            rsync_args += ['-e', ' '.join(['ssh'] + extra_ssh_args)]
        rsync_args += ["{0}:{1}".format(host, remote_path)]
        logging.info("Running rsync command: %s", shlex_join(rsync_args))
        proc = subprocess.Popen(rsync_args, shell=False)
        proc.communicate()
        if proc.returncode != 0:
            raise RuntimeError('rsync terminated with code {}'.format(proc.returncode))

    if del_files:
        remote_command = 'cd {0} && rm -f '.format(escaped_remote_path)
        for file in del_files:
            remote_command += shlex.quote(file)
            remote_command += ' '
        remote_communicate(host=host, remote_command=remote_command, extra_ssh_args=extra_ssh_args)

    check_remote_files(
        escaped_remote_path=escaped_remote_path, host=host, remote_path=remote_path, files=files,
        extra_ssh_args=extra_ssh_args)

    return escaped_remote_path


def sync_changes_with_args(args: argparse.Namespace) -> str:
    extra_ssh_args = process_extra_ssh_args(args.extra_ssh_args)
    escaped_remote_path = sync_changes(
        host=args.host,
        branch=args.branch,
        remote_path=args.remote_path,
        wait_for_ssh=args.wait_for_ssh,
        upstream=args.upstream,
        extra_ssh_args=extra_ssh_args)
    return escaped_remote_path


def exec_command(
        host: str,
        escaped_remote_path: str,
        script_name: str,
        script_args: List[str],
        should_quote_args: bool,
        extra_ssh_args: List[str],
        **kwargs: bool) -> None:
    remote_command = "cd {0} && ./{1}".format(escaped_remote_path, script_name)
    for arg in script_args:
        remote_command += " {0}".format(shlex.quote(arg) if should_quote_args else arg)
    logging.info("Remote command: {0}".format(remote_command))
    # Let's not use subprocess if the output is potentially large:
    # https://thraxil.org/users/anders/posts/2008/03/13/Subprocess-Hanging-PIPE-is-your-enemy/
    ssh_path: str = subprocess.check_output(['which', 'ssh']).decode('utf-8').strip()
    ssh_args: List[str] = get_ssh_cmd_line(
        host=host,
        extra_ssh_args=extra_ssh_args,
        remote_command=[remote_command]
    )
    assert ssh_args[0] == 'ssh'
    logging.info("Full command line for SSH exec: %s", shlex_join([ssh_path] + ssh_args[1:]))
    if kwargs.get('patch_path', False):
        proc = subprocess.Popen(
            [ssh_path] + ssh_args[1:], stderr=subprocess.STDOUT, stdout=subprocess.PIPE)
        assert proc.stdout is not None
        prefix = '../../src/yb'
        new_prefix = os.path.join(os.getcwd(), 'src/yb')
        for binary_line in proc.stdout:
            line = binary_line.decode('utf-8')
            if line.startswith(prefix):
                print(new_prefix + line[len(prefix):], end='')
            else:
                print(line, end='')
    else:
        os.execv(ssh_path, ssh_args)


def get_default_remote_path() -> str:
    """
    Get the default remote path to use. If the current directory is a subdirectory of the home
    directory or any of its aliases, returns the current directory with the home directory path
    replaced with a tilde. Otherwise simply returns the current directory.
    """
    home = os.path.expanduser('~')
    cwd = os.getcwd()
    for home_prefix in [home, os.path.realpath(home), os.path.abspath(home)]:
        if cwd.startswith(home_prefix + '/'):
            return '~/{0}'.format(cwd[len(home_prefix) + 1:])
    return cwd


def add_common_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument('--host', type=str, default=None,
                        help=('Host to build on. Can also be specified using the {} environment ' +
                              'variable.').format(REMOTE_BUILD_HOST_ENV_VAR))

    # Note: don't specify default arguments here, because they may come from the "profile".
    parser.add_argument('--remote-path', type=str, default=None,
                        help='path used for build')
    parser.add_argument('--branch', type=str, default=None,
                        help='Base branch for build. If not specified, we will attempt to '
                             'auto-detect the branch from the local branch name based on the '
                             'x.y.z_... pattern')
    parser.add_argument('--upstream', type=str, default=None,
                        help='base upstream for remote host to fetch')
    parser.add_argument('--build-type', type=str, default=None,
                        help='build type, defaults to release')
    parser.add_argument('--skip-build', action='store_true',
                        help='skip build, only sync files')
    parser.add_argument('--wait-for-ssh', action='store_true',
                        help='Wait for the remote server to be ssh-able')
    parser.add_argument('--profile',
                        help='Use a "profile" specified in the {} file'.format(CONFIG_FILE_PATH))
    parser.add_argument('--extra-ssh-args',
                        help="Arguments (separated by whitespace) to add to the SSH command line")
    parser.add_argument('--verbose',
                        action='store_true',
                        help='Verbose output')


def apply_default_arg_values(args: argparse.Namespace) -> None:
    args.host = apply_default_host_value(args.host)

    if args.branch is None:
        local_branch_name = subprocess.check_output(
            shlex.split('git rev-parse --abbrev-ref HEAD')).decode('utf-8')
        branch_name_match = LOCAL_BRANCH_PREFIX_RE.match(local_branch_name)
        if branch_name_match:
            local_branch_name_prefix = branch_name_match.group(1)
            candidate_branch = 'origin/' + local_branch_name_prefix
            if subprocess.call(['git', 'show-ref', candidate_branch]) == 0:
                logging.info("Auto-detected upstream branch %s from local branch name %s",
                             candidate_branch, local_branch_name)
                args.branch = candidate_branch
            else:
                logging.warning(
                    "Could not use the local branch name (%s) prefix %s as the upstream branch "
                    "name, the corresponding upstream branch does not exist" % (
                        local_branch_name, local_branch_name_prefix))
        if args.branch is None:
            args.branch = 'origin/master'

    if args.remote_path is None:
        args.remote_path = get_default_remote_path()

    if args.upstream is None:
        args.upstream = DEFAULT_UPSTREAM


def handle_yb_build_cmd_line() -> None:
    if len(sys.argv) >= 2 and sys.argv[1] in ['ybd', 'yb_build.sh', './yb_build.sh']:
        # Allow the first argument to be yb_build.sh or its equivalent, so we can copy and paste a
        # non-remote build command line directly and give it to remote_build.py.
        sys.argv[1:2] = ['--']


def process_extra_ssh_args(extra_ssh_args_value: Optional[str]) -> List[str]:
    return (extra_ssh_args_value or '').strip().split()


def log_args(args: argparse.Namespace) -> None:
    logging.info(
        "Host: {0}, build type: {1}, remote path: {2}".format(
            args.host,
            args.build_type or 'N/A',
            args.remote_path))
    logging.info("Arguments to remote build: {}".format(args.build_args))
