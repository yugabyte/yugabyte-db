#!/usr/bin/env python
#
# Copyright 2022 YugabyteDB, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

import json
import jwt
import logging
import os
import shlex
import time
import uuid

from ansible.module_utils._text import to_native
from google.protobuf.json_format import MessageToJson
from google.protobuf.message import Message
from grpc import secure_channel, ssl_channel_credentials, metadata_call_credentials, \
    composite_channel_credentials, AuthMetadataPlugin, RpcError, StatusCode
from ybops.node_agent.common_pb2 import Error
from ybops.node_agent.server_pb2 import DownloadFileRequest, ExecuteCommandRequest, FileInfo, \
    PingRequest, UploadFileRequest, SubmitTaskRequest, DescribeTaskRequest, AbortTaskRequest, \
    CommandInput
from ybops.node_agent.server_pb2_grpc import NodeAgentStub
from ybops.common.exceptions import YBOpsRuntimeError

CORRELATION_ID = "correlation_id"
REQUEST_LOG_LEVEL = "node_agent_request_log_level"
X_CORRELATION_ID = "x-correlation-id"
X_REQUEST_ID = "x-request-id"
X_REQUEST_LOG_LEVEL = "x-request-log-level"
SERVER_READY_RETRY_LIMIT = 60
PING_TIMEOUT_SEC = 10
RPC_TIMEOUT_SEC = 900
FILE_UPLOAD_CHUNK_BYTES = 524288

# GRPC and Node Agent specific configurations.
GRPC_WAIT_FOR_READY = False
GRPC_KEEPALIVE_TIME_MS_ENV = "grpc_keepalive_time_ms"
GRPC_KEEPALIVE_TIMEOUT_MS_ENV = "grpc_keepalive_timeout_ms"
GRPC_KEEPALIVE_TIME_MS_DEFAULT = "10000"
GRPC_KEEPALIVE_TIMEOUT_MS_DEFAULT = "10000"
DESCRIBE_POLL_DEADLINE_MS_ENV = "node_agent_describe_poll_deadline"
DESCRIBE_POLL_DEADLINE_MS_DEFAULT = "10000"

