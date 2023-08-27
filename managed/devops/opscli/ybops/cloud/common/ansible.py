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
import sys

from ybops.common.exceptions import YBOpsRuntimeError, YBOpsRecoverableError
import ybops.utils as ybutils
from ybops.utils.ssh import SSH, SSH2, parse_private_key, check_ssh2_bin_present
from ybops.utils.remote_shell import copy_to_tmp, RemoteShell


class AnsibleProcess(object):
    """Generic class for handling external calls to our Ansible playbooks.

    Callers can directly update self.playbook_args before calling the run() method.
    """
    DEFAULT_SSH_USER = "centos"
    DEFAULT_SSH_CONNECTION_TYPE = "ssh"
    REDACT_STRING = "REDACTED"

    def __init__(self):
        self.yb_user_name = "yugabyte"

        self.playbook_args = {
            "user_name": self.yb_user_name,
            "instance_search_pattern": "all"
        }

        self.can_ssh = True
        self.connection_type = self.DEFAULT_SSH_CONNECTION_TYPE
        self.connection_target = "localhost"
        self.sensitive_data_keywords = ["KEY", "SECRET", "CREDENTIALS", "API", "POLICY",
                                        "NODE_AGENT_AUTH_TOKEN"]

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
        if "PYTHON_EXECUTABLE" in os.environ:
            return os.environ["PYTHON_EXECUTABLE"]
        raise YBOpsRuntimeError("Could not find python path in environment.")

    def get_pex_path(self):
        """
        Method used to determine the pex path if script is being called with a PEX environment.
        Returns path to env if available, False otherwise
        """
        if "PEX" in os.environ:
            return os.environ.get("PEX")
        return False

    # Finds ansible playbook path. Only necessary in PEX environments.
    def get_ansible_playbook_path(self):
        return [x for x in sys.path if x.find('ansible-') >= 0][0]

    def run(self, filename, extra_vars=None, host_info=None, print_output=True,
            disable_offloading=False):
        """Method used to call out to the respective Ansible playbooks.
        Args:
            filename: The playbook file to execute
            extra_vars: A dictionary of KVs to pass as extra-vars to ansible-playbook
            host_info: A dictionary of host level attributes which is empty for localhost.
            disable_offloading: A flag to disable ansible offloading.
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
        vars_file = vars.pop("vars_file", None)
        ask_sudo_pass = vars.pop("ask_sudo_pass", None)
        sudo_pass_file = vars.pop("sudo_pass_file", None)
        ssh_key_file = vars.pop("private_key_file", None)
        ssh2_enabled = vars.pop("ssh2_enabled", False) and check_ssh2_bin_present()
        local_package_path = vars.pop("local_package_path", None)
        connection_type = vars.pop("connection_type", None)
        node_agent_home = vars.pop("node_agent_home", None)
        node_agent_ip = vars.pop("node_agent_ip", None)
        node_agent_port = vars.pop("node_agent_port", None)
        node_agent_cert_path = vars.pop("node_agent_cert_path", None)
        node_agent_auth_token = vars.pop("node_agent_auth_token", None)
        offload = vars.pop("offload_ansible", False) and not disable_offloading
        ssh_key_type = parse_private_key(ssh_key_file)
        remote_tmp_dir = vars.get("remote_tmp_dir", "/tmp")
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

        process_args = ["ansible-playbook"]

        # Determine PEX and override ansible-playbook path.
        pex_path = self.get_pex_path()
        if pex_path:
            python = self.get_python_executable()
            ansible_playbook = self.get_ansible_playbook_path()
            process_args = [python, pex_path, ansible_playbook + "/.prefix/bin/ansible-playbook"]

        if ssh2_enabled:
            # Will be moved as part of task of license upload api.
            configure_ssh2_args = process_args + [
                os.path.join(ybutils.YB_DEVOPS_HOME, "configure_ssh2.yml")
            ]
            p = subprocess.Popen(configure_ssh2_args,
                                 stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE,
                                 env=env)
            stdout, stderr = p.communicate()
            if p.returncode != 0:
                raise YBOpsRuntimeError("Failed to configure ssh2 on the platform")

        process_args.extend([os.path.join(ybutils.YB_DEVOPS_HOME, filename)])
        playbook_args["yb_home_dir"] = ybutils.YB_HOME_DIR

        if connection_type is not None and connection_type == 'node_agent_rpc':
            playbook_args.update({
                # Below args are used in the playbooks.
                # E.g ssh_user as home_dir.
                "ansible_port": node_agent_port,
                "yb_ansible_host": node_agent_ip,
                "ssh_user": ssh_user,
                "yb_server_ssh_user": ssh_user
            })
            if offload:
                if vars_file is not None:
                    copy_to_tmp(extra_vars, vars_file, remote_tmp_dir=remote_tmp_dir)
                    vars_file = os.path.join(remote_tmp_dir, os.path.basename(vars_file))
                if vault_password_file is not None:
                    copy_to_tmp(extra_vars, vault_password_file, remote_tmp_dir=remote_tmp_dir)
                    vault_password_file = os.path.join(
                        remote_tmp_dir, os.path.basename(vault_password_file))

                devops_path = os.path.join(node_agent_home, "pkg", "devops")
                thirdparty_path = os.path.join(node_agent_home, "pkg", "thirdparty")
                # Pass local ansible env vars to remote shell.
                process_args = []
                for env_var, value in os.environ.items():
                    if env_var.startswith("ANSIBLE") and env_var != "ANSIBLE_LOCAL_TEMP":
                        process_args.append("{}={}".format(env_var, value))

                process_args.extend([os.path.join(devops_path, "bin", "ansible-playbook.sh"),
                                     os.path.join(devops_path, os.path.basename(filename))])
                if local_package_path is not None:
                    local_package_path = thirdparty_path
                playbook_args.update({
                    "ansible_remote_tmp": "/tmp",
                    "yb_ansible_host": "localhost"
                })
                process_args.extend(["--limit", "localhost"])
                inventory_target = "localhost,"
                connection_type = "local"
            else:
                playbook_args.update({
                  "node_agent_user": ssh_user,
                  "node_agent_ip": node_agent_ip,
                  "node_agent_port": node_agent_port,
                  "node_agent_cert_path": node_agent_cert_path,
                  "node_agent_auth_token": node_agent_auth_token,
                })
                inventory_target = self.build_connection_target(node_agent_ip)
        elif ssh_port is None or ssh_host is None:
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

        if vars_file is not None:
            playbook_args["vars_file"] = vars_file
        if vault_password_file is not None:
            process_args.extend(["--vault-password-file", vault_password_file])
        if local_package_path is not None:
            playbook_args["local_package_path"] = local_package_path
        if ask_sudo_pass is not None:
            process_args.extend(["--ask-sudo-pass"])
        if sudo_pass_file is not None:
            playbook_args["yb_sudo_pass_file"] = sudo_pass_file

        if skip_tags is not None:
            process_args.extend(["--skip-tags", ','.join(skip_tags)])
        if tags is not None:
            process_args.extend(["--tags", ','.join(tags)])

        process_args.extend([
          "--user", ssh_user
        ])
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
        if offload:
            logging.info("Running ansible playbook {} on DB node"
                         .format(os.path.basename(filename)))
            remote_shell_args = {"async": True}
            remote_shell = RemoteShell(extra_vars)
            try:
                rc, stdout, stderr = remote_shell.exec_command(process_args, **remote_shell_args)
            finally:
                delete_files = []
                if vars_file is not None:
                    delete_files.extend(vars_file)
                if vault_password_file is not None:
                    delete_files.extend(vault_password_file)
                if delete_files:
                    remote_shell.exec_command("rm -rf ".format(' '.join(delete_files)))
        else:
            logging.info("Running ansible playbook {} on platform"
                         .format(os.path.basename(filename)))
            p = subprocess.Popen(process_args, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                 env=env)
            stdout, stderr = p.communicate()
            rc = p.returncode
        if print_output:
            print(stdout.decode('utf-8'))

        if rc != 0:
            errmsg = f"Playbook run of {filename} against {inventory_target} with args " \
                     f"{redacted_process_args} failed with return code {rc} " \
                     f"and error '{stderr.decode('utf-8')}'"

            if rc == 4:  # host unreachable
                raise YBOpsRecoverableError(errmsg)
            else:
                raise YBOpsRuntimeError(errmsg)

        return rc
