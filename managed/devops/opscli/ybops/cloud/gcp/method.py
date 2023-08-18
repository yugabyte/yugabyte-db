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

from ybops.cloud.common.method import (AbstractInstancesMethod, AbstractAccessMethod,
                                       AbstractMethod, UpdateMountedDisksMethod,
                                       ChangeInstanceTypeMethod, CreateInstancesMethod,
                                       CreateRootVolumesMethod, DestroyInstancesMethod,
                                       ProvisionInstancesMethod, ReplaceRootVolumeMethod,
                                       DeleteRootVolumesMethod, HardRebootInstancesMethod)
from ybops.cloud.gcp.utils import GCP_PERSISTENT, GCP_SCRATCH
from ybops.common.exceptions import YBOpsRuntimeError, get_exception_message
from ybops.utils.ssh import format_rsa_key, validated_key_file


class GcpReplaceRootVolumeMethod(ReplaceRootVolumeMethod):
    def __init__(self, base_command):
        super(GcpReplaceRootVolumeMethod, self).__init__(base_command)

    def _mount_root_volume(self, args, volume):
        self.cloud.mount_disk(args, {
            "boot": True,
            "source": volume
        })

    def _host_info_with_current_root_volume(self, args, host_info):
        args.private_ip = host_info["private_ip"]
        return (vars(args), host_info.get("root_volume_device_name"))


class GcpCreateInstancesMethod(CreateInstancesMethod):
    """Subclass for creating instances in GCP. This is responsible for taking in the GCP specific
    flags, such as instance types, subnets, etc.
    """
    SSH_USER = "centos"

    def __init__(self, base_command):
        super(GcpCreateInstancesMethod, self).__init__(base_command)

    def add_extra_args(self):
        super(GcpCreateInstancesMethod, self).add_extra_args()
        self.parser.add_argument("--volume_type", choices=[GCP_SCRATCH, GCP_PERSISTENT],
                                 default="scratch", help="Storage type for GCP instances.")
        self.parser.add_argument("--instance_template",
                                 help="Instance type template for GCP instances")

    def run_ansible_create(self, args):
        server_type = args.type

        can_ip_forward = (
            server_type.startswith('openvpn-server') or
            server_type.startswith('ipsec-gateway'))

        machine_image = args.machine_image if args.machine_image else \
            self.cloud.get_image(args.region)["selfLink"]

        ssh_keys = None
        if args.private_key_file is not None:
            rsa_key = validated_key_file(args.private_key_file)
            public_key = format_rsa_key(rsa_key, public_key=True)
            ssh_keys = "{}:{} {}".format(self.SSH_USER, public_key, self.SSH_USER)

        self.cloud.create_instance(args, server_type, can_ip_forward, machine_image, ssh_keys)


class GcpProvisionInstancesMethod(ProvisionInstancesMethod):
    """Subclass for provisioning instances in GCP. Sets up the proper Create method to point to the
    GCP specific one.
    """

    def __init__(self, base_command):
        super(GcpProvisionInstancesMethod, self).__init__(base_command)

    def add_extra_args(self):
        super(GcpProvisionInstancesMethod, self).add_extra_args()
        self.parser.add_argument("--volume_type", choices=[GCP_SCRATCH, GCP_PERSISTENT],
                                 default="scratch", help="Storage type for GCP instances.")

    def update_ansible_vars_with_args(self, args):
        super(GcpProvisionInstancesMethod, self).update_ansible_vars_with_args(args)
        self.extra_vars["device_names"] = self.cloud.get_device_names(args)
        self.extra_vars["mount_points"] = self.cloud.get_mount_points_csv(args)


class GcpCreateRootVolumesMethod(CreateRootVolumesMethod):
    """Subclass for creating root volumes in GCP.
    """
    def __init__(self, base_command):
        super(GcpCreateRootVolumesMethod, self).__init__(base_command)
        self.create_method = GcpCreateInstancesMethod(base_command)

    def create_master_volume(self, args):
        name = args.search_pattern[:63] if len(args.search_pattern) > 63 else args.search_pattern
        res = self.cloud.get_admin().create_disk(args.zone, args.instance_tags, body={
            "name": name,
            "sizeGb": args.boot_disk_size_gb,
            "sourceImage": args.machine_image})
        return res["targetLink"]

    # Not invoked. Just keeping it for consistency.
    def delete_instance(self, args):
        name = args.search_pattern[:63] if len(args.search_pattern) > 63 else args.search_pattern
        self.cloud.get_admin().delete_instance(
            args.region, args.zone, name, has_static_ip=args.assign_static_public_ip)


