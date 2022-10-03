# Copyright 2020 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

from ybops.cloud.common.method import ListInstancesMethod, CreateInstancesMethod, \
    ProvisionInstancesMethod, DestroyInstancesMethod, AbstractMethod, \
    AbstractAccessMethod, AbstractNetworkMethod, AbstractInstancesMethod, \
    DestroyInstancesMethod, AbstractInstancesMethod, DeleteRootVolumesMethod
import logging
import json
import glob
import os


class AzureNetworkBootstrapMethod(AbstractNetworkMethod):
    def __init__(self, base_command):
        super(AzureNetworkBootstrapMethod, self).__init__(base_command, "bootstrap")

    def add_extra_args(self):
        super(AzureNetworkBootstrapMethod, self).add_extra_args()
        self.parser.add_argument("--custom_payload", required=False,
                                 help="JSON payload of per-region data")

    def callback(self, args):
        self.cloud.network_bootstrap(args)


class AzureNetworkCleanupMethod(AbstractNetworkMethod):
    def __init__(self, base_command):
        super(AzureNetworkCleanupMethod, self).__init__(base_command, "cleanup")

    def add_extra_args(self):
        super(AzureNetworkCleanupMethod, self).add_extra_args()
        self.parser.add_argument("--custom_payload", required=False,
                                 help="JSON payload of per-region data")

    def callback(self, args):
        self.cloud.network_cleanup(args)


class AzureCreateInstancesMethod(CreateInstancesMethod):
    def __init__(self, base_command):
        super(AzureCreateInstancesMethod, self).__init__(base_command)

    def add_extra_args(self):
        super(AzureCreateInstancesMethod, self).add_extra_args()
        self.parser.add_argument("--volume_type",
                                 choices=["premium_lrs", "standardssd_lrs", "ultrassd_lrs"],
                                 default="premium_lrs", help="Volume type for Azure instances.")
        self.parser.add_argument("--security_group_id", default=None,
                                 help="Azure comma delimited security group IDs.")
        self.parser.add_argument("--vpcId", required=False,
                                 help="name of the virtual network associated with the subnet")
        self.parser.add_argument("--disk_iops", type=int, default=None,
                                 help="Desired iops for ultrassd instance volumes.")
        self.parser.add_argument("--disk_throughput", type=int, default=None,
                                 help="Desired throughput for ultrassd instance volumes.")

    def preprocess_args(self, args):
        super(AzureCreateInstancesMethod, self).preprocess_args(args)

    def callback(self, args):
        super(AzureCreateInstancesMethod, self).callback(args)

    def run_ansible_create(self, args):
        return self.cloud.create_or_update_instance(args, self.extra_vars["ssh_user"])


class AzureProvisionInstancesMethod(ProvisionInstancesMethod):
    def __init__(self, base_command):
        super(AzureProvisionInstancesMethod, self).__init__(base_command)

    def add_extra_args(self):
        super(AzureProvisionInstancesMethod, self).add_extra_args()

    def callback(self, args):
        super(AzureProvisionInstancesMethod, self).callback(args)

    def update_ansible_vars_with_args(self, args):
        super(AzureProvisionInstancesMethod, self).update_ansible_vars_with_args(args)
        self.extra_vars["device_names"] = self.cloud.get_device_names(args)
        self.extra_vars["mount_points"] = self.cloud.get_mount_points_csv(args)


class AzureDestroyInstancesMethod(DestroyInstancesMethod):
    def __init__(self, base_command):
        super(AzureDestroyInstancesMethod, self).__init__(base_command)

    def callback(self, args):
        self.cloud.destroy_instance(args)


class AzureAccessAddKeyMethod(AbstractAccessMethod):
    def __init__(self, base_command):
        super(AzureAccessAddKeyMethod, self).__init__(base_command, "add-key")

    def callback(self, args):
        (private_key_file, public_key_file) = self.validate_key_files(args)
        print(json.dumps({"private_key": private_key_file, "public_key": public_key_file}))


class AzureQueryVPCMethod(AbstractMethod):
    def __init__(self, base_command):
        super(AzureQueryVPCMethod, self).__init__(base_command, "vpc")

    def callback(self, args):
        print(json.dumps(self.cloud.query_vpc(args)))


class AzureQueryRegionsMethod(AbstractMethod):
    def __init__(self, base_command):
        super(AzureQueryRegionsMethod, self).__init__(base_command, "region")

    def callback(self, args):
        logging.debug("Querying Region!")


class AzureQueryInstanceTypesMethod(AbstractMethod):
    def __init__(self, base_command):
        super(AzureQueryInstanceTypesMethod, self).__init__(base_command, "instance_types")

    def add_extra_args(self):
        super(AzureQueryInstanceTypesMethod, self).add_extra_args()
        self.parser.add_argument("--regions", nargs='+')
        self.parser.add_argument("--custom_payload", required=False,
                                 help="JSON payload of per-region data.")

    def callback(self, args):
        print(json.dumps(self.cloud.get_instance_types(args)))


