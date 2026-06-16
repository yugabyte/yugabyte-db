#!/usr/bin/env python
#
# Copyright 2026 YugabyteDB, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

import json
import logging
import socket
import requests
import os

from ybops.common.exceptions import YBOpsRuntimeError
from ybops.cloud.common.cloud import AbstractCloud, InstanceState
from ybops.cloud.oci.command import (
    OciNetworkCommand, OciInstanceCommand, OciAccessCommand, OciQueryCommand
)
from ybops.cloud.oci.utils import (
    OciCloudAdmin, OciMetadata, get_oci_config,
    OCI_INSTANCE_RUNNING, OCI_INSTANCE_STOPPED, OCI_INSTANCE_STOPPING,
    OCI_INSTANCE_STARTING, OCI_INSTANCE_PROVISIONING, OCI_INSTANCE_TERMINATED,
    OCI_INSTANCE_TERMINATING, OCI_VOLUME_TYPE_STANDARD,
    OCI_PRIVATE_KEY_CONTENT_ENV,
)


class OciCloud(AbstractCloud):

    BASE_INSTANCE_METADATA_API = "http://169.254.169.254/opc/v2"
    METADATA_API_TIMEOUT_SECONDS = 3

    def __init__(self):
        super(OciCloud, self).__init__("oci")
        self.admin = None

    def get_admin(self):
        if self.admin is None:
            self.admin = OciCloudAdmin(self.metadata)
        return self.admin

    def add_subcommands(self):
        self.add_subcommand(OciInstanceCommand())
        self.add_subcommand(OciNetworkCommand())
        self.add_subcommand(OciAccessCommand())
        self.add_subcommand(OciQueryCommand())

    def validate_credentials(self):
        super(OciCloud, self).validate_credentials()
        if OCI_PRIVATE_KEY_CONTENT_ENV not in os.environ:
            if not self.has_machine_credentials():
                raise YBOpsRuntimeError(
                    "Cloud oci missing OCI_PRIVATE_KEY_CONTENT "
                    "and has no machine credentials to default to.")

    def has_machine_credentials(self):
        try:
            instance_info = OciMetadata.get_instance_info()
            return instance_info is not None
        except Exception:
            return False

    def get_image(self, region, shape=None):
        return self.metadata["regions"][region]["image"]

    def query_vpc(self, args):
        result = {}
        regions = [args.region] if args.region else self.get_regions()
        for region in regions:
            result[region] = self.get_admin().network().get_zone_to_subnets(
                args.dest_vpc_id if hasattr(args, 'dest_vpc_id') else None,
                region
            )
            result[region]["default_image"] = self.get_image(region)
        return result

    def get_regions(self):
        return list(self.metadata.get("regions", {}).keys())

    def get_zones(self, args):
        result = {}
        regions = [args.region] if args.region else self.get_regions()
        for region in regions:
            self.get_admin().set_region(region)
            result[region] = self.get_admin().network().get_zone_to_subnets(
                args.dest_vpc_id if hasattr(args, 'dest_vpc_id') else None,
                region
            )
        return result

    def get_instance_types(self, args):
        regions = args.regions if args.regions else self.get_regions()
        result = {}
        for region in regions:
            region_types = self.get_admin().get_instance_types(region)
            for name, info in region_types.items():
                if name not in result:
                    result[name] = {
                        "numCores": info["numCores"],
                        "memSizeGb": info["memSizeGb"],
                        "description": info["description"],
                        "isShared": info["isShared"],
                        "prices": {}
                    }
                result[name]["prices"][region] = [{"os": "Linux", "price": 0.0}]
        return result

    def _get_user_data(self, args):
        if hasattr(args, 'boot_script') and args.boot_script:
            try:
                with open(args.boot_script, 'r') as f:
                    return f.read()
            except Exception as e:
                logging.warning("Failed to read boot script: {}".format(e))
        return None

    def get_host_info(self, args, get_all=False):
        region = args.region if hasattr(args, 'region') and args.region else None
        return self.get_admin().get_instances(
            region=region,
            search_pattern=args.search_pattern,
            get_all=get_all,
            node_uuid=getattr(args, 'node_uuid', None)
        )

    def get_device_names(self, args):
        return ["sd{}".format(chr(ord('b') + i))
                for i in range(args.num_volumes)]

    def get_console_output(self, args):
        host_info = self.get_host_info(args)
        if host_info:
            return self.get_admin().get_console_history(host_info['id'])
        return ""

    def network_bootstrap(self, args):
        custom_payload = json.loads(args.custom_payload)
        per_region_meta = custom_payload.get("perRegionMetadata", {})
        added_region_codes = custom_payload.get("addedRegionCodes")

        if added_region_codes is None:
            added_region_codes = per_region_meta.keys()

        components = {}
        for region, metadata in per_region_meta.items():
            if region in added_region_codes:
                components[region] = self.get_admin().network(metadata).bootstrap(region)
            else:
                components[region] = self.get_admin().network(metadata).to_components()

        print(json.dumps(components))

    def network_cleanup(self, args):
        per_region_meta = json.loads(args.custom_payload).get("perRegionMetadata", {})
        for region, metadata in per_region_meta.items():
            self.get_admin().network(metadata).cleanup(region)

    def normalize_instance_state(self, instance_state):
        if instance_state:
            instance_state = instance_state.upper()
            if instance_state in (OCI_INSTANCE_PROVISIONING, OCI_INSTANCE_STARTING):
                return InstanceState.STARTING
            if instance_state == OCI_INSTANCE_RUNNING:
                return InstanceState.RUNNING
            if instance_state == OCI_INSTANCE_STOPPING:
                return InstanceState.STOPPING
            if instance_state == OCI_INSTANCE_STOPPED:
                return InstanceState.STOPPED
            if instance_state == OCI_INSTANCE_TERMINATING:
                return InstanceState.TERMINATING
            if instance_state == OCI_INSTANCE_TERMINATED:
                return InstanceState.TERMINATED
        return InstanceState.UNKNOWN

    def get_current_host_info(self, args=None):
        try:
            instance_info = OciMetadata.get_instance_info()
            if instance_info:
                return {
                    "region": instance_info.get("region"),
                    "availabilityDomain": instance_info.get("availabilityDomain"),
                    "compartmentId": instance_info.get("compartmentId"),
                    "instanceId": instance_info.get("id"),
                    "shape": instance_info.get("shape")
                }
            raise YBOpsRuntimeError("Unable to fetch OCI instance metadata")
        except Exception as e:
            raise YBOpsRuntimeError(
                "Unable to auto-discover OCI provider information: {}".format(e))
