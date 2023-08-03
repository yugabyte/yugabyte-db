#!/usr/bin/env python
#
# Copyright 2019 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

from ybops.cloud.common.method import ListInstancesMethod, CreateInstancesMethod, \
    ProvisionInstancesMethod, DestroyInstancesMethod, AbstractMethod, \
    AbstractAccessMethod, AbstractNetworkMethod, AbstractInstancesMethod, AccessDeleteKeyMethod, \
    CreateRootVolumesMethod, ReplaceRootVolumeMethod, ChangeInstanceTypeMethod, \
    UpdateMountedDisksMethod, ConsoleLoggingErrorHandler, DeleteRootVolumesMethod, \
    UpdateDiskMethod, HardRebootInstancesMethod
from ybops.common.exceptions import YBOpsRuntimeError, get_exception_message
from ybops.cloud.aws.utils import get_yb_sg_name, create_dns_record_set, edit_dns_record_set, \
    delete_dns_record_set, list_dns_record_set, get_root_label
from ybops.utils.ssh import DEFAULT_SSH_PORT
import json
import os
import logging


class AwsReplaceRootVolumeMethod(ReplaceRootVolumeMethod):
    def __init__(self, base_command):
        super(AwsReplaceRootVolumeMethod, self).__init__(base_command)

    def add_extra_args(self):
        super(AwsReplaceRootVolumeMethod, self).add_extra_args()
        self.parser.add_argument("--root_device_name",
                                 required=True,
                                 help="The path where to attach the root device.")

    def _replace_root_volume(self, args, host_info, current_root_volume):
        # update specifically for AWS
        host_info["root_device_name"] = args.root_device_name if args.root_device_name else None
        return super(AwsReplaceRootVolumeMethod, self)._replace_root_volume(
            args, host_info, current_root_volume)

    def _mount_root_volume(self, host_info, volume):
        self.cloud.mount_disk(host_info, volume, host_info["root_device_name"])

    def _host_info_with_current_root_volume(self, args, host_info):
        return (host_info, host_info.get("root_volume"))


class AwsListInstancesMethod(ListInstancesMethod):
    """Subclass for listing instances in AWS. Currently doesn't provide any extra functionality.
    """

    def __init__(self, base_command):
        super(AwsListInstancesMethod, self).__init__(base_command)


class AwsCreateInstancesMethod(CreateInstancesMethod):
    """Subclass for creating instances in AWS. This is responsible for taking in the AWS specific
    flags, such as VPCs, AMIs and more.
    """

    def __init__(self, base_command):
        super(AwsCreateInstancesMethod, self).__init__(base_command)

    def add_extra_args(self):
        """Setup the CLI options for creating instances.
        """
        super(AwsCreateInstancesMethod, self).add_extra_args()
        self.parser.add_argument("--key_pair_name", default=os.environ.get("YB_EC2_KEY_PAIR_NAME"),
                                 help="AWS Key Pair name")
        self.parser.add_argument("--security_group_id", default=None,
                                 help="AWS comma delimited security group IDs.")
        self.parser.add_argument("--volume_type", choices=["gp3", "gp2", "io1"], default="gp2",
                                 help="Volume type for volumes on EBS-backed instances.")
        self.parser.add_argument("--spot_price", default=None,
                                 help="Spot price for each instance (if desired)")
        self.parser.add_argument("--cmk_res_name", help="CMK arn to enable encrypted EBS volumes.")
        self.parser.add_argument("--iam_profile_arn", help="ARN string for IAM instance profile")
        self.parser.add_argument("--disk_iops", type=int, default=1000,
                                 help="desired iops for aws v4 instance volumes")
        self.parser.add_argument("--disk_throughput", type=int, default=125,
                                 help="desired throughput for aws gp3 instance volumes")

    def preprocess_args(self, args):
        super(AwsCreateInstancesMethod, self).preprocess_args(args)
        if args.region is None:
            raise YBOpsRuntimeError("Must specify a region!")
        # TODO: better handling of this...
        if args.machine_image is None:
            # Update the machine_image with the ami_id for this version.
            args.machine_image = self.cloud.get_image(region=args.region).get(args.region)

    def callback(self, args):
        # These are to be used in the provision part for now.
        self.extra_vars.update({
            "aws_key_pair_name": args.key_pair_name,
        })
        if args.security_group_id is not None:
            self.extra_vars.update({
                "aws_security_group_id": args.security_group_id
            })
        else:
            self.extra_vars.update({
                "aws_security_group": get_yb_sg_name(args.region)
            })
        if args.spot_price is not None:
            self.extra_vars.update({
                "aws_spot_price": args.spot_price
            })
        if args.instance_tags is not None:
            self.extra_vars.update({
                "instance_tags": args.instance_tags
            })

        super(AwsCreateInstancesMethod, self).callback(args)

    def run_ansible_create(self, args):
        self.cloud.create_instance(args)


