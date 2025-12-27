#!/usr/bin/env python
#
# Copyright 2019 YugabyteDB, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

import json
import logging
import os
import time

from ybops.common.exceptions import YBOpsRecoverableError, YBOpsRuntimeError
from ybops.utils.ssh import SSHClient
from ybops.node_agent.rpc import RpcClient
from ybops.utils.kubectl_shell import KubectlClient

CONNECTION_ATTEMPTS = 5
CONNECTION_ATTEMPT_DELAY_SEC = 3
CONNECT_RETRY_LIMIT = 60
# Retry in seconds
CONNECT_RETRY_DELAY = 10
CONNECT_TIMEOUT_SEC = 10


# Similar method exists for SSH.
def wait_for_server(connect_options, num_retries=CONNECT_RETRY_LIMIT, **kwargs):
    """This method waits for the connection to the remote host to become available.
    """

    retry_count = 0
    while retry_count < num_retries:
        logging.info("[app] Attempting connection to the remote host, retry count: {}"
                     .format(retry_count))
        if can_connect(connect_options, **kwargs):
            return True
        time.sleep(1)
        retry_count += 1

    return False


# Similar method exists for SSH.
def can_connect(connect_options):
    """This method checks if connection to remote host is available.
    """

    try:
        client = RemoteShell(connect_options)
        # The param timeout is used by node-agent.
        lines = client.check_exec_command("echo 'test'", timeout=CONNECT_TIMEOUT_SEC).splitlines()
        if len(lines) == 1 and (lines[0] == "test"):
            return True
        return False
    except Exception as e:
        logging.error("Error connecting, {}".format(e))
        return False


# Similar method exists for SSH.
def copy_to_tmp(connect_options, filepath, retries=3, retry_delay=CONNECT_RETRY_DELAY, **kwargs):
    """This method copies the given file to the specified tmp directory on remote host
    and return the output.
    """

    remote_tmp_dir = kwargs.get("remote_tmp_dir", "/tmp")
    dest_path = os.path.join(remote_tmp_dir, os.path.basename(filepath))
    chmod = kwargs.get('chmod', 0)
    if chmod == 0:
        chmod = os.stat(filepath).st_mode
        kwargs.setdefault('chmod', chmod)

    rc = 1
    while retries > 0:
        try:
            logging.info("[app] Copying local '{}' to remote '{}'".format(
                filepath, dest_path))
            client = RemoteShell(connect_options)
            try:
                client.put_file(filepath, dest_path, **kwargs)
                rc = 0
                break
            finally:
                client.close()
        except Exception as e:
            logging.error("Error copying file {} to {} - {}".format(filepath, dest_path, e))
            retries -= 1
            if (retries > 0):
                time.sleep(retry_delay)

    return rc


def get_connection_type(connect_options):
    """Returns the connection type.
    """
    connection_type = connect_options.get('connection_type')
    if connection_type is None:
        return 'ssh'
    return connection_type