class AzureQueryZonesMethod(AbstractMethod):
    def __init__(self, base_command):
        super(AzureQueryZonesMethod, self).__init__(base_command, "zones")

    def add_extra_args(self):
        super(AzureQueryZonesMethod, self).add_extra_args()
        self.parser.add_argument(
            "--dest_vpc_id", default=None,
            help="Custom VPC to get zone and subnet info for.")
        self.parser.add_argument("--custom_payload", required=False,
                                 help="JSON payload of per-region data.")

    def callback(self, args):
        print(json.dumps(self.cloud.get_zones(args)))
        # print(json.dumps({"westus2": {"1": "default", "2": "default", "3": "default"}}))


class AzureQueryVnetMethod(AbstractMethod):
    def __init__(self, base_command):
        super(AzureQueryVnetMethod, self).__init__(base_command, "vnet")

    def callback(self, args):
        print(json.dumps(self.cloud.get_default_vnet(args)))


class AzureQueryUltraMethod(AbstractMethod):
    """
    This method writes the instance types to a file that support ultra disks.
    If no regions are provided, the metadata file is read. All regions are
    written to their own unique file inside the folder path given.
    """
    def __init__(self, base_command):
        super(AzureQueryUltraMethod, self).__init__(base_command, "ultra")

    def callback(self, args):
        self.cloud.get_ultra_instances(args)

    def add_extra_args(self):
        super(AzureQueryUltraMethod, self).add_extra_args()
        self.parser.add_argument("--regions", nargs='+')
        self.parser.add_argument("--folder", required=True,
                                 help="Folder to write region ultra disk json.")


class AbstractDnsMethod(AbstractMethod):
    def __init__(self, base_command, method_name):
        super(AbstractDnsMethod, self).__init__(base_command, method_name)
        self.ip_list = []
        self.naming_info_required = True

    def add_extra_args(self):
        super(AbstractDnsMethod, self).add_extra_args()
        self.parser.add_argument("--hosted_zone_id", required=True,
                                 help="The ID of the Azure private DNS zone.")
        self.parser.add_argument("--domain_name_prefix", required=self.naming_info_required,
                                 help="The prefix to create the RecordSet with, in your Zone.")
        self.parser.add_argument("--node_ips", required=self.naming_info_required,
                                 help="The CSV of the node IPs to associate to this DNS entry.")

    def preprocess_args(self, args):
        super(AbstractDnsMethod, self).preprocess_args(args)
        if args.node_ips:
            self.ip_list = args.node_ips.split(',')


class AzureCreateDnsEntryMethod(AbstractDnsMethod):
    def __init__(self, base_command):
        super(AzureCreateDnsEntryMethod, self).__init__(base_command, "create")

    def callback(self, args):
        self.cloud.create_dns_record_set(
            args.hosted_zone_id, args.domain_name_prefix, self.ip_list)


class AzureEditDnsEntryMethod(AbstractDnsMethod):
    def __init__(self, base_command):
        super(AzureEditDnsEntryMethod, self).__init__(base_command, "edit")

    def callback(self, args):
        self.cloud.edit_dns_record_set(
            args.hosted_zone_id, args.domain_name_prefix, self.ip_list)


class AzureDeleteDnsEntryMethod(AbstractDnsMethod):
    def __init__(self, base_command):
        super(AzureDeleteDnsEntryMethod, self).__init__(base_command, "delete")

    def callback(self, args):
        self.cloud.delete_dns_record_set(args.hosted_zone_id, args.domain_name_prefix)


class AzureListDnsEntryMethod(AbstractDnsMethod):
    def __init__(self, base_command):
        super(AzureListDnsEntryMethod, self).__init__(base_command, "list")
        self.naming_info_required = False

    def callback(self, args):
        try:
            result = self.cloud.list_dns_record_set(args.hosted_zone_id)
            print(json.dumps({
                'name': result.name
            }))
        except Exception as e:
            print(json.dumps({'error': repr(e)}))


class AzureTagsMethod(AbstractInstancesMethod):
    def __init__(self, base_command):
        super(AzureTagsMethod, self).__init__(base_command, "tags")

    def add_extra_args(self):
        super(AzureTagsMethod, self).add_extra_args()
        self.parser.add_argument("--remove_tags", required=False,
                                 help="Tag keys to remove. Noop for Azure.")

    def callback(self, args):
        self.cloud.modify_tags(args)


class AzureDeleteRootVolumesMethod(DeleteRootVolumesMethod):
    def __init__(self, base_command):
        super(AzureDeleteRootVolumesMethod, self).__init__(base_command)

    def delete_volumes(self, args):
        pass


class AzurePauseInstancesMethod(AbstractInstancesMethod):
    def __init__(self, base_command):
        super(AzurePauseInstancesMethod, self).__init__(base_command, "pause")

    def add_extra_args(self):
        super(AzurePauseInstancesMethod, self).add_extra_args()
        self.parser.add_argument("--node_ip", default=None,
                                 help="The ip of the instance to pause.")

    def callback(self, args):
        self.cloud.stop_instance(args)


class AzureResumeInstancesMethod(AbstractInstancesMethod):
    def __init__(self, base_command):
        super(AzureResumeInstancesMethod, self).__init__(base_command,  "resume")

    def add_extra_args(self):
        super(AzureResumeInstancesMethod, self).add_extra_args()
        self.parser.add_argument("--node_ip", default=None,
                                 help="The ip of the instance to resume.")

    def callback(self, args):
        self.cloud.start_instance(args, [args.custom_ssh_port])