class AwsProvisionInstancesMethod(ProvisionInstancesMethod):
    """Subclass for provisioning instances in AWS. Setups the proper Create method to point to the
    AWS specific one.
    """

    def __init__(self, base_command):
        super(AwsProvisionInstancesMethod, self).__init__(base_command)

    def add_extra_args(self):
        super(AwsProvisionInstancesMethod, self).add_extra_args()
        self.parser.add_argument("--key_pair_name", default=os.environ.get("YB_EC2_KEY_PAIR_NAME"),
                                 help="AWS Key Pair name")

    def update_ansible_vars_with_args(self, args):
        super(AwsProvisionInstancesMethod, self).update_ansible_vars_with_args(args)
        self.extra_vars["device_names"] = self.cloud.get_device_names(args)
        self.extra_vars["mount_points"] = self.cloud.get_mount_points_csv(args)
        self.extra_vars.update({"aws_key_pair_name": args.key_pair_name})


class AwsCreateRootVolumesMethod(CreateRootVolumesMethod):
    """Subclass for creating root volumes in AWS
    """
    def __init__(self, base_command):
        super(AwsCreateRootVolumesMethod, self).__init__(base_command)
        self.create_method = AwsCreateInstancesMethod(base_command)

    def create_master_volume(self, args):
        args.auto_delete_boot_disk = False
        args.num_volumes = 0

        self.create_method.run_ansible_create(args)
        host_info = self.cloud.get_host_info(args)

        self.delete_instance(args, host_info["id"])
        return host_info["root_volume"]

    def delete_instance(self, args, instance_id):
        self.cloud.delete_instance(args.region, instance_id, args.assign_static_public_ip)

    def add_extra_args(self):
        super(AwsCreateRootVolumesMethod, self).add_extra_args()
        self.parser.add_argument("--snapshot_creation_delay",
                                 required=False,
                                 default=15,
                                 help="Time in seconds to wait for snapshot creation per attempt.",
                                 type=int)
        self.parser.add_argument("--snapshot_creation_max_attempts",
                                 required=False,
                                 default=80,
                                 help="Max number of wait attempts to try for snapshot creation.",
                                 type=int)


class AwsDeleteRootVolumesMethod(DeleteRootVolumesMethod):
    """Subclass for deleting root volumes in AWS.
    """

    def __init__(self, base_command):
        super(AwsDeleteRootVolumesMethod, self).__init__(base_command)

    def delete_volumes(self, args):
        self.cloud.delete_volumes(args)


class AwsDestroyInstancesMethod(DestroyInstancesMethod):
    """Subclass for destroying an instance in AWS, we fetch the host info and update the extra_vars
    with necessary parameters
    """

    def __init__(self, base_command):
        super(AwsDestroyInstancesMethod, self).__init__(base_command)

    def callback(self, args):
        filters = [
            {
                "Name": "instance-state-name",
                "Values": ["stopped", "running"]
            }
        ]
        host_info = self.cloud.get_host_info_specific_args(
            args.region,
            args.search_pattern,
            get_all=False,
            private_ip=args.node_ip,
            filters=filters
        )
        if not host_info:
            logging.error("Host {} does not exist.".format(args.search_pattern))
            return

        self.cloud.delete_instance(args.region, host_info['id'],
                                   args.delete_static_public_ip)


class AwsPauseInstancesMethod(AbstractInstancesMethod):
    """
    Subclass for stopping an instance in AWS, we fetch the host info
    and call the stop_instance method.
    """

    def __init__(self, base_command):
        super(AwsPauseInstancesMethod, self).__init__(base_command, "pause")

    def add_extra_args(self):
        super(AwsPauseInstancesMethod, self).add_extra_args()
        self.parser.add_argument("--node_ip", default=None,
                                 help="The ip of the instance to pause.")

    def callback(self, args):
        host_info = self.cloud.get_host_info_specific_args(
            args.region,
            args.search_pattern,
            get_all=False,
            private_ip=args.node_ip
        )

        if not host_info:
            logging.error("Host {} does not exist.".format(args.search_pattern))
            return

        self.cloud.stop_instance(host_info)


