#!/usr/bin/env python
#
# Copyright 2022 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

from ansible.errors import AnsibleConnectionFailure, AnsibleError
from ansible.plugins.connection import ConnectionBase
from ansible.utils.display import Display

from ybops.node_agent.shell_client import RpcShellClient

display = Display()

DOCUMENTATION = """
    author: Yugabyte Team
    connection: node_agent_rpc
    short_description: Interact with a node agent via gRPC.
    description:
        - Run commands or put/fetch files to a node agent server.
    version_added: 2.8
    options:
      ip:
        description:
          - IP of the node agent server.
        default: inventory_hostname
        vars:
          - name: rpc_ip
        required: True
      port:
        description:
          - Port of the node agent server.
        vars:
          - name: rpc_port
      cert_path:
        description:
          - Cert path to verify the server self-signed cert.
        vars:
          - name: rpc_cert_path
        required: True
      auth_token:
        description:
          - Auth token to authenticate the RPC requests.
        vars:
          - name: rpc_auth_token
        required: True
"""


class Connection(ConnectionBase):
    """Node agent connection plugin"""

    transport = "node_agent_rpc"

    def __init__(self, *args, **kwargs):
        super(Connection, self).__init__(*args, **kwargs)
        self._connected = False

    def _connect(self):
        """
        Create GRPC connection to the node agent.
        :return: None
        """

        if self._connected:
            return
        try:
            self.ip = self.get_option("ip")
            self.port = self.get_option("port")
            self.cert_path = self.get_option("cert_path")
            self.auth_token = self.get_option("auth_token")
            assert self.ip is not None, 'Node agent server ip is required'
            assert self.port is not None, "Node agent server port is required"
            assert self.cert_path is not None, 'Node agent cert_path is required'
            assert self.auth_token is not None, 'Node agent auth_token is required'
            connect_params = {
                "ip": self.ip,
                "port": self.port,
                "cert_path": self.cert_path,
                "auth_token": self.auth_token
            }
            display.vvv("Connecting to node agent {}:{}".format(self.ip, self.port))
            self.client = RpcShellClient(connect_params)
            self.client.connect()
            self._connected = True
        except Exception as e:
            raise AnsibleConnectionFailure(
                "Failed to connect: %s" % e
            )

    def close(self):
        """
        Close the active connection to the node agent.
        :return: None
        """

        if self._connected:
            display.vvv("Closing gRPC connection to node agent {}:{}".format(self.ip, self.port))
            self.client.close()

    def exec_command(self, cmd, in_data=None, sudoable=True):
        """
        Run a command on the remote host.
        """

        try:
            super(Connection, self).exec_command(cmd, in_data=in_data, sudoable=sudoable)
            output = self.client.exec_command(cmd)
            return output.rc, output.stdout, output.stderr
        except Exception as e:
            raise AnsibleError(
                "Failed to execute command %s" % e
            )

    def put_file(self, in_path, out_path):
        """
        Transfer a file from local to remote.
        """

        try:
            self.client.put_file(in_path, out_path)
        except Exception as e:
            raise AnsibleError(
                "Failed to put file %s" % e
            )

    def fetch_file(self, in_path, out_path):
        """
        Fetch a file from remote to local.
        """

        try:
            self.client.fetch_file(in_path, out_path)
        except Exception as e:
            raise AnsibleError(
                "Failed to fetch file %s" % e
            )
