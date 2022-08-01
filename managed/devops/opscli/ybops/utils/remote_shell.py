#!/usr/bin/env python
#
# Copyright 2019 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

import os
import logging

from ybops.common.exceptions import YBOpsRuntimeError
from ybops.utils.ssh import SSHClient

CONNECTION_ATTEMPTS = 5
CONNECTION_ATTEMPT_DELAY_SEC = 3


class RemoteShellOutput(object):
    """
        RemoteShellOutput class converts the o/p in the standard format
        with o/p, err, exited status attached to it.
    """

    def __init__(self):
        self.stdout = None
        self.exited = False
        self.stderr = None


class RemoteShell(object):
    """RemoteShell class is used run remote shell commands against nodes using paramiko.
    """

    def __init__(self, options):
        assert options["ssh_user"] is not None, 'ssh_user is required option'
        assert options["ssh_host"] is not None, 'ssh_host is required option'
        assert options["ssh_port"] is not None, 'ssh_port is required option'
        assert options["private_key_file"] is not None, 'private_key_file is required option'

        self.ssh_conn = SSHClient(ssh2_enabled=options["ssh2_enabled"])
        self.ssh_conn.connect(
            options.get("ssh_host"),
            options.get("ssh_user"),
            options.get("private_key_file"),
            options.get("ssh_port")
        )

    def run_command_raw(self, command):
        result = RemoteShellOutput()
        try:
            output = self.ssh_conn.exec_command(command, output_only=True)
            result.stdout = output
            result.exited = 0
        except Exception as e:
            result.stderr = str(e)
            result.exited = 1

        return result

    def run_command(self, command):
        result = self.run_command_raw(command)

        if result.exited:
            raise YBOpsRuntimeError(
                "Remote shell command '{}' failed with "
                "return code '{}' and error '{}'".format(command.encode('utf-8'),
                                                         result.stderr,
                                                         result.exited)
            )
        return result

    def put_file(self, local_path, remote_path):
        self.ssh_conn.upload_file_to_remote_server(local_path, remote_path)

    # Checks if the file exists on the remote, and if not, it puts it there.
    def put_file_if_not_exists(self, local_path, remote_path, file_name):
        result = self.run_command('ls ' + remote_path)
        if file_name not in result:
            self.put_file(local_path, os.path.join(remote_path, file_name))