def get_host_port_user(connect_options):
    """Returns the host, port and user for the connection type.
    """
    connection_type = get_connection_type(connect_options)
    connect_options['connection_type'] = connection_type
    if connection_type == 'ssh':
        connect_options['host'] = connect_options['ssh_host']
        connect_options['port'] = connect_options['ssh_port']
        connect_options['user'] = connect_options['ssh_user']
    elif connection_type == 'node_agent_rpc':
        connect_options['host'] = connect_options['node_agent_ip']
        connect_options['port'] = connect_options['node_agent_port']
        connect_options['user'] = connect_options['node_agent_user']
    elif connection_type == 'kubectl':
        connect_options['host'] = connect_options['kubectl_pod']
        connect_options['port'] = None  # Not applicable for kubectl
        connect_options['user'] = connect_options.get('kubectl_user', 'yugabyte')
    else:
        raise YBOpsRuntimeError("Unknown connection_type '{}'".format(connection_type))
    return connect_options


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
    """RemoteShell class is used run remote shell commands against nodes using
    the connection type. The connect_options are:
    For SSH:
      connection_type - None or set it to ssh to enable SSH.
      ssh_user - SSH user.
      ssh_host - SSH host IP.
      ssh_port - SSH port.
      private_key_file - Path to SSH private key file.
      ssh2_enabled - Optional SSH2 enabled flag.
    For RPC:
      connection_type - set to either rpc or node_agent_rpc to enable RPC.
      node_agent_user - Remote user.
      node_agent_ip - Node agent IP.
      node_agent_port - Node agent port.
      node_agent_cert_path - Path to node agent cert.
      node_agent_auth_token - JWT to authenticate the client.
    For Kubectl:
      connection_type - set to kubectl to enable Kubernetes pod exec.
      kubectl_pod - Pod name.
      kubectl_namespace - Kubernetes namespace (default: "default").
      kubectl_kubeconfig - Path to kubeconfig file (optional).
      kubectl_container - Container name within pod (optional).

    """

    def __init__(self, connect_options):
        connection_type = get_connection_type(connect_options)
        if connection_type == 'ssh':
            self.delegate = _SshRemoteShell(connect_options)
        elif connection_type == 'node_agent_rpc':
            self.delegate = _RpcRemoteShell(connect_options)
        elif connection_type == 'kubectl':
            self.delegate = _KubectlRemoteShell(connect_options)
        else:
            raise YBOpsRuntimeError("Unknown connection_type '{}'".format(connection_type))

    def close(self):
        self.delegate.close()

    def get_host_port_user(self):
        return self.delegate.get_host_port_user()

    def run_command_raw(self, command, **kwargs):
        '''
            Executes the command on the remote machine and returns RemoteShellOutput object
            without raising exception.
        '''
        return self.delegate.run_command_raw(command, **kwargs)

    def check_exec_command(self, command, **kwargs):
        '''
            Executes the command on the remote machine and raises exception if it fails.
            It returns the stdout of the command on success.
        '''
        return self.delegate.check_exec_command(command, **kwargs)

    def exec_command(self, command, **kwargs):
        '''
            Executes the command on the remote machine and returns rc, stdout, stderr without
            raising exception.
        '''
        return self.delegate.exec_command(command, **kwargs)

    def check_exec_script(self, local_script_name, params):
        '''
            Executes the script on the remote machine and raises exception if it fails.
            It returns the stdout of the command on success.
        '''
        return self.delegate.check_exec_script(local_script_name, params)

    def put_file(self, local_path, remote_path, **kwargs):
        self.delegate.put_file(local_path, remote_path, **kwargs)

    def put_file_if_not_exists(self, local_path, remote_path, file_name, **kwargs):
        self.delegate.put_file_if_not_exists(local_path, remote_path, file_name, **kwargs)

    def fetch_file(self, remote_file_name, local_file_name, **kwargs):
        self.delegate.fetch_file(remote_file_name, local_file_name, **kwargs)

    def invoke_method(self, param, **kwargs):
        return self.delegate.invoke_method(param, **kwargs)