class AwsResumeInstancesMethod(AbstractInstancesMethod):
    """
    Subclass for resuming an instance in AWS, we fetch the host info
    and call the start_instance method.
    """

    def __init__(self, base_command):
        super(AwsResumeInstancesMethod, self).__init__(base_command, "resume")

    def add_extra_args(self):
        super(AwsResumeInstancesMethod, self).add_extra_args()
        self.parser.add_argument("--node_ip", default=None,
                                 help="The ip of the instance to resume.")

    def callback(self, args):
        host_info = self.cloud.get_host_info_specific_args(
            args.region,
            args.search_pattern,
            get_all=False,
            private_ip=args.node_ip
        )

        if not host_info:
            logging.error("Host {} does not exist.".format(args.search_pattern))
            return

        if host_info["instance_state"] != "stopped":
            logging.warning(f"Expected instance {args.search_pattern} to be stopped, "
                            f"got {host_info['instance_state']}")
        self.update_ansible_vars_with_args(args)
        server_ports = self.get_server_ports_to_check(args)
        self.cloud.start_instance(host_info, server_ports)


class AwsHardRebootInstancesMethod(HardRebootInstancesMethod):
    def __init__(self, base_command):
        super(AwsHardRebootInstancesMethod, self).__init__(base_command)
        self.valid_states = ('running', 'stopping', 'stopped', 'pending')
        self.valid_stoppable_states = ('running', 'stopping')


class AwsTagsMethod(AbstractInstancesMethod):
    def __init__(self, base_command):
        super(AwsTagsMethod, self).__init__(base_command, "tags")

    def add_extra_args(self):
        super(AwsTagsMethod, self).add_extra_args()
        self.parser.add_argument("--remove_tags", required=False,
                                 help="Tag keys to remove.")

    def callback(self, args):
        self.cloud.modify_tags(args)


class AwsAccessAddKeyMethod(AbstractAccessMethod):
    def __init__(self, base_command):
        super(AwsAccessAddKeyMethod, self).__init__(base_command, "add-key")

    def callback(self, args):
        (private_key_file, public_key_file) = self.validate_key_files(args)
        delete_remote = False
        if not args.skip_add_keypair_aws:
            delete_remote = self.cloud.add_key_pair(args)
        print(json.dumps({"private_key": private_key_file,
                          "public_key": public_key_file,
                          "delete_remote": delete_remote}))


class AwsAccessDeleteKeyMethod(AccessDeleteKeyMethod):
    def __init__(self, base_command):
        super(AwsAccessDeleteKeyMethod, self).__init__(base_command)

    def _delete_key_pair(self, args):
        self.cloud.delete_key_pair(args)


class AwsAccessListKeysMethod(AbstractMethod):
    def __init__(self, base_command):
        super(AwsAccessListKeysMethod, self).__init__(base_command, "list-keys")

    def add_extra_args(self):
        """Setup the CLI options for List Key Pair.
        """
        super(AwsAccessListKeysMethod, self).add_extra_args()
        self.parser.add_argument("--key_pair_name", required=False, default=None,
                                 help="AWS Key Pair name")

    def callback(self, args):
        print(json.dumps(self.cloud.list_key_pair(args)))


class AwsQueryRegionsMethod(AbstractMethod):
    def __init__(self, base_command):
        super(AwsQueryRegionsMethod, self).__init__(base_command, "regions")

    def callback(self, args):
        print(json.dumps(self.cloud.get_regions()))


class AwsQueryZonesMethod(AbstractMethod):
    def __init__(self, base_command):
        super(AwsQueryZonesMethod, self).__init__(base_command, "zones")

    def add_extra_args(self):
        super(AwsQueryZonesMethod, self).add_extra_args()
        self.parser.add_argument("--dest_vpc_id", required=False, help="Destination VPC Id. " +
                                 "Do not specify if you want us to create a new one.")

    def preprocess_args(self, args):
        super(AwsQueryZonesMethod, self).preprocess_args(args)
        if args.dest_vpc_id and not args.region:
            raise YBOpsRuntimeError("Using --dest_vpc_id requires --region")

    def callback(self, args):
        print(json.dumps(self.cloud.get_zones(args)))


