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

from ybops.common.exceptions import YBOpsRuntimeError
import ybops.utils as ybutils


class AnsibleProcess(object):
    """Generic class for handling external calls to our Ansible playbooks.

    Callers can directly update self.playbook_args before calling the run() method.
    """
    DEFAULT_SSH_USER = "centos"
    DEFAULT_SSH_CONNECTION_TYPE = "ssh"

    def __init__(self):
        self.yb_user_name = "yugabyte"

        self.playbook_args = {
            "user_name": self.yb_user_name,
            "instance_search_pattern": "all"
        }

        self.can_ssh = True
        self.connection_type = self.DEFAULT_SSH_CONNECTION_TYPE
        self.connection_target = "localhost"

    def set_connection_params(self, conn_type, target):
        self.connection_type = conn_type
        self.connection_target = self.build_connection_target(target)

    def build_connection_target(self, target):
        return target + ","

    def run(self, filename, extra_vars=dict(), host_info={}, print_output=True):
        """Method used to call out to the respective Ansible playbooks.
        Args:
            filename: The playbook file to execute
            extra_args: A dictionary of KVs to pass as extra-vars to ansible-playbook
            host_info: A dictionary of host level attributes which is empty for localhost.
        """

        playbook_args = self.playbook_args
        tags = extra_vars.get("tags")
        skip_tags = extra_vars.get("skip_tags")
        # Use the ssh_user provided in extra vars as the ssh user to override.
        ssh_user = extra_vars.get("ssh_user", host_info.get("ssh_user", self.DEFAULT_SSH_USER))
        ssh_port = extra_vars.get("ssh_port")
        ssh_host = extra_vars.get("ssh_host")
        vault_password_file = extra_vars.get("vault_password_file")
        ask_sudo_pass = extra_vars.get("ask_sudo_pass")
        sudo_pass_file = extra_vars.get("sudo_pass_file")
        ssh_key_file = extra_vars.get("private_key_file")

        playbook_args.update(extra_vars)

        if self.can_ssh:
            playbook_args.update({
                "ssh_user": ssh_user,
                "yb_server_ssh_user": ssh_user
            })

        playbook_args["yb_home_dir"] = ybutils.YB_HOME_DIR

        process_args = [
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

        if ssh_port is None or ssh_host is None:
            connection_type = "local"
            inventory_target = "localhost,"
        elif self.can_ssh:
            process_args.extend([
                "--private-key", ssh_key_file,
                "--user", ssh_user
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
            "-c", connection_type,
            "-e", "ansible_python_interpreter='/usr/bin/env python'"
        ])

        # Setup the full list of extra-vars needed for ansible plays.
        process_args.extend(["--extra-vars", json.dumps(playbook_args)])
        env = os.environ.copy()
        if env.get('APPLICATION_CONSOLE_LOG_LEVEL') != 'INFO':
            env['PROFILE_TASKS_TASK_OUTPUT_LIMIT'] = '30'
        logging.info("[app] Running ansible playbook {} against target {}".format(
                        filename, inventory_target))
        logging.info("Running ansible command {}".format(json.dumps(process_args,
                                                                    separators=(' ', ' '))))
        p = subprocess.Popen(process_args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
        stdout, stderr = p.communicate()
        if print_output:
            print(stdout)
        EXCEPTION_MSG_FORMAT = ("Playbook run of {} against {} with args {} " +
                                "failed with return code {} and error '{}'")
        if p.returncode != 0:
            raise YBOpsRuntimeError(EXCEPTION_MSG_FORMAT.format(
                    filename, inventory_target, process_args, p.returncode, stderr))
        return p.returncode
