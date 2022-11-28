#!/usr/bin/env python
#
# Copyright 2022 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

import os
import shlex
import time
import traceback

from ansible.module_utils._text import to_native
from grpc import secure_channel, ssl_channel_credentials, metadata_call_credentials, \
    composite_channel_credentials, AuthMetadataPlugin
from ybops.node_agent.server_pb2 import DownloadFileRequest, ExecuteCommandRequest, FileInfo, \
    PingRequest, UploadFileRequest
from ybops.node_agent.server_pb2_grpc import NodeAgentStub

SERVER_READY_RETRY_LIMIT = 60
PING_TIMEOUT_SEC = 10
COMMAND_EXECUTION_TIMEOUT_SEC = 300
FILE_UPLOAD_DOWNLOAD_TIMEOUT_SEC = 600


class RpcShellOutput(object):
    """
    RpcShellOutput class converts the o/p in the standard format with o/p, err,
    rc status attached to it.
    """

    def __init__(self):
        self.rc = 0
        self.stdout = ''
        self.stderr = ''


class AuthTokenCallback(AuthMetadataPlugin):
    def __init__(self, auth_token):
        self.auth_token = auth_token

    def __call__(self, context, callback):
        callback((("authorization", self.auth_token),), None)


class RpcShellClient(object):
    """
    RpcShellClient class is used to run remote shell commands against a node agent using gRPC.
    """

    def __init__(self, options):
        self.ip = options.get("ip")
        self.port = options.get("port")
        self.cert_path = options.get("cert_path")
        self.auth_token = options.get("auth_token")
        assert self.ip is not None, 'RPC ip is required'
        assert self.port is not None, 'RPC port is required'
        assert self.cert_path is not None, 'RPC cert_path is required'
        assert self.auth_token is not None, 'RPC api_token is required'

    def connect(self):
        """
        Create GRPC connection to the node agent.
        :return: None
        """

        root_certs = open(self.cert_path, mode='rb').read()
        cert_creds = ssl_channel_credentials(root_certificates=root_certs)
        auth_creds = metadata_call_credentials(
                AuthTokenCallback(self.auth_token), name='auth_creds')
        credentials = composite_channel_credentials(cert_creds, auth_creds)
        self.channel = secure_channel(self.ip + ':' + str(self.port), credentials)
        self.connected = True

    def close(self):
        """
        Close the active connection to the node agent.
        :return: None
        """

        if self.connected:
            self.channel.close()

    def exec_command(self, cmd, **kwargs):
        """
        Run a command on the remote host.
        """

        output = RpcShellOutput()
        try:
            timeout_sec = kwargs.get('timeout', COMMAND_EXECUTION_TIMEOUT_SEC)
            if isinstance(cmd, str):
                cmd_args_list = shlex.split(to_native(cmd, errors='surrogate_or_strict'))
            else:
                cmd_args_list = cmd
            stub = NodeAgentStub(self.channel)
            for response in stub.ExecuteCommand(
                    ExecuteCommandRequest(command=cmd_args_list), timeout=timeout_sec):
                if response.HasField('error'):
                    output.rc = response.error.code
                    output.stderr = response.error.message
                    break
                else:
                    output.stdout = response.output if output.stdout is None \
                        else output.stdout + response.output
        except Exception as e:
            traceback.print_exc()
            output.rc = 1
            output.stderr = str(e)
        return output

    def read_iterfile(self, in_path, out_path, chunk_size=1024):
        file_info = FileInfo()
        file_info.filename = out_path
        yield UploadFileRequest(fileInfo=file_info)
        with open(in_path, mode='rb') as f:
            while True:
                chunk = f.read(chunk_size)
                if chunk:
                    yield UploadFileRequest(chunkData=chunk)
                else:
                    return

    def put_file(self, local_path, remote_path, **kwargs):
        """
        Transfer a file from local to the remote node agent.
        """

        timeout_sec = kwargs.get('timeout', FILE_UPLOAD_DOWNLOAD_TIMEOUT_SEC)
        stub = NodeAgentStub(self.channel)
        stub.UploadFile(self.read_iterfile(local_path, remote_path), timeout=timeout_sec)

    def fetch_file(self, in_path, out_path, **kwargs):
        """
        Fetch a file from the remote node agent to local.
        """

        timeout_sec = kwargs.get('timeout', FILE_UPLOAD_DOWNLOAD_TIMEOUT_SEC)
        stub = NodeAgentStub(self.channel)
        for response in stub.DownloadFile(
                DownloadFileRequest(filename=in_path), timeout=timeout_sec):
            with open(out_path, mode="ab") as f:
                f.write(response.chunk_data)

    def wait_for_server(self, num_retries=SERVER_READY_RETRY_LIMIT, **kwargs):
        """
        Wait for the server to be ready to serve request.
        Returns:
            (boolean): Returns true if the server is ready.
        """

        retry_count = 0
        while retry_count < num_retries:
            if self.is_server_ready(**kwargs):
                return True
            time.sleep(1)
            retry_count += 1

        return False

    def is_server_ready(self, **kwargs):
        """
        Checks if the server is ready to serve request.
        Returns:
            (boolean): Returns true if the server is ready.
        """

        stub = NodeAgentStub(self.channel)
        try:
            timeout_sec = kwargs.get('timeout', PING_TIMEOUT_SEC)
            stub.Ping(PingRequest(data="connection-test"), timeout=timeout_sec)
            return True
        except Exception as e:
            return False