class _SshRemoteShell(object):
    """_SshRemoteShell class is used run remote shell commands against nodes using paramiko.
    """

    def __init__(self, connect_options):
        assert connect_options["ssh_user"] is not None, 'ssh_user is required'
        assert connect_options["ssh_host"] is not None, 'ssh_host is required'
        assert connect_options["ssh_port"] is not None, 'ssh_port is required'
        assert connect_options["private_key_file"] is not None, 'private_key_file is required'

        self.ssh_conn = SSHClient(ssh2_enabled=connect_options["ssh2_enabled"])
        self.ssh_conn.connect(
            connect_options.get("ssh_host"),
            connect_options.get("ssh_user"),
            connect_options.get("private_key_file"),
            connect_options.get("ssh_port")
        )
        self.connected = True

    def get_host_port_user(self):
        return get_host_port_user(self.connect_options)

    def close(self):
        if self.connected:
            self.ssh_conn.close_connection()

    def run_command_raw(self, command, **kwargs):
        result = RemoteShellOutput()
        try:
            rc, stdout, stderr = self.ssh_conn.exec_command(command, **kwargs)
            result.stdout = stdout
            result.exited = rc
            result.stderr = stderr
        except Exception as e:
            result.stderr = str(e)
            result.exited = 1

        return result

    def check_exec_command(self, command, **kwargs):
        skip_cmd_logging = kwargs.get('skip_cmd_logging', False)
        result = self.run_command_raw(command, **kwargs)
        if result.exited:
            if result.stdout:
                # Log the stdout for debugging purposes.
                logging.error(result.stdout)
            cmd = ' '.join(command).encode('utf-8') if isinstance(command, list) else command
            raise YBOpsRuntimeError('Remote command \'{}\' failed with error code {}: {}'.format(
                "" if skip_cmd_logging else cmd, result.exited, result.stderr))
        return result.stdout

    def exec_command(self, command, **kwargs):
        # This returns rc, stdout, stderr.
        return self.ssh_conn.exec_command(command, **kwargs)

    def check_exec_script(self, local_script_name, params):
        rc, stdout, stderr = self.ssh_conn.exec_script(local_script_name, params)
        if rc != 0:
            raise YBOpsRuntimeError('Remote command failed with error code {}: {}'.format(
                rc, stderr))
        return stdout

    def put_file(self, local_path, remote_path, **kwargs):
        self.ssh_conn.upload_file_to_remote_server(local_path, remote_path, **kwargs)

    # Checks if the file exists on the remote, and if not, it puts it there.
    def put_file_if_not_exists(self, local_path, remote_path, file_name, **kwargs):
        # Command fails if the file does not exist.
        rc, stdout, _ = self.exec_command('ls ' + remote_path, **kwargs)
        if rc != 0 or file_name not in stdout:
            self.put_file(local_path, os.path.join(remote_path, file_name), **kwargs)

    def fetch_file(self, remote_file_name, local_file_name, **kwargs):
        self.ssh_conn.download_file_from_remote_server(remote_file_name, local_file_name, **kwargs)

    def invoke_method(self, input, **kwargs):
        raise NotImplementedError("SSH does not support method invocation")


class _RpcRemoteShell(object):
    """_RpcRemoteShell class is used run remote shell commands against nodes using gRPC.
    """

    def __init__(self, connect_options):
        client_connect_options = {
            "user": connect_options.get("node_agent_user"),
            "ip": connect_options.get("node_agent_ip"),
            "port": connect_options.get("node_agent_port"),
            "cert_path": connect_options.get("node_agent_cert_path"),
            "auth_token": connect_options.get("node_agent_auth_token"),
        }
        self.client = RpcClient(client_connect_options)
        self.client.connect()

    def get_host_port_user(self):
        return get_host_port_user(self.connect_options)

    def close(self):
        self.client.close()

    def run_command_raw(self, command, **kwargs):
        result = RemoteShellOutput()
        try:
            kwargs.setdefault('bash', True)
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

    def check_exec_command(self, command, **kwargs):
        skip_cmd_logging = kwargs.get('skip_cmd_logging', False)
        result = self.run_command_raw(command, **kwargs)
        if result.exited:
            if result.stdout:
                # Log the stdout for debugging purposes.
                logging.error(result.stdout)
            cmd = ' '.join(command).encode('utf-8') if isinstance(command, list) else command
            raise YBOpsRuntimeError('Remote command \'{}\' failed with error code {}: {}'.format(
                "" if skip_cmd_logging else cmd, result.exited, result.stderr))
        return result.stdout

    def exec_command(self, command, **kwargs):
        kwargs.setdefault('bash', True)
        result = self.client.exec_command(command, **kwargs)
        return result.rc, result.stdout, result.stderr

    def check_exec_script(self, local_script_name, params):
        # Copied from exec_script of ssh.py to run a shell script.
        if not isinstance(params, str):
            params = ' '.join(params)

        with open(local_script_name, "r") as f:
            local_script = f.read()

        # Heredoc syntax for input redirection from a local shell script.
        command = f"/bin/bash -s {params} <<'EOF'\n{local_script}\nEOF"
        return self.check_exec_command(command)

    def put_file(self, local_path, remote_path, **kwargs):
        self.client.put_file(local_path, remote_path, **kwargs)

    # Checks if the file exists on the remote, and if not, it puts it there.
    def put_file_if_not_exists(self, local_path, remote_path, file_name, **kwargs):
        # Command fails if the file does not exist.
        rc, stdout, _ = self.exec_command('ls ' + remote_path, **kwargs)
        if rc != 0 or file_name not in stdout:
            self.put_file(local_path, os.path.join(remote_path, file_name), **kwargs)

    def fetch_file(self, remote_file_name, local_file_name, **kwargs):
        self.client.fetch_file(remote_file_name, local_file_name, **kwargs)

    def invoke_method(self, param, **kwargs):
        result = self.client.invoke_method_async(param, **kwargs)
        if result.stderr != '' or result.rc != 0:
            raise YBOpsRecoverableError(
                "Remote method invocation failed with "
                "return code '{}' and error '{}'".format(result.rc,
                                                         result.stderr)
            )
        return result.obj


