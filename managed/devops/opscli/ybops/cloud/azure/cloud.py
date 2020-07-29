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

from ybops.common.exceptions import YBOpsRuntimeError
from ybops.cloud.common.cloud import AbstractCloud
from ybops.cloud.azure.command import AzureNetworkCommand, AzureInstanceCommand, \
    AzureAccessCommand, AzureQueryCommand
from ybops.cloud.azure.utils import AzureBootstrapClient, AzureCloudAdmin


class AzureCloud(AbstractCloud):
    """Subclass related to Azure specific functionality.
    """
    def __init__(self):
        super(AzureCloud, self).__init__("azu")
        self.admin = None

    def get_admin(self, region):
        if self.admin is None:
            self.admin = AzureCloudAdmin(self.metadata, region)
        return self.admin

    def add_subcommands(self):
        """Override to implement cloud specific commands.
        """
        self.add_subcommand(AzureInstanceCommand())
        self.add_subcommand(AzureNetworkCommand())
        self.add_subcommand(AzureAccessCommand())
        self.add_subcommand(AzureQueryCommand())

    def network_bootstrap(self, args):
        # hardcoded for now but will eventually read in from custom_payload
        print(json.dumps({"westus2": {"security_group": [{"id": "", "name": "yb-us-west-2-sg"}],
                          "vpc_id": "", "zones": {"1": "", "2": "", "3": ""}}}))

    def create_instance(self, args, adminSSH):
        vmName = args.search_pattern
        region = args.region
        zone = args.zone
        subnet = os.environ.get("AZURE_SUBNET")
        numVolumes = args.num_volumes
        volSize = args.volume_size
        private_key_file = args.private_key_file
        instanceType = args.instance_type
        image = args.machine_image
        self.get_admin(region).create_vm(vmName, zone, numVolumes, subnet, private_key_file,
                                         volSize, instanceType, adminSSH, image)

    def destroy_instance(self, args):
        host_info = self.get_host_info(args)
        self.get_admin(args.region).destroy_instance(args.search_pattern, host_info)

    def get_instance_types(self, args):
        return self.get_admin(args.region).get_instance_types()

    def get_host_info(self, args, get_all=False):
        return self.get_admin(args.region).get_host_info(args.search_pattern, get_all)

    def get_device_names(self, args):
        return ["sd{}".format(chr(i)) for i in range(ord('c'), ord('c') + args.num_volumes)]

    def update_disk(self, args):
        raise YBOpsRuntimeError("Update Disk not implemented for Azure")
