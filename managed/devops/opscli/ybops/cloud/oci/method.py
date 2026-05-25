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
    AbstractInstancesMethod, AbstractAccessMethod, AbstractMethod,
    UpdateDiskMethod, ChangeInstanceTypeMethod, CreateInstancesMethod,
    CreateRootVolumesMethod, DestroyInstancesMethod, ProvisionInstancesMethod,
    ReplaceRootVolumeMethod, DeleteRootVolumesMethod, HardRebootInstancesMethod
)
from ybops.cloud.oci.utils import (
    OCI_VOLUME_TYPE_STANDARD, OCI_VOLUME_TYPE_HIGH_PERFORMANCE,
    OCI_VOLUME_TYPE_ULTRA_HIGH_PERFORMANCE, OCI_VOLUME_TYPE_BALANCED,
    OCI_VOLUME_TYPE_HIGHER_PERF, OCI_VOLUME_TYPE_LOWER_COST
)
from ybops.common.exceptions import YBOpsRuntimeError, get_exception_message
from ybops.utils.ssh import format_rsa_key, validated_key_file


class OciCreateInstancesMethod(CreateInstancesMethod):

    SSH_USER = "opc"

    def __init__(self, base_command):
        super(OciCreateInstancesMethod, self).__init__(base_command)
        self.INSTANCE_LOOKUP_RETRY_LIMIT = 60

    def add_extra_args(self):
        super(OciCreateInstancesMethod, self).add_extra_args()
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
            "--ocpus", type=float, default=None,
            help="Number of OCPUs for Flex shapes."
        )
        self.parser.add_argument(
            "--memory_in_gbs", type=float, default=None,
            help="Memory in GBs for Flex shapes."
        )

    def run_create_instance(self, args):
        if args.ssh_user is not None:
            self.SSH_USER = args.ssh_user
        server_type = args.type

        ssh_keys = None
        if args.private_key_file is not None:
            rsa_key = validated_key_file(args.private_key_file)
            public_key = format_rsa_key(rsa_key, public_key=True)
            ssh_keys = public_key

        self.cloud.create_instance(args, server_type, ssh_keys)


class OciProvisionInstancesMethod(ProvisionInstancesMethod):

    def __init__(self, base_command):
        super(OciProvisionInstancesMethod, self).__init__(base_command)

    def add_extra_args(self):
        super(OciProvisionInstancesMethod, self).add_extra_args()
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

    def update_extra_vars_with_args(self, args):
        super(OciProvisionInstancesMethod, self).update_extra_vars_with_args(args)
        self.extra_vars["device_names"] = self.cloud.get_device_names(args)
        self.extra_vars["mount_points"] = self.cloud.get_mount_points_csv(args)


class OciDestroyInstancesMethod(DestroyInstancesMethod):

    def __init__(self, base_command):
        super(OciDestroyInstancesMethod, self).__init__(base_command)

    def callback(self, args):
        self.cloud.destroy_instance(args)


class OciCreateRootVolumesMethod(CreateRootVolumesMethod):

    def __init__(self, base_command):
        super(OciCreateRootVolumesMethod, self).__init__(base_command)
        self.create_method = OciCreateInstancesMethod(base_command)

    def create_master_volume(self, args):
        raise YBOpsRuntimeError(
            "OCI creates boot volumes automatically with instances. "
            "Use create instance method instead."
        )


class OciDeleteRootVolumesMethod(DeleteRootVolumesMethod):

    def __init__(self, base_command):
        super(OciDeleteRootVolumesMethod, self).__init__(base_command)

    def delete_volumes(self, args):
        self.cloud.delete_volumes(args)


class OciReplaceRootVolumeMethod(ReplaceRootVolumeMethod):

    def __init__(self, base_command):
        super(OciReplaceRootVolumeMethod, self).__init__(base_command)

    def _mount_root_volume(self, args, volume):
        raise YBOpsRuntimeError("Root volume replacement not supported for OCI")

    def _host_info_with_current_root_volume(self, args, host_info):
        args.private_ip = host_info["private_ip"]
        return (vars(args), None)


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


class OciChangeInstanceTypeMethod(ChangeInstanceTypeMethod):

    def __init__(self, base_command):
        super(OciChangeInstanceTypeMethod, self).__init__(base_command)

    def add_extra_args(self):
        super(OciChangeInstanceTypeMethod, self).add_extra_args()
        self.parser.add_argument(
            "--ocpus", type=float, default=None,
            help="Number of OCPUs for Flex shapes."
        )
        self.parser.add_argument(
            "--memory_in_gbs", type=float, default=None,
            help="Memory in GBs for Flex shapes."
        )

    def _change_instance_type(self, args, host_info):
        self.cloud.change_instance_type(
            host_info,
            args.instance_type,
            ocpus=getattr(args, 'ocpus', None),
            memory_in_gbs=getattr(args, 'memory_in_gbs', None)
        )

    def _host_info(self, args, host_info):
        args.private_ip = host_info["private_ip"]
        result = vars(args).copy()
        result['instance_type'] = host_info["instance_type"]
        return result


class OciPauseInstancesMethod(AbstractInstancesMethod):

    def __init__(self, base_command):
        super(OciPauseInstancesMethod, self).__init__(base_command, "pause")

    def add_extra_args(self):
        super(OciPauseInstancesMethod, self).add_extra_args()
        self.parser.add_argument(
            "--node_ip", default=None,
            help="The IP of the instance to pause."
        )

    def callback(self, args):
        host_info = self.cloud.get_host_info(args)
        if host_info:
            self.cloud.stop_instance(host_info)
        else:
            raise YBOpsRuntimeError("Instance {} not found".format(args.search_pattern))


class OciResumeInstancesMethod(AbstractInstancesMethod):

    def __init__(self, base_command):
        super(OciResumeInstancesMethod, self).__init__(base_command, "resume")

    def add_extra_args(self):
        super(OciResumeInstancesMethod, self).add_extra_args()
        self.parser.add_argument(
            "--node_ip", default=None,
            help="The IP of the instance to resume."
        )

    def callback(self, args):
        self.update_extra_vars_with_args(args)
        # Use get_all=True to find instances in any state (including STOPPED)
        host_info = self.cloud.get_host_info(args, get_all=True)
        if host_info:
            # get_all=True returns a list, get the first matching instance
            if isinstance(host_info, list):
                host_info = host_info[0] if host_info else None
            if host_info:
                server_ports = self.get_server_ports_to_check(args)
                self.cloud.start_instance(host_info, server_ports)
                return
        raise YBOpsRuntimeError("Instance {} not found".format(args.search_pattern))


class OciUpdateDiskMethod(UpdateDiskMethod):

    def __init__(self, base_command):
        super(OciUpdateDiskMethod, self).__init__(base_command)


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


class OciHardRebootInstancesMethod(HardRebootInstancesMethod):

    def __init__(self, base_command):
        super(OciHardRebootInstancesMethod, self).__init__(base_command)
        self.valid_states = ('RUNNING', 'STOPPING', 'STOPPED', 'STARTING')
        self.valid_stoppable_states = ('RUNNING', 'STOPPING')
