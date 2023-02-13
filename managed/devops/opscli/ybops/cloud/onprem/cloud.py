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

from ybops.cloud.common.cloud import AbstractCloud
from ybops.cloud.common.method import AbstractInstancesMethod
from ybops.cloud.onprem.command import OnPremInstanceCommand, OnPremAccessCommand
from ybops.common.exceptions import YBOpsRuntimeError


class OnPremCloud(AbstractCloud):
    """Subclass specific to GCP cloud related functionality.
    """
    def __init__(self):
        super(OnPremCloud, self).__init__("onprem")

    def add_extra_args(self):
        """Override to setup cloud-specific command line flags.
        """
        super(OnPremCloud, self).add_extra_args()
        self.parser.add_argument("--node_metadata", required=False)

    def add_subcommands(self):
        """Override to setup the cloud-specific instances of the subcommands.
        """
        self.add_subcommand(OnPremInstanceCommand())
        self.add_subcommand(OnPremAccessCommand())

    def get_host_info(self, args, get_all=False):
        """Override to get host specific information from an on premise deployed machine.

        Required fields in args:
          search_pattern: the regex or direct name to search hosts by
        """
        if args.node_metadata is None:
            raise YBOpsRuntimeError("Must provide node_metadata!")

        instances = [json.loads(args.node_metadata)]

        results = []
        for data in instances:
            node_name = data.get("nodeName")
            if node_name != args.search_pattern:
                return None
            result = dict(
                name=node_name,
                public_ip=data.get("ip"),
                # TODO: do we have both IPs stored in yugaware?
                private_ip=data.get("ip"),
                region=data.get("region"),
                zone=data.get("zone"),
                instance_type=data.get("instanceType"),
                ssh_port=args.custom_ssh_port or data.get("sshPort", 22),
                ssh_user=data.get("sshUser"),
                # TODO: would we ever use this for non yugabyte servers?
                server_type=AbstractInstancesMethod.YB_SERVER_TYPE
            )
            if not get_all:
                return result
            results.append(result)
        return results