class AwsQueryVPCMethod(AbstractMethod):
    def __init__(self, base_command):
        super(AwsQueryVPCMethod, self).__init__(base_command, "vpc")

    def callback(self, args):
        print(json.dumps(self.cloud.query_vpc(args)))


class AwsQueryCurrentHostMethod(AbstractMethod):
    VALID_METADATA_TYPES = ("vpc-id", "subnet-id", "instance-id", "mac",
                            "region", "privateIp", "security-groups", "role")

    def __init__(self, base_command):
        super(AwsQueryCurrentHostMethod, self).__init__(base_command, "current-host")
        # We do not need cloud credentials to query metadata.
        self.need_validation = False

    def add_extra_args(self):
        super(AwsQueryCurrentHostMethod, self).add_extra_args()
        self.parser.add_argument("--metadata_types", nargs="+", type=str, required=True,
                                 choices=self.VALID_METADATA_TYPES)

    def callback(self, args):
        try:
            print(json.dumps(self.cloud.get_current_host_info(args)))
        except YBOpsRuntimeError as ye:
            print(json.dumps(get_exception_message(ye)))


class AwsQueryPricingMethod(AbstractMethod):
    def __init__(self, base_command):
        super(AwsQueryPricingMethod, self).__init__(base_command, "pricing")

    def callback(self, args):
        raise YBOpsRuntimeError("Not Implemented")


class AwsQuerySpotPricingMethod(AbstractMethod):
    def __init__(self, base_command):
        super(AwsQuerySpotPricingMethod, self).__init__(base_command, "spot-pricing")

    def add_extra_args(self):
        super(AwsQuerySpotPricingMethod, self).add_extra_args()
        self.parser.add_argument("--instance_type", required=True,
                                 help="The instance type to get pricing info for")

    def callback(self, args):
        try:
            if args.region is None or args.zone is None:
                raise YBOpsRuntimeError("Must specify a region & zone to query spot price")
            print(json.dumps({'SpotPrice': self.cloud.get_spot_pricing(args)}))
        except YBOpsRuntimeError as ye:
            print(json.dumps({"error": get_exception_message(ye)}))


class AwsQueryImageMethod(AbstractMethod):
    def __init__(self, base_command):
        super(AwsQueryImageMethod, self).__init__(base_command, "image")
        self.error_handler = ConsoleLoggingErrorHandler(self.cloud)

    def add_extra_args(self):
        super(AwsQueryImageMethod, self).add_extra_args()
        self.parser.add_argument("--machine_image",
                                 required=True,
                                 help="The machine image (e.g. an AMI on AWS) to query")

    def callback(self, args):
        try:
            if args.region is None:
                raise YBOpsRuntimeError("Must specify a region to query image")
            print(json.dumps({"architecture": self.cloud.get_image_arch(args)}))
        except YBOpsRuntimeError as ye:
            print(json.dumps({"error": get_exception_message(ye)}))


class AwsNetworkBootstrapMethod(AbstractNetworkMethod):
    def __init__(self, base_command):
        super(AwsNetworkBootstrapMethod, self).__init__(base_command, "bootstrap")

    def add_extra_args(self):
        """Setup the CLI options network bootstrap."""
        super(AwsNetworkBootstrapMethod, self).add_extra_args()
        self.parser.add_argument("--custom_payload", required=False,
                                 help="JSON payload of per-region data.")

    def callback(self, args):
        try:
            print(json.dumps(self.cloud.network_bootstrap(args)))
        except YBOpsRuntimeError as ye:
            print(json.dumps({"error": get_exception_message(ye)}))


class AwsNetworkQueryMethod(AbstractNetworkMethod):
    def __init__(self, base_command):
        super(AwsNetworkQueryMethod, self).__init__(base_command, "query")

    def callback(self, args):
        try:
            print(json.dumps(self.cloud.query_vpc(args)))
        except YBOpsRuntimeError as ye:
            print(json.dumps({"error": get_exception_message(ye)}))


