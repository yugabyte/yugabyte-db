#!/usr/bin/env python
#
# Copyright 2019 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt


import json
import logging
import os
import subprocess
from sys import platform

from ybops.common.exceptions import YBOpsRuntimeError, YBOpsRecoverableError
import ybops.utils as ybutils
from ybops.utils.ssh import SSH, SSH2, parse_private_key, check_ssh2_bin_present


class AnsibleProcess(object):
    """Generic class for handling external calls to our Ansible playbooks.

    Callers can directly update self.playbook_args before calling the run() method.
    """
    DEFAULT_SSH_USER = "centos"
    DEFAULT_SSH_CONNECTION_TYPE = "ssh"
    REDACT_STRING = "REDACTED"
    PYTHON3_EXECUTABLES = ["python3", "python3.6", "python3.7", "python3.8", "python3.9"]

    def __init__(self):
        self.yb_user_name = "yugabyte"

        self.playbook_args = {
            "user_name": self.yb_user_name,
            "instance_search_pattern": "all"
        }

        self.can_ssh = True
        self.connection_type = self.DEFAULT_SSH_CONNECTION_TYPE
        self.connection_target = "localhost"
        self.sensitive_data_keywords = ["KEY", "SECRET", "CREDENTIALS", "API", "POLICY"]

    def set_connection_params(self, conn_type, target):
        self.connection_type = conn_type
        self.connection_target = self.build_connection_target(target)

    def build_connection_target(self, target):
        return target + ","

    def is_sensitive(self, key_string):
        for word in self.sensitive_data_keywords:
            if word in key_string:
                return True
        return False

    def redact_sensitive_data(self, playbook_args):
        for key, value in playbook_args.items():
            if self.is_sensitive(key.upper()):
                playbook_args[key] = self.REDACT_STRING
        return playbook_args

    def get_python_executable(self):

        for py_executable in self.PYTHON3_EXECUTABLES:
            bashCommand = "which \"" + py_executable + "\" > /dev/null 2>&1"
            process = subprocess.Popen(bashCommand.split(), stdout=subprocess.PIPE)
            output, error = process.communicate()
            if not output:
                PYTHON_EXECUTABLE = py_executable
                os.environ["PYTHON_EXECUTABLE"] = PYTHON_EXECUTABLE
                return PYTHON_EXECUTABLE

        bashCommand = "which python > /dev/null 2>&1"
        process = subprocess.Popen(bashCommand.split(), stdout=subprocess.PIPE)
        output, error = process.communicate()
        if not output:
            bashCommand = "python -c 'import sys; sys.exit(1) if sys.version_info[0] != 2 else sys.exit(0)'"
            process = subprocess.Popen(bashCommand.split(), stdout=subprocess.PIPE)
            output, error = process.communicate()
            if not output:
                if os.environ.get("YB_MANAGED_DEVOPS_USE_PYTHON3") == "0":
                    PYTHON_EXECUTABLE = "python"
                    os.environ["PYTHON_EXECUTABLE"] = PYTHON_EXECUTABLE
                    return PYTHON_EXECUTABLE
                elif os.environ.get("YB_MANAGED_DEVOPS_USE_PYTHON3") == "1":
                    PYTHON_EXECUTABLE = "python"
                    os.environ["PYTHON_EXECUTABLE"] = PYTHON_EXECUTABLE
                    return PYTHON_EXECUTABLE

        logging.info("Failed to find Python executable.")
        return None

    def get_pex_path(self):
        PEX_PATH = os.path.join(ybutils.YB_DEVOPS_HOME, "pex", "pexEnv")
        os.environ["PEX_PATH"] = PEX_PATH
        return PEX_PATH

    def run(self, filename, extra_vars=dict(), host_info={}, print_output=True):
        """Method used to call out to the respective Ansible playbooks.
        Args:
            filename: The playbook file to execute
            extra_args: A dictionary of KVs to pass as extra-vars to ansible-playbook
            host_info: A dictionary of host level attributes which is empty for localhost.
        """

        if host_info is None:
            host_info = {}
        if extra_vars is None:
            extra_vars = dict()

        playbook_args = self.playbook_args
        vars = extra_vars.copy()
        tags = vars.pop("tags", None)
        skip_tags = vars.pop("skip_tags", None)
        # Use the ssh_user provided in extra vars as the ssh user to override.
        ssh_user = vars.pop("ssh_user", host_info.get("ssh_user", self.DEFAULT_SSH_USER))
        ssh_port = vars.pop("ssh_port", None)
        ssh_host = vars.pop("ssh_host", None)
        vault_password_file = vars.pop("vault_password_file", None)
        ask_sudo_pass = vars.pop("ask_sudo_pass", None)
        sudo_pass_file = vars.pop("sudo_pass_file", None)
        ssh_key_file = vars.pop("private_key_file", None)
        ssh2_enabled = vars.pop("ssh2_enabled", False) and check_ssh2_bin_present()
        ssh_key_type = parse_private_key(ssh_key_file)
        env = os.environ.copy()
        if env.get('APPLICATION_CONSOLE_LOG_LEVEL') != 'INFO':
            env['PROFILE_TASKS_TASK_OUTPUT_LIMIT'] = '30'

        playbook_args.update(vars)

        if self.can_ssh:
            playbook_args.update({
                "ssh_user": ssh_user,
                "yb_server_ssh_user": ssh_user,
                "ssh_version": SSH if ssh_key_type == SSH else SSH2
            })

        if ssh2_enabled:
            # Will be moved as part of task of license upload api.
            configure_ssh2_args = []

            # The virtual environment is used if the operating system is macOS, or if the
            # environment variable YB_USE_VIRTUAL_ENV is set (default unset).
            if platform == "darwin" or "YB_USE_VIRTUAL_ENV" in os.environ:
                configure_ssh2_args = [
                    "ansible-playbook",
                    os.path.join(ybutils.YB_DEVOPS_HOME, "configure_ssh2.yml")
                ]
            # The PEX environment is used in all other cases, with defaults specified.
            else:
                PYTHON_EXECUTABLE = self.get_python_executable()
                if PYTHON_EXECUTABLE is None:
                    PYTHON_EXECUTABLE = "python3"
                PEX_PATH = self.get_pex_path()
                if PEX_PATH is None:
                    PEX_PATH = os.path.join(ybutils.YB_DEVOPS_HOME, "pex", "pexEnv")
                configure_ssh2_args = [
                    PYTHON_EXECUTABLE, PEX_PATH,
                    "ansible-playbook", os.path.join(ybutils.YB_DEVOPS_HOME, "configure_ssh2.yml")
                ]
            p = subprocess.Popen(configure_ssh2_args,
                                 stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE,
                                 env=env)
            stdout, stderr = p.communicate()
            if p.returncode != 0:
                raise YBOpsRuntimeError("Failed to configure ssh2 on the platform")

        playbook_args["yb_home_dir"] = ybutils.YB_HOME_DIR

        process_args = []

        # The virtual environment is used if the operating system is macOS, or if the
        # environment variable YB_USE_VIRTUAL_ENV is set (default unset).
        if platform == "darwin" or "YB_USE_VIRTUAL_ENV" in os.environ:
            process_args = [
                "ansible-playbook", os.path.join(ybutils.YB_DEVOPS_HOME, filename)
            ]
        # The PEX environment is used in all other cases for YBA.
        else:
            PYTHON_EXECUTABLE = self.get_python_executable()
            if PYTHON_EXECUTABLE is None:
                PYTHON_EXECUTABLE = "python3"
            PEX_PATH = self.get_pex_path()
            if PEX_PATH is None:
                PEX_PATH = os.path.join(ybutils.YB_DEVOPS_HOME, "pex", "pexEnv")
            process_args = [
                PYTHON_EXECUTABLE, PEX_PATH,
                "ansible-playbook", os.path.join(ybutils.YB_DEVOPS_HOME, filename)
            ]
        if vault_password_file is not None:
            process_args.extend(["--vault-password-file", vault_password_file])
        if ask_sudo_pass is not None:
            process_args.extend(["--ask-sudo-pass"])
        if sudo_pass_file is not None:
            playbook_args["yb_sudo_pass_file"] = sudo_pass_file

        if skip_tags is not None:
            process_args.extend(["--skip-tags", skip_tags])
        elif tags is not None:
            process_args.extend(["--tags", tags])

        process_args.extend([
            "--user", ssh_user
        ])

        if ssh_port is None or ssh_host is None:
            connection_type = "local"
            inventory_target = "localhost,"
        elif self.can_ssh:
            if ssh2_enabled:
                process_args.extend([
                    '--ssh-common-args=\'-K%s\'' % (ssh_key_file),
                    '--ssh-extra-args=\'-l%s\'' % (ssh_user),
                ])
            else:
                process_args.extend([
                    "--private-key", ssh_key_file,
                ])

            playbook_args.update({
                "yb_ansible_host": ssh_host,
                "ansible_port": ssh_port
            })

            inventory_target = self.build_connection_target(ssh_host)
            connection_type = self.DEFAULT_SSH_CONNECTION_TYPE
        else:
            connection_type = self.connection_type
            inventory_target = self.build_connection_target(
                host_info.get("name", self.connection_target))

        # Set inventory, connection type, and pythonpath.
        process_args.extend([
            "-i", inventory_target,
            "-c", connection_type
        ])

        redacted_process_args = process_args.copy()

        # Setup the full list of extra-vars needed for ansible plays.
        process_args.extend(["--extra-vars", json.dumps(playbook_args)])
        redacted_process_args.extend(
            ["--extra-vars", json.dumps(self.redact_sensitive_data(playbook_args))])
        logging.info("[app] Running ansible playbook {} against target {}".format(
                        filename, inventory_target))

        logging.info("Running ansible command {}".format(json.dumps(redacted_process_args,
                                                                    separators=(' ', ' '))))
        p = subprocess.Popen(process_args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
        stdout, stderr = p.communicate()

        if print_output:
            print(stdout.decode('utf-8'))

        if p.returncode != 0:
            errmsg = f"Playbook run of {filename} against {inventory_target} with args " \
                     f"{redacted_process_args} failed with return code {p.returncode} " \
                     f"and error '{stderr}'"

            if p.returncode == 4:  # host unreachable
                raise YBOpsRecoverableError(errmsg)
            else:
                raise YBOpsRuntimeError(errmsg)

        return p.returncode