class GcpDeleteRootVolumesMethod(DeleteRootVolumesMethod):
    """Subclass for deleting root volumes in GCP.
    """
    def __init__(self, base_command):
        super(GcpDeleteRootVolumesMethod, self).__init__(base_command)

    def delete_volumes(self, args):
        self.cloud.delete_volumes(args)


class GcpDestroyInstancesMethod(DestroyInstancesMethod):
    """Subclass for deleting instances in GCP. Uses the API to delete instance bypassing Ansible.
    """

    def __init__(self, base_command):
        super(GcpDestroyInstancesMethod, self).__init__(base_command)

    def callback(self, args):
        filters = "((status = \"RUNNING\") OR (status = \"TERMINATED\"))"
        self.cloud.delete_instance(args, filters=filters)


class GcpQueryRegionsMethod(AbstractMethod):
    def __init__(self, base_command):
        super(GcpQueryRegionsMethod, self).__init__(base_command, "regions")

    def callback(self, args):
        print(json.dumps(self.cloud.get_regions(args)))


class GcpQueryVpcMethod(AbstractMethod):
    def __init__(self, base_command):
        super(GcpQueryVpcMethod, self).__init__(base_command, "vpc")

    def add_extra_args(self):
        super(GcpQueryVpcMethod, self).add_extra_args()
        self.parser.add_argument("--custom_payload", required=False,
                                 help="JSON payload of per-region data.")

    def callback(self, args):
        print(json.dumps(self.cloud.query_vpc(args)))


class GcpQueryZonesMethod(AbstractMethod):
    def __init__(self, base_command):
        super(GcpQueryZonesMethod, self).__init__(base_command, "zones")

    def add_extra_args(self):
        super(GcpQueryZonesMethod, self).add_extra_args()
        self.parser.add_argument(
            "--dest_vpc_id", default=None,
            help="Custom VPC to get zone and subnet info for.")
        self.parser.add_argument("--custom_payload", required=False,
                                 help="JSON payload of per-region data.")

    def callback(self, args):
        print(json.dumps(self.cloud.get_zones(args)))


class GcpQueryInstanceTypesMethod(AbstractMethod):
    def __init__(self, base_command):
        super(GcpQueryInstanceTypesMethod, self).__init__(base_command, "instance_types")

    def add_extra_args(self):
        super(GcpQueryInstanceTypesMethod, self).add_extra_args()
        self.parser.add_argument("--regions", nargs='+')
        self.parser.add_argument("--custom_payload", required=False,
                                 help="JSON payload of per-region data.")

    def callback(self, args):
        print(json.dumps(self.cloud.get_instance_types(args)))


class GcpQueryCurrentHostMethod(AbstractMethod):
    def __init__(self, base_command):
        super(GcpQueryCurrentHostMethod, self).__init__(base_command, "current-host")
        # We do not need cloud credentials to query metadata.
        self.need_validation = False

    def callback(self, args):
        print(json.dumps(self.cloud.get_current_host_info()))


class GcpQueryPreemptibleInstanceMethod(AbstractMethod):
    def __init__(self, base_command):
        super(GcpQueryPreemptibleInstanceMethod, self).__init__(base_command, "spot-pricing")

    def add_extra_args(self):
        super(GcpQueryPreemptibleInstanceMethod, self).add_extra_args()
        self.parser.add_argument("--instance_type", required=True,
                                 help="The instance type to get pricing info for")

    def callback(self, args):
        try:
            if args.region is None:
                raise YBOpsRuntimeError("Must specify a region to query spot price")
            print(json.dumps({'SpotPrice': self.cloud.get_spot_pricing(args)}))
        except YBOpsRuntimeError as ye:
            print(json.dumps({"error": get_exception_message(ye)}))


class GcpAccessAddKeyMethod(AbstractAccessMethod):
    def __init__(self, base_command):
        super(GcpAccessAddKeyMethod, self).__init__(base_command, "add-key")

    def callback(self, args):
        (private_key_file, public_key_file) = self.validate_key_files(args)
        print(json.dumps({"private_key": private_key_file, "public_key": public_key_file}))


class GcpAbstractNetworkMethod(AbstractMethod):
    def __init__(self, base_command, method_name):
        super(GcpAbstractNetworkMethod, self).__init__(base_command, method_name)

    def add_extra_args(self):
        super(GcpAbstractNetworkMethod, self).add_extra_args()
        self.parser.add_argument("--metadata_override", required=False,
                                 help="A custom YML metadata override file.")

    def preprocess_args(self, args):
        super(GcpAbstractNetworkMethod, self).preprocess_args(args)
        if args.metadata_override:
            self.cloud.update_metadata(args.metadata_override)