class AwsNetworkCleanupMethod(AbstractNetworkMethod):
    def __init__(self, base_command):
        super(AwsNetworkCleanupMethod, self).__init__(base_command, "cleanup")

    def add_extra_args(self):
        """Setup the CLI options network cleanup."""
        super(AwsNetworkCleanupMethod, self).add_extra_args()
        self.parser.add_argument("--custom_payload", required=False,
                                 help="JSON payload of per-region data.")

    def callback(self, args):
        try:
            print(json.dumps(self.cloud.network_cleanup(args)))
        except YBOpsRuntimeError as ye:
            print(json.dumps({"error": get_exception_message(ye)}))


class AbstractDnsMethod(AbstractMethod):
    def __init__(self, base_command, method_name):
        super(AbstractDnsMethod, self).__init__(base_command, method_name)
        self.ip_list = []
        self.naming_info_required = True

    def add_extra_args(self):
        super(AbstractDnsMethod, self).add_extra_args()
        self.parser.add_argument("--hosted_zone_id", required=True,
                                 help="The ID of the Route53 Hosted Zone.")
        self.parser.add_argument("--domain_name_prefix", required=self.naming_info_required,
                                 help="The prefix to create the RecordSet with, in your Zone.")
        self.parser.add_argument("--node_ips", required=self.naming_info_required,
                                 help="The CSV of the node IPs to associate to this DNS entry.")

    def preprocess_args(self, args):
        super(AbstractDnsMethod, self).preprocess_args(args)
        if args.node_ips:
            self.ip_list = args.node_ips.split(',')


class AwsCreateDnsEntryMethod(AbstractDnsMethod):
    def __init__(self, base_command):
        super(AwsCreateDnsEntryMethod, self).__init__(base_command, "create")

    def callback(self, args):
        create_dns_record_set(args.hosted_zone_id, args.domain_name_prefix, self.ip_list)


class AwsEditDnsEntryMethod(AbstractDnsMethod):
    def __init__(self, base_command):
        super(AwsEditDnsEntryMethod, self).__init__(base_command, "edit")

    def callback(self, args):
        edit_dns_record_set(args.hosted_zone_id, args.domain_name_prefix, self.ip_list)


class AwsDeleteDnsEntryMethod(AbstractDnsMethod):
    def __init__(self, base_command):
        super(AwsDeleteDnsEntryMethod, self).__init__(base_command, "delete")

    def callback(self, args):
        delete_dns_record_set(args.hosted_zone_id, args.domain_name_prefix, self.ip_list)


class AwsListDnsEntryMethod(AbstractDnsMethod):
    def __init__(self, base_command):
        super(AwsListDnsEntryMethod, self).__init__(base_command, "list")
        self.naming_info_required = False

    def callback(self, args):
        try:
            result = list_dns_record_set(args.hosted_zone_id)
            print(json.dumps({
                'name': result['HostedZone']['Name']
            }))
        except Exception as e:
            print(json.dumps({'error': repr(e)}))


class AwsChangeInstanceTypeMethod(ChangeInstanceTypeMethod):
    def __init__(self, base_command):
        super(AwsChangeInstanceTypeMethod, self).__init__(base_command)

    def _change_instance_type(self, args, host_info):
        self.cloud.change_instance_type(host_info, args.instance_type)

    # We have to use this to uniform accessing host_info for AWS and GCP
    def _host_info(self, args, host_info):
        return host_info


class AwsUpdateDiskMethod(UpdateDiskMethod):
    def __init__(self, base_command):
        super(AwsUpdateDiskMethod, self).__init__(base_command)

    def add_extra_args(self):
        super(AwsUpdateDiskMethod, self).add_extra_args()
        self.parser.add_argument("--disk_iops", type=int, default=None,
                                 help="Disk IOPS to provision on EBS-backed instances.")
        self.parser.add_argument("--disk_throughput", type=int, default=None,
                                 help="Disk throughput to provision on EBS-backed instances.")


class AwsUpdateMountedDisksMethod(UpdateMountedDisksMethod):
    def __init__(self, base_command):
        super(AwsUpdateMountedDisksMethod, self).__init__(base_command)

    def add_extra_args(self):
        super(AwsUpdateMountedDisksMethod, self).add_extra_args()
        self.parser.add_argument("--volume_type", choices=["gp3", "gp2", "io1"], default="gp2",
                                 help="Volume type for volumes on EBS-backed instances.")
