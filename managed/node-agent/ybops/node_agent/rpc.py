#!/usr/bin/env python
#
# Copyright 2022 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

import logging
import shlex
import time
import uuid

from ansible.module_utils._text import to_native
from google.protobuf.json_format import MessageToJson
from google.protobuf.message import Message
from grpc import secure_channel, ssl_channel_credentials, metadata_call_credentials, \
    composite_channel_credentials, AuthMetadataPlugin, RpcError, StatusCode
from ybops.node_agent.server_pb2 import DownloadFileRequest, ExecuteCommandRequest, FileInfo, \
    PingRequest, UploadFileRequest, SubmitTaskRequest, DescribeTaskRequest, AbortTaskRequest, \
    CommandInput, Error
from ybops.node_agent.server_pb2_grpc import NodeAgentStub

SERVER_READY_RETRY_LIMIT = 60
PING_TIMEOUT_SEC = 10
COMMAND_EXECUTION_TIMEOUT_SEC = 900
FILE_UPLOAD_DOWNLOAD_TIMEOUT_SEC = 1800
FILE_UPLOAD_CHUNK_BYTES = 524288


class RpcOutput(object):
    """
    RpcOutput class converts the o/p in the standard format with o/p, err,
    rc status attached to it.
    """

    def __init__(self):
        self.rc = 0
        self.stdout = ''
        self.stderr = ''
        self.obj = None


class AuthTokenCallback(AuthMetadataPlugin):
    def __init__(self, auth_token):
        self.auth_token = auth_token

    def __call__(self, context, callback):
        callback((("authorization", self.auth_token),), None)