class _KubectlRemoteShell(object):
    """_KubectlRemoteShell class is used to run remote shell commands in Kubernetes pods
    using kubectl exec.
    """

    def __init__(self, connect_options):
        assert connect_options.get("kubectl_pod") is not None, 'kubectl_pod is required'

        self.kubectl_conn = KubectlClient(
            pod_name=connect_options.get("kubectl_pod"),
            namespace=connect_options.get("kubectl_namespace", "default"),
            kubeconfig=connect_options.get("kubectl_kubeconfig"),
            container=connect_options.get("kubectl_container")
        )
        self.connect_options = connect_options
        self.connected = True

    def get_host_port_user(self):
        return get_host_port_user(self.connect_options)

    def close(self):
        if self.connected:
            self.kubectl_conn.close_connection()

    def run_command_raw(self, command, **kwargs):
        result = RemoteShellOutput()
        try:
            kwargs.setdefault('output_only', True)
            output = self.kubectl_conn.exec_command(command, **kwargs)
            result.stdout = output
            result.exited = 0
        except Exception as e:
            result.stderr = str(e)
            result.exited = 1

        return result

    def run_command(self, command, **kwargs):
        result = self.run_command_raw(command, **kwargs)

        if result.exited:
            cmd = ' '.join(command).encode('utf-8') if isinstance(command, list) else command
            raise YBOpsRecoverableError(
                f"Remote shell command '{cmd}' failed with "
                f"return code '{result.exited}' and error '{result.stderr}'"
            )
        return result

    def exec_command(self, command, **kwargs):
        output_only = kwargs.get('output_only', False)
        if output_only:
            result = self.run_command(command, **kwargs)
            return result.stdout
        else:
            # This returns rc, stdout, stderr.
            return self.kubectl_conn.exec_command(command, **kwargs)

    def exec_script(self, local_script_name, params):
        return self.kubectl_conn.exec_script(local_script_name, params)

    def put_file(self, local_path, remote_path, **kwargs):
        self.kubectl_conn.upload_file_to_remote_server(local_path, remote_path, **kwargs)

    # Checks if the file exists on the remote, and if not, it puts it there.
    def put_file_if_not_exists(self, local_path, remote_path, file_name, **kwargs):
        result = self.run_command('ls ' + remote_path, **kwargs)
        if file_name not in result.stdout:
            self.put_file(local_path, os.path.join(remote_path, file_name), **kwargs)

    def fetch_file(self, remote_file_name, local_file_name, **kwargs):
        self.kubectl_conn.download_file_from_remote_server(remote_file_name, local_file_name, **kwargs)

    def invoke_method(self, input, **kwargs):
        raise NotImplementedError("kubectl does not support method invocation")