# Max attempt is capped at 5 internally by the underlying client.
# Below values are large enough to detect permanently unreachable server much faster,
# and handle flaky connection.
GRPC_SERVICE_CONFIG = {
    "methodConfig": [
        {
            "name": [{"service": "nodeagent.server.NodeAgent"}],
            "retryPolicy": {
                "maxAttempts": 5,
                "initialBackoff": "5s",
                "maxBackoff": "30s",
                "backoffMultiplier": 2,
                "retryableStatusCodes": ["UNAVAILABLE", "RESOURCE_EXHAUSTED"]
            }
        }
    ]
}


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
        claims = jwt.decode(self.auth_token, options={"verify_signature": False})
        # Use the token expiry (seconds) as the timeout for RPC calls.
        self.rpc_timeout_sec = int(claims.get('exp', 0)) - int(claims.get('iat', time.time()))
        if self.rpc_timeout_sec < RPC_TIMEOUT_SEC:
            self.rpc_timeout_sec = RPC_TIMEOUT_SEC
        self.describe_poll_deadline_ms = int(os.getenv(DESCRIBE_POLL_DEADLINE_MS_ENV,
                                                       DESCRIBE_POLL_DEADLINE_MS_DEFAULT))
        logging.info("RPC time-out is set to {} secs".format(self.rpc_timeout_sec))

        # Prepare channel options.
        self.channel_options = None
        if not GRPC_WAIT_FOR_READY:
            keep_alive_time_ms = int(os.getenv(GRPC_KEEPALIVE_TIME_MS_ENV,
                                               GRPC_KEEPALIVE_TIME_MS_DEFAULT))
            keep_alive_timeout_ms = int(os.getenv(GRPC_KEEPALIVE_TIMEOUT_MS_ENV,
                                                  GRPC_KEEPALIVE_TIMEOUT_MS_DEFAULT))
            self.channel_options = (("grpc.service_config", json.dumps(GRPC_SERVICE_CONFIG)),
                                    ("grpc.keepalive_time_ms", keep_alive_time_ms),
                                    ("grpc.keepalive_timeout_ms", keep_alive_timeout_ms))

    def connect(self):
        """
        Create GRPC connection to the node agent.
        :return: None
        """
        cert_creds = ssl_channel_credentials(root_certificates=self.root_certs)
        auth_creds = metadata_call_credentials(
                AuthTokenCallback(self.auth_token), name='auth_creds')
        credentials = composite_channel_credentials(cert_creds, auth_creds)
        self.channel = secure_channel(self.ip + ':' + str(self.port), credentials,
                                      options=self.channel_options)
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
            timeout_sec = kwargs.get('timeout', self.rpc_timeout_sec)
            metadata = self._get_metadata(**kwargs)
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
                    timeout=timeout_sec, wait_for_ready=GRPC_WAIT_FOR_READY, metadata=metadata):
                if response.HasField('error'):
                    output.rc = response.error.code
                    output.stderr = response.error.message
                    break
                else:
                    output.stdout = response.output if output.stdout is None \
                        else output.stdout + response.output
        except Exception as e:
            output.rc = 1
            output.stderr = str(self._handle_rpc_error(e))
        return output

    def exec_command_async(self, cmd, **kwargs):
        """
        Run an async long running command on the remote host.
        """

        output = RpcOutput()
        task_id = kwargs.get('task_id', str(uuid.uuid4()))
        try:
            timeout_sec = kwargs.get('timeout', self.rpc_timeout_sec)
            metadata = self._get_metadata(**kwargs)
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
                                 timeout=timeout_sec, wait_for_ready=GRPC_WAIT_FOR_READY,
                                 metadata=metadata)
            while True:
                try:
                    for response in self.stub.DescribeTask(
                            DescribeTaskRequest(taskId=task_id),
                            timeout=self.describe_poll_deadline_ms,
                            wait_for_ready=GRPC_WAIT_FOR_READY):
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
            output.stderr = str(self._handle_rpc_error(e))
        return output

    def invoke_method_async(self, param, **kwargs):
        """
        Run an async method invocation on the remote host.
        """

        output = RpcOutput()
        task_id = kwargs.get('task_id', str(uuid.uuid4()))
        output_json = kwargs.get('output_json', True)
        try:
            timeout_sec = kwargs.get('timeout', self.rpc_timeout_sec)
            metadata = self._get_metadata(**kwargs)
            request = SubmitTaskRequest(user=self.user, taskId=task_id)
            self._set_request_oneof_field(request, param)
            self.stub.SubmitTask(request, timeout=timeout_sec, wait_for_ready=GRPC_WAIT_FOR_READY,
                                 metadata=metadata)
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
            output.stderr = str(self._handle_rpc_error(e))
        return output

    def abort_task(self, task_id, **kwargs):
        try:
            timeout_sec = kwargs.get('timeout', self.rpc_timeout_sec)
            metadata = self._get_metadata(**kwargs)
            self.stub.AbortTask(AbortTaskRequest(taskId=task_id), timeout=timeout_sec,
                                metadata=metadata)
        except Exception as e:
            # Ignore error.
            logging.error("Failed to abort remote task {} - {}".format(task_id, str(e)))

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
        timeout_sec = kwargs.get('timeout', self.rpc_timeout_sec)
        metadata = self._get_metadata(**kwargs)
        try:
            self.stub.UploadFile(self.read_iterfile(self.user, local_path, remote_path, chmod),
                                 timeout=timeout_sec, wait_for_ready=GRPC_WAIT_FOR_READY,
                                 metadata=metadata)
        except RpcError as e:
            raise self._handle_rpc_error(e)

    def fetch_file(self, in_path, out_path, **kwargs):
        """
        Fetch a file from the remote node agent to local.
        """

        timeout_sec = kwargs.get('timeout', self.rpc_timeout_sec)
        metadata = self._get_metadata(**kwargs)
        try:
            for response in self.stub.DownloadFile(
                    DownloadFileRequest(filename=in_path, user=self.user),
                    timeout=timeout_sec, wait_for_ready=GRPC_WAIT_FOR_READY, metadata=metadata):
                with open(out_path, mode="ab") as f:
                    f.write(response.chunkData)
        except RpcError as e:
            raise self._handle_rpc_error(e)

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
            metadata = self._get_metadata(**kwargs)
            self.stub.Ping(PingRequest(), timeout=timeout_sec, metadata=metadata)
            return True
        except Exception as e:
            return False

    def _get_metadata(self, **kwargs):
        metadata = []
        correlation_id = os.getenv(CORRELATION_ID, None)
        if correlation_id is None:
            correlation_id = str(uuid.uuid4())
        request_id = str(uuid.uuid4())
        request_log_level = os.getenv(REQUEST_LOG_LEVEL, str(-1))
        logging.info("Using correlation ID: {} and request ID: {}"
                     .format(correlation_id, request_id))
        metadata.append((X_CORRELATION_ID, correlation_id))
        metadata.append((X_REQUEST_ID, request_id))
        if int(request_log_level) >= 0:
            metadata.append((X_REQUEST_LOG_LEVEL, request_log_level))
        return metadata

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

    def _handle_rpc_error(self, e):
        logging.error(str(e))
        if isinstance(e, RpcError):
            if e.code() == StatusCode.DEADLINE_EXCEEDED:
                return YBOpsRuntimeError("Timed out while connecting to node agent at {}:{}"
                                         .format(self.ip, self.port))
            if e.code() == StatusCode.UNAVAILABLE:
                return YBOpsRuntimeError("Node agent is unreachable at {}:{}"
                                         .format(self.ip, self.port))
            return YBOpsRuntimeError("RPC error while connecting to node agent at {}:{} - {}"
                                     .format(self.ip, self.port, e.code()))
        return YBOpsRuntimeError("Unknown error while connecting to node agent at {}:{} - {}"
                                 .format(self.ip, self.port, str(e)))