class RpcClient(object):
    """
    RpcClient class is used to run remote commands or methods against a node agent using gRPC.
    """

    def __init__(self, options):
        self.user = options.get("user", "")
        self.ip = options.get("ip")
        self.port = options.get("port")
        self.cert_path = options.get("cert_path")
        self.auth_token = options.get("auth_token")
        assert self.ip is not None, 'RPC ip is required'
        assert self.port is not None, 'RPC port is required'
        assert self.cert_path is not None, 'RPC cert_path is required'
        assert self.auth_token is not None, 'RPC api_token is required'
        with open(self.cert_path, mode='rb') as file:
            self.root_certs = file.read()

    def connect(self):
        """
        Create GRPC connection to the node agent.
        :return: None
        """
        cert_creds = ssl_channel_credentials(root_certificates=self.root_certs)
        auth_creds = metadata_call_credentials(
                AuthTokenCallback(self.auth_token), name='auth_creds')
        credentials = composite_channel_credentials(cert_creds, auth_creds)
        self.channel = secure_channel(self.ip + ':' + str(self.port), credentials)
        self.stub = NodeAgentStub(self.channel)
        self.connected = True

    def close(self):
        """
        Close the active connection to the node agent.
        :return: None
        """

        if self.connected:
            self.channel.close()
            self.connected = False

    def exec_command(self, cmd, **kwargs):
        """
        Run a command on the remote host.
        Optional 'async' arg can be passed for long running commands.
        """

        if kwargs.get('async', False):
            return self.exec_command_async(cmd, **kwargs)
        return self.exec_command_sync(cmd, **kwargs)

    def exec_command_sync(self, cmd, **kwargs):
        """
        Run a sync short running command on the remote host.
        """

        output = RpcOutput()
        try:
            timeout_sec = kwargs.get('timeout', COMMAND_EXECUTION_TIMEOUT_SEC)
            bash = kwargs.get('bash', False)
            if isinstance(cmd, str):
                if bash:
                    cmd_args_list = ["/bin/bash", "-c", cmd]
                else:
                    cmd_args_list = shlex.split(to_native(cmd, errors='surrogate_or_strict'))
            else:
                if bash:
                    # Need to join with spaces, but surround arguments with spaces
                    # using "'" character.
                    cmd_str = ' '.join(
                        list(map(lambda part: part if ' ' not in part else "'" + part + "'", cmd)))
                    cmd_args_list = ["/bin/bash", "-c", cmd_str]
                else:
                    cmd_args_list = cmd
            for response in self.stub.ExecuteCommand(
                    ExecuteCommandRequest(user=self.user, command=cmd_args_list),
                    timeout=timeout_sec):
                if response.HasField('error'):
                    output.rc = response.error.code
                    output.stderr = response.error.message
                    break
                else:
                    output.stdout = response.output if output.stdout is None \
                        else output.stdout + response.output
        except Exception as e:
            output.rc = 1
            output.stderr = str(e)
        return output

    def exec_command_async(self, cmd, **kwargs):
        """
        Run an async long running command on the remote host.
        """

        output = RpcOutput()
        task_id = kwargs.get('task_id', str(uuid.uuid4()))
        try:
            timeout_sec = kwargs.get('timeout', COMMAND_EXECUTION_TIMEOUT_SEC)
            bash = kwargs.get('bash', False)
            if isinstance(cmd, str):
                if bash:
                    cmd_args_list = ["/bin/bash", "-c", cmd]
                else:
                    cmd_args_list = shlex.split(to_native(cmd, errors='surrogate_or_strict'))
            else:
                if bash:
                    # Need to join with spaces, but surround arguments with spaces
                    # using "'" character.
                    cmd_str = ' '.join(
                        list(map(lambda part: part if ' ' not in part else "'" + part + "'", cmd)))
                    cmd_args_list = ["/bin/bash", "-c", cmd_str]
                else:
                    cmd_args_list = cmd
            cmd_input = CommandInput(command=cmd_args_list)
            self.stub.SubmitTask(SubmitTaskRequest(user=self.user, taskId=task_id,
                                                   commandInput=cmd_input),
                                 timeout=timeout_sec)
            while True:
                try:
                    for response in self.stub.DescribeTask(
                            DescribeTaskRequest(taskId=task_id),
                            timeout=timeout_sec):
                        if response.HasField('error'):
                            output.rc = response.error.code
                            output.stderr = response.error.message
                            break
                        output.stdout = response.output if output.stdout is None \
                            else output.stdout + response.output
                    break
                except RpcError as e:
                    if e.code() == StatusCode.DEADLINE_EXCEEDED:
                        logging.info("Reconnecting for task {} as client timed out".format(task_id))
                        continue
                    raise e
        except Exception as e:
            self.abort_task(task_id, **kwargs)
            output.rc = 1
            output.stderr = str(e)
        return output

    def invoke_method_async(self, param, **kwargs):
        """
        Run an async method invocation on the remote host.
        """

        output = RpcOutput()
        task_id = kwargs.get('task_id', str(uuid.uuid4()))
        output_json = kwargs.get('output_json', True)
        try:
            timeout_sec = kwargs.get('timeout', COMMAND_EXECUTION_TIMEOUT_SEC)
            request = SubmitTaskRequest(user=self.user, taskId=task_id)
            self._set_request_oneof_field(request, param)
            self.stub.SubmitTask(request, timeout=timeout_sec)
            while True:
                try:
                    for response in self.stub.DescribeTask(
                            DescribeTaskRequest(taskId=task_id),
                            timeout=timeout_sec):
                        if response.HasField('error'):
                            output.rc = response.error.code
                            output.stderr = response.error.message
                            break
                        if response.HasField('output'):
                            output.stdout = response.output if output.stdout is None \
                                else output.stdout + response.output
                        else:
                            output.obj = self._get_response_oneof_field(response)
                            if output_json:
                                # JSON library of python cannot serialize this output.
                                output.obj = MessageToJson(output.obj)
                            break
                    break
                except RpcError as e:
                    if e.code() == StatusCode.DEADLINE_EXCEEDED:
                        logging.info("Reconnecting for task {} as client timed out".format(task_id))
                        continue
                    raise e
        except Exception as e:
            self.abort_task(task_id, **kwargs)
            output.rc = 1
            output.stderr = str(e)
        return output

    def abort_task(self, task_id, **kwargs):
        try:
            timeout_sec = kwargs.get('timeout', COMMAND_EXECUTION_TIMEOUT_SEC)
            self.stub.AbortTask(AbortTaskRequest(taskId=task_id), timeout=timeout_sec)
        except Exception:
            # Ignore error.
            logging.error("Failed to abort remote task {}".format(task_id))

    def read_iterfile(self, user, in_path, out_path, chmod=0, chunk_size=FILE_UPLOAD_CHUNK_BYTES):
        file_info = FileInfo()
        file_info.filename = out_path
        yield UploadFileRequest(chmod=chmod, user=user, fileInfo=file_info)
        with open(in_path, mode='rb') as f:
            while True:
                chunk = f.read(chunk_size)
                if chunk:
                    yield UploadFileRequest(user=user, chmod=chmod, chunkData=chunk)
                else:
                    return

    def put_file(self, local_path, remote_path, **kwargs):
        """
        Transfer a file from local to the remote node agent.
        """
        chmod = kwargs.get('chmod', 0)
        timeout_sec = kwargs.get('timeout', FILE_UPLOAD_DOWNLOAD_TIMEOUT_SEC)
        self.stub.UploadFile(self.read_iterfile(self.user, local_path, remote_path, chmod),
                             timeout=timeout_sec)

    def fetch_file(self, in_path, out_path, **kwargs):
        """
        Fetch a file from the remote node agent to local.
        """

        timeout_sec = kwargs.get('timeout', FILE_UPLOAD_DOWNLOAD_TIMEOUT_SEC)
        for response in self.stub.DownloadFile(
                DownloadFileRequest(filename=in_path, user=self.user), timeout=timeout_sec):
            with open(out_path, mode="ab") as f:
                f.write(response.chunkData)

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

        try:
            timeout_sec = kwargs.get('timeout', PING_TIMEOUT_SEC)
            self.stub.Ping(PingRequest(), timeout=timeout_sec)
            return True
        except Exception as e:
            return False

    def _set_request_oneof_field(self, request, field):
        descriptor = getattr(field, 'DESCRIPTOR', None)
        if descriptor and isinstance(field, Message):
            for oneofs in request.DESCRIPTOR.oneofs:
                for f in oneofs.fields:
                    if f.message_type.full_name == descriptor.full_name:
                        getattr(request, f.name).CopyFrom(field)
                        return
        raise TypeError("Unknown request type: " + str(type(field)))

    def _get_response_oneof_field(self, response):
        descriptor = getattr(response, 'DESCRIPTOR', None)
        if descriptor:
            for oneofs in descriptor.oneofs:
                for f in oneofs.fields:
                    obj = getattr(response, f.name, None)
                    if obj is None:
                        continue
                    if not isinstance(obj, Message) or isinstance(obj, Error):
                        continue
                    return obj
        raise TypeError("Unknown response type: " + str(type(response)))
