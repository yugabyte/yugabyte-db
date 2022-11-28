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

from ybops.common.exceptions import YBOpsRecoverableError
from ybops.utils.ssh import SSHClient
from ybops.node_agent.shell_client import RpcShellClient

CONNECTION_ATTEMPTS = 5
CONNECTION_ATTEMPT_DELAY_SEC = 3


class RemoteShellOutput(object):
    """
        RemoteShellOutput class converts the o/p in the standard format
        with o/p, err, exited status attached to it.
    """

    def __init__(self):
        self.stdout = ''
        self.stderr = ''
        self.exited = False


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
        self.connected = True

    def close(self):
        if self.connected:
            self.ssh_conn.close_connection()

    def run_command_raw(self, command, **kwargs):
        result = RemoteShellOutput()
        try:
            kwargs.setdefault('output_only', True)
            output = self.ssh_conn.exec_command(command, **kwargs)
            result.stdout = output
            result.exited = 0
        except Exception as e:
            result.stderr = str(e)
            result.exited = 1

        return result

    def run_command(self, command, **kwargs):
        result = self.run_command_raw(command, **kwargs)

        if result.exited:
            raise YBOpsRecoverableError(
                "Remote shell command '{}' failed with "
                "return code '{}' and error '{}'".format(command.encode('utf-8'),
                                                         result.stderr,
                                                         result.exited)
            )
        return result

    def put_file(self, local_path, remote_path, **kwargs):
        self.ssh_conn.upload_file_to_remote_server(local_path, remote_path, **kwargs)

    # Checks if the file exists on the remote, and if not, it puts it there.
    def put_file_if_not_exists(self, local_path, remote_path, file_name, **kwargs):
        result = self.run_command('ls ' + remote_path, **kwargs)
        if file_name not in result:
            self.put_file(local_path, os.path.join(remote_path, file_name), **kwargs)

    def fetch_file(self, remote_file_name, local_file_name, **kwargs):
        self.ssh_conn.download_file_from_remote_server(remote_file_name, local_file_name, **kwargs)


class RpcRemoteShell(object):
    """RpcRemoteShell class is used run remote shell commands against nodes using gRPC.
    """

    def __init__(self, options):
        client_options = {
            "ip": options.get("ip"),
            "port": options.get("port"),
            "cert_path": options.get("cert_path"),
            "auth_token": options.get("auth_token"),
        }
        self.client = RpcShellClient(client_options)
        self.client.connect()

    def close(self):
        self.client.close()

    def run_command_raw(self, command, **kwargs):
        result = RemoteShellOutput()
        try:
            output = self.client.exec_command(command, **kwargs)
            if output.stderr != '' or output.rc != 0:
                result.stderr = output.stderr
                result.exited = 1
            else:
                result.stdout = output.stdout
                result.exited = 0
        except Exception as e:
            result.stderr = str(e)
            result.exited = 1
        return result

    def run_command(self, command, **kwargs):
        result = self.run_command_raw(command, **kwargs)
        if result.exited:
            raise YBOpsRecoverableError(
                "Remote shell command '{}' failed with "
                "return code '{}' and error '{}'".format(' '.join(command).encode('utf-8'),
                                                         result.stderr,
                                                         result.exited)
            )
        return result

    def put_file(self, local_path, remote_path, **kwargs):
        self.client.put_file(local_path, remote_path, **kwargs)

    # Checks if the file exists on the remote, and if not, it puts it there.
    def put_file_if_not_exists(self, local_path, remote_path, file_name, **kwargs):
        result = self.run_command('ls ' + remote_path, kwargs)
        if file_name not in result:
            self.put_file(local_path, os.path.join(remote_path, file_name), **kwargs)

    def fetch_file(self, remote_file_name, local_file_name, **kwargs):
        self.client.fetch_file(remote_file_name, local_file_name, **kwargs)
