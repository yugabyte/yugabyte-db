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
import time

from ybops.cloud.common.method import (
    AbstractInstancesMethod, AbstractAccessMethod, AbstractMethod
)
from ybops.cloud.oci.utils import (
    OCI_VOLUME_TYPE_STANDARD, OCI_VOLUME_TYPE_HIGH_PERFORMANCE,
    OCI_VOLUME_TYPE_ULTRA_HIGH_PERFORMANCE, OCI_VOLUME_TYPE_BALANCED,
    OCI_VOLUME_TYPE_HIGHER_PERF, OCI_VOLUME_TYPE_LOWER_COST
)
from ybops.common.exceptions import YBOpsRuntimeError, get_exception_message
from ybops.utils.ssh import format_rsa_key, validated_key_file


class OciQueryRegionsMethod(AbstractMethod):

    def __init__(self, base_command):
        super(OciQueryRegionsMethod, self).__init__(base_command, "regions")

    def callback(self, args):
        print(json.dumps(self.cloud.get_regions()))


class OciQueryZonesMethod(AbstractMethod):

    def __init__(self, base_command):
        super(OciQueryZonesMethod, self).__init__(base_command, "zones")

    def add_extra_args(self):
        super(OciQueryZonesMethod, self).add_extra_args()
        self.parser.add_argument(
            "--dest_vpc_id", default=None,
            help="VCN OCID to get zone and subnet info for."
        )
        self.parser.add_argument(
            "--custom_payload", required=False,
            help="JSON payload of per-region data."
        )

    def callback(self, args):
        print(json.dumps(self.cloud.get_zones(args)))


class OciQueryVpcMethod(AbstractMethod):

    def __init__(self, base_command):
        super(OciQueryVpcMethod, self).__init__(base_command, "vpc")

    def add_extra_args(self):
        super(OciQueryVpcMethod, self).add_extra_args()
        self.parser.add_argument(
            "--dest_vpc_id", default=None,
            help="VCN OCID to query."
        )
        self.parser.add_argument(
            "--custom_payload", required=False,
            help="JSON payload of per-region data."
        )

    def callback(self, args):
        print(json.dumps(self.cloud.query_vpc(args)))


class OciQueryInstanceTypesMethod(AbstractMethod):

    def __init__(self, base_command):
        super(OciQueryInstanceTypesMethod, self).__init__(base_command, "instance_types")

    def add_extra_args(self):
        super(OciQueryInstanceTypesMethod, self).add_extra_args()
        self.parser.add_argument("--regions", nargs='+')
        self.parser.add_argument(
            "--custom_payload", required=False,
            help="JSON payload of per-region data."
        )

    def callback(self, args):
        print(json.dumps(self.cloud.get_instance_types(args)))


class OciQueryCurrentHostMethod(AbstractMethod):

    def __init__(self, base_command):
        super(OciQueryCurrentHostMethod, self).__init__(base_command, "current-host")
        self.need_validation = False

    def callback(self, args):
        print(json.dumps(self.cloud.get_current_host_info(args)))


class OciQueryDeviceNames(AbstractMethod):
    """Query method for getting device names for OCI block volumes."""

    def __init__(self, base_command):
        super(OciQueryDeviceNames, self).__init__(base_command, "device_names")
        # No cloud credentials needed for device name computation
        self.need_validation = False

    def add_extra_args(self):
        super(OciQueryDeviceNames, self).add_extra_args()
        self.parser.add_argument(
            "--volume_type",
            choices=[
                OCI_VOLUME_TYPE_STANDARD,
                OCI_VOLUME_TYPE_HIGH_PERFORMANCE,
                OCI_VOLUME_TYPE_ULTRA_HIGH_PERFORMANCE,
                OCI_VOLUME_TYPE_BALANCED,
                OCI_VOLUME_TYPE_HIGHER_PERF,
                OCI_VOLUME_TYPE_LOWER_COST
            ],
            default=OCI_VOLUME_TYPE_BALANCED,
            help="Storage type for OCI block volumes."
        )
        self.parser.add_argument(
            "--instance_type",
            required=False,
            help="The instance type to act on"
        )
        self.parser.add_argument(
            "--num_volumes", type=int, default=0,
            help="Number of volumes to mount at the default path (/mnt/d#)"
        )

    def callback(self, args):
        print(json.dumps(self.cloud.get_device_names(args)))


class OciAccessAddKeyMethod(AbstractAccessMethod):

    def __init__(self, base_command):
        super(OciAccessAddKeyMethod, self).__init__(base_command, "add-key")

    def callback(self, args):
        (private_key_file, public_key_file) = self.validate_key_files(args)
        print(json.dumps({"private_key": private_key_file, "public_key": public_key_file}))


class OciAbstractNetworkMethod(AbstractMethod):

    def __init__(self, base_command, method_name):
        super(OciAbstractNetworkMethod, self).__init__(base_command, method_name)

    def add_extra_args(self):
        super(OciAbstractNetworkMethod, self).add_extra_args()
        self.parser.add_argument(
            "--metadata_override", required=False,
            help="A custom YML metadata override file."
        )

    def preprocess_args(self, args):
        super(OciAbstractNetworkMethod, self).preprocess_args(args)
        if args.metadata_override:
            self.cloud.update_metadata(args.metadata_override)


class OciNetworkBootstrapMethod(OciAbstractNetworkMethod):

    def __init__(self, base_command):
        super(OciNetworkBootstrapMethod, self).__init__(base_command, "bootstrap")

    def add_extra_args(self):
        super(OciNetworkBootstrapMethod, self).add_extra_args()
        self.parser.add_argument(
            "--custom_payload", required=False,
            help="JSON payload of per-region data."
        )

    def callback(self, args):
        try:
            self.cloud.network_bootstrap(args)
        except YBOpsRuntimeError as ye:
            print(json.dumps({"error": get_exception_message(ye)}))


class OciNetworkCleanupMethod(OciAbstractNetworkMethod):

    def __init__(self, base_command):
        super(OciNetworkCleanupMethod, self).__init__(base_command, "cleanup")

    def add_extra_args(self):
        super(OciNetworkCleanupMethod, self).add_extra_args()
        self.parser.add_argument(
            "--custom_payload", required=False,
            help="JSON payload of per-region data."
        )

    def callback(self, args):
        try:
            self.cloud.network_cleanup(args)
            print(json.dumps({"success": "VCN resources cleaned up."}))
        except YBOpsRuntimeError as ye:
            print(json.dumps({"error": get_exception_message(ye)}))


class OciNetworkQueryMethod(OciAbstractNetworkMethod):

    def __init__(self, base_command):
        super(OciNetworkQueryMethod, self).__init__(base_command, "query")

    def add_extra_args(self):
        super(OciNetworkQueryMethod, self).add_extra_args()
        self.parser.add_argument(
            "--custom_payload", required=False,
            help="JSON payload of per-region data."
        )

    def callback(self, args):
        try:
            print(json.dumps(self.cloud.query_vpc(args)))
        except YBOpsRuntimeError as ye:
            print(json.dumps({"error": get_exception_message(ye)}))


class OciTagsMethod(AbstractInstancesMethod):

    def __init__(self, base_command):
        super(OciTagsMethod, self).__init__(base_command, "tags")

    def add_extra_args(self):
        super(OciTagsMethod, self).add_extra_args()
        self.parser.add_argument(
            "--remove_tags", required=False,
            help="Tag keys to remove (comma-separated)."
        )

    def callback(self, args):
        self.cloud.modify_tags(args)