class GcpNetworkBootstrapMethod(GcpAbstractNetworkMethod):
    def __init__(self, base_command):
        super(GcpNetworkBootstrapMethod, self).__init__(base_command, "bootstrap")

    def add_extra_args(self):
        """Setup the CLI options network bootstrap."""
        super(GcpNetworkBootstrapMethod, self).add_extra_args()
        self.parser.add_argument("--custom_payload", required=False,
                                 help="JSON payload of per-region data.")

    def callback(self, args):
        try:
            print(json.dumps(self.cloud.network_bootstrap(args)))
        except YBOpsRuntimeError as ye:
            print(json.dumps({"error": get_exception_message(ye)}))


class GcpNetworkCleanupMethod(GcpAbstractNetworkMethod):
    def __init__(self, base_command):
        super(GcpNetworkCleanupMethod, self).__init__(base_command, "cleanup")

    def add_extra_args(self):
        """Setup the CLI options network cleanup."""
        super(GcpNetworkCleanupMethod, self).add_extra_args()
        self.parser.add_argument("--custom_payload", required=False,
                                 help="JSON payload of per-region data.")

    def callback(self, args):
        try:
            print(json.dumps(self.cloud.network_cleanup(args)))
        except YBOpsRuntimeError as ye:
            print(json.dumps({"error": get_exception_message(ye)}))


class GcpNetworkQueryMethod(GcpAbstractNetworkMethod):
    def __init__(self, base_command):
        super(GcpNetworkQueryMethod, self).__init__(base_command, "query")

    def add_extra_args(self):
        """Setup the CLI options network queries."""
        super(GcpNetworkQueryMethod, self).add_extra_args()
        self.parser.add_argument("--custom_payload", required=False,
                                 help="JSON payload of per-region data.")

    def callback(self, args):
        try:
            print(json.dumps(self.cloud.query_vpc(args)))
        except YBOpsRuntimeError as ye:
            print(json.dumps({"error": get_exception_message(ye)}))


class GcpChangeInstanceTypeMethod(ChangeInstanceTypeMethod):
    def __init__(self, base_command):
        super(GcpChangeInstanceTypeMethod, self).__init__(base_command)

    def _change_instance_type(self, args, host_info):
        self.cloud.change_instance_type(host_info, args.instance_type)

    def _host_info(self, args, host_info):
        args.private_ip = host_info["private_ip"]
        result = vars(args).copy()
        result['instance_type'] = host_info["instance_type"]
        return result


class GcpResumeInstancesMethod(AbstractInstancesMethod):
    def __init__(self, base_command):
        super(GcpResumeInstancesMethod, self).__init__(base_command,  "resume")

    def add_extra_args(self):
        super(GcpResumeInstancesMethod, self).add_extra_args()
        self.parser.add_argument("--node_ip", default=None,
                                 help="The ip of the instance to resume.")

    def callback(self, args):
        self.update_ansible_vars_with_args(args)
        if args.boot_script is not None:
            self.cloud.update_user_data(args)
        server_ports = self.get_server_ports_to_check(args)
        self.cloud.start_instance(vars(args), server_ports)


class GcpPauseInstancesMethod(AbstractInstancesMethod):
    def __init__(self, base_command):
        super(GcpPauseInstancesMethod, self).__init__(base_command, "pause")

    def add_extra_args(self):
        super(GcpPauseInstancesMethod, self).add_extra_args()
        self.parser.add_argument("--node_ip", default=None,
                                 help="The ip of the instance to pause.")

    def callback(self, args):
        self.cloud.stop_instance(vars(args))


class GcpHardRebootInstancesMethod(HardRebootInstancesMethod):
    def __init__(self, base_command):
        super(GcpHardRebootInstancesMethod, self).__init__(base_command)
        self.valid_states = ('RUNNING', 'STOPPING', 'TERMINATED', 'PROVISIONING', 'STAGING')
        self.valid_stoppable_states = ('RUNNING', 'STOPPING')


class GcpUpdateMountedDisksMethod(UpdateMountedDisksMethod):
    def __init__(self, base_command):
        super(GcpUpdateMountedDisksMethod, self).__init__(base_command)

    def add_extra_args(self):
        super(GcpUpdateMountedDisksMethod, self).add_extra_args()
        self.parser.add_argument("--volume_type", choices=[GCP_SCRATCH, GCP_PERSISTENT],
                                 default="scratch", help="Storage type for GCP instances.")


class GcpTagsMethod(AbstractInstancesMethod):
    def __init__(self, base_command):
        super(GcpTagsMethod, self).__init__(base_command, "tags")

    def add_extra_args(self):
        super(GcpTagsMethod, self).add_extra_args()
        self.parser.add_argument("--remove_tags", required=False,
                                 help="Tag keys to remove.")

    def callback(self, args):
        self.cloud.modify_tags(args)
