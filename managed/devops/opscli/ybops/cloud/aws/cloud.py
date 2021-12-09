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
import socket

import boto3
from botocore.exceptions import ClientError
from botocore.utils import InstanceMetadataFetcher
from six.moves.urllib.error import URLError
from six.moves.urllib.request import urlopen
from ybops.cloud.aws.command import (AwsAccessCommand, AwsDnsCommand, AwsInstanceCommand,
                                     AwsNetworkCommand, AwsQueryCommand)
from ybops.cloud.aws.utils import (ROOT_VOLUME_LABEL, AwsBootstrapClient, YbVpcComponents,
                                   change_instance_type, create_instance, delete_vpc, get_client,
                                   get_clients, get_device_names, get_spot_pricing,
                                   get_vpc_for_subnet, get_zones, has_ephemerals, modify_tags,
                                   query_vpc, update_disk)
from ybops.cloud.common.cloud import AbstractCloud
from ybops.common.exceptions import YBOpsRuntimeError
from ybops.utils import (format_rsa_key, is_valid_ip_address, validated_key_file)


class AwsCloud(AbstractCloud):
    """Subclass specific to AWS cloud related functionality.
    """
    INSTANCE_METADATA_API = "http://169.254.169.254/2016-09-02/meta-data/"
    INSTANCE_IDENTITY_API = "http://169.254.169.254/2016-09-02/dynamic/instance-identity/document"
    NETWORK_METADATA_API = os.path.join(INSTANCE_METADATA_API, "network/interfaces/macs/")
    METADATA_API_TIMEOUT_SECONDS = 3

    def __init__(self):
        super(AwsCloud, self).__init__("aws")
        self._wait_for_startup_script_command = \
            "until test -e /var/lib/cloud/instance/boot-finished ; do sleep 1 ; done"

    def add_subcommands(self):
        """Override to setup the cloud-specific instances of the subcommands.
        """
        self.add_subcommand(AwsInstanceCommand())
        self.add_subcommand(AwsNetworkCommand())
        self.add_subcommand(AwsAccessCommand())
        self.add_subcommand(AwsQueryCommand())
        self.add_subcommand(AwsDnsCommand())

    def has_machine_credentials(self):
        """
        Override for superclass method to detect if current instance has cloud access credentials.
        """
        return self.get_instance_metadata("role") is not None

    def get_vpc_for_subnet(self, region, subnet):
        return get_vpc_for_subnet(get_client(region), subnet)

    def get_image(self, region=None):
        regions = [region] if region is not None else self.get_regions()
        output = {}
        for r in regions:
            output[r] = self.metadata["regions"][r]["image"]
        return output

    def get_spot_pricing(self, args):
        return get_spot_pricing(args.region, args.zone, args.instance_type)

    def get_regions(self):
        return list(self.metadata.get("regions", {}).keys())

    def _get_all_regions_or_arg(self, region=None):
        return [region] if region is not None else self.get_regions()

    def get_zones(self, args):
        output = {}
        for r in self._get_all_regions_or_arg(args.region):
            output[r] = get_zones(r, args.dest_vpc_id)
        return output

    def _get_clients(self, region=None):
        return get_clients(self._get_all_regions_or_arg(region))

    def get_vpcs(self, args):
        result = {}
        for region, client in self._get_clients(args.region).items():
            result[region] = {}
            for vpc in client.vpcs.all():
                result[region][vpc.id] = {}
                subnets = {}
                for subnet in vpc.subnets.all():
                    subnets.setdefault(subnet.availability_zone, []).append(subnet.id)
                result[region][vpc.id]["zones"] = subnets
        return result

    def list_key_pair(self, args):
        key_pair_name = args.key_pair_name if args.key_pair_name else '*'
        filters = [{'Name': 'key-name', 'Values': [key_pair_name]}]
        result = {}
        for region, client in self._get_clients(args.region).items():
            result[region] = [keyInfo.name for keyInfo in client.key_pairs.filter(Filters=filters)]
        return result

    def delete_key_pair(self, args):
        for region, client in self._get_clients(args.region).items():
            client.KeyPair(args.key_pair_name).delete()

    def add_key_pair(self, args):
        key_pair_name = args.key_pair_name
        # If we were provided with a private key file, we use that to generate the public
        # key using RSA. If not we will use the public key file (assuming the private key exists).
        key_file = args.private_key_file if args.private_key_file else args.public_key_file
        key_file_path = os.path.join(args.key_file_path, key_file)

        # Make sure the key pair name doesn't exists already.
        # TODO: may be add extra validation to see if the key exists in specific region
        # if it doesn't exists in a region add them?. But only after validating the existing
        # is the same key in other regions.
        result = list(self.list_key_pair(args).values())[0]
        if len(result) > 0:
            raise YBOpsRuntimeError("KeyPair already exists {}".format(key_pair_name))

        if not os.path.exists(key_file_path):
            raise YBOpsRuntimeError("Key: {} file not found".format(key_file_path))

        # This call would throw a exception if the file is not valid key file.
        rsa_key = validated_key_file(key_file_path)

        result = {}
        for region, client in self._get_clients(args.region).items():
            result[region] = client.import_key_pair(
                KeyName=key_pair_name,
                PublicKeyMaterial=format_rsa_key(rsa_key, public_key=True)
            )
        return result

    def _subset_region_data(self, per_region_meta):
        metadata_subset = {k: v for k, v in self.metadata["regions"].items()
                           if k in per_region_meta}
        if len(metadata_subset) != len(per_region_meta):
            raise YBOpsRuntimeError("Asked to bootstrap/cleanup {}, only know of {}".format(
                list(per_region_meta.keys()), list(self.metadata["regions"].keys())))
        return metadata_subset

    def network_cleanup(self, args):
        # Generate region subset, based on passed in JSON.
        custom_payload = json.loads(args.custom_payload)
        host_vpc_id = custom_payload.get("hostVpcId")
        host_vpc_region = custom_payload.get("hostVpcRegion")
        if (host_vpc_id is None) ^ (host_vpc_region is None):
            raise YBOpsRuntimeError("Must have none or both of host_vpc_id and host_vpc_region.")
        per_region_meta = custom_payload.get("perRegionMetadata")
        metadata_subset = self._subset_region_data(per_region_meta)
        output = {}
        for region in metadata_subset:
            output[region] = delete_vpc(region, host_vpc_id, host_vpc_region)
        return output

    def network_bootstrap(self, args):
        result = {}
        # Generate region subset, based on passed in JSON.
        custom_payload = json.loads(args.custom_payload)
        per_region_meta = custom_payload.get("perRegionMetadata")
        metadata_subset = self._subset_region_data(per_region_meta)
        # Override region CIDR info, if any.
        for k in metadata_subset:
            custom_cidr = per_region_meta[k].get("vpcCidr")
            if custom_cidr is not None:
                cidr_pieces = custom_cidr.split(".")
                if len(cidr_pieces) != 4:
                    raise YBOpsRuntimeError(
                        "Invalid CIDR description {} in {}", custom_cidr, per_region_meta[k])
                metadata_subset[k]["cidr_prefix"] = "{}.{}".format(cidr_pieces[0], cidr_pieces[1])
        # Overwrite metadata object if overrides were given.
        self.metadata["regions"] = metadata_subset
        client = AwsBootstrapClient(
            self.metadata, custom_payload.get("hostVpcId"), custom_payload.get("hostVpcRegion"))
        # TODO(bogdan): this needs to be refactored into individual calls to per-region bootstrap,
        # per-item creation and x-region connectivity/glue.
        #
        # For now, let's leave it as a top-level knob to "do everything" vs "do nothing"...
        user_provided_vpc_ids = 0
        for r in per_region_meta.values():
            if r.get("vpcId") is not None:
                user_provided_vpc_ids += 1
        if user_provided_vpc_ids > 0 and user_provided_vpc_ids != len(per_region_meta):
            raise YBOpsRuntimeError("Either no regions or all regions must have vpcId specified.")

        components = {}
        if user_provided_vpc_ids > 0:
            for region in metadata_subset:
                components[region] = YbVpcComponents.from_user_json(
                    region, per_region_meta.get(region))
        else:
            # Bootstrap the individual region items standalone (vpc, subnet, sg, RT, etc).
            for region in metadata_subset:
                components[region] = client.bootstrap_individual_region(region)
            # Cross link all the regions together.
            client.cross_link_regions(components)
        return {region: c.as_json() for region, c in components.items()}

    def query_vpc(self, args):
        result = {}
        for region in self._get_all_regions_or_arg(args.region):
            result[region] = query_vpc(region)
            result[region]["default_image"] = self.get_image(region).get(region)
        return result

    def get_current_host_info(self, args):
        """This method would fetch current host information by calling AWS metadata api
        to fetch requested metatdata's.
        """
        try:
            metadata = {}
            for metadata_type in args.metadata_types:
                # Since sometime metadata might have multiple values separated by \n, we would
                # replace it with comma instead.
                metadata[metadata_type] = \
                    self.get_instance_metadata(metadata_type).replace("\n", ",")
            return metadata
        except (URLError, socket.timeout):
            raise YBOpsRuntimeError("Unable to auto-discover AWS provider information")

    def get_instance_metadata(self, metadata_type):
        """This method fetches instance metadata using AWS metadata api
        Args:
            metadata_type (str): Metadata to fetch
        Returns:
            metadata (str): metadata information for the requested metadata key or
            raises a runtime exception.
        """
        if metadata_type in ["mac", "instance-id", "security-groups"]:
            return urlopen(os.path.join(self.INSTANCE_METADATA_API, metadata_type),
                           timeout=self.METADATA_API_TIMEOUT_SECONDS).read().decode('utf-8')
        elif metadata_type in ["vpc-id", "subnet-id"]:
            mac = self.get_instance_metadata("mac")
            return urlopen(os.path.join(self.NETWORK_METADATA_API, mac, metadata_type),
                           timeout=self.METADATA_API_TIMEOUT_SECONDS).read().decode('utf-8')
        elif metadata_type in ["region", "privateIp"]:
            identity_data = urlopen(self.INSTANCE_IDENTITY_API,
                                    timeout=self.METADATA_API_TIMEOUT_SECONDS) \
                .read().decode('utf-8')
            return json.loads(identity_data).get(metadata_type) if identity_data else None
        elif metadata_type in ["role"]:
            # Arg timeout is in MS.
            fetcher = InstanceMetadataFetcher(
                timeout=self.METADATA_API_TIMEOUT_SECONDS, num_attempts=2)
            c = fetcher.retrieve_iam_role_credentials()
            # This will return None in case of no assigned role on the instance.
            return c.get("role_name")
        else:
            raise YBOpsRuntimeError("Unsupported metadata type: {}".format(metadata_type))

    def get_first_subnet_per_region(self, region=None):
        """Method used to get a subnet for the given region or one for each regions.

        Required fields in args:
          region: the AWS region to query for, or None if to try all regions
        """
        # TODO: Sometimes this might cause AWS rate limiting to hit. For those cases, we should
        # probably catch the exceptions and default to the self.metadata["base_subnets"] for the
        # mapping.
        regions = [region] if region is not None else self.get_regions()
        search_pattern = "*-vpn"
        subnet_per_region = {}
        for r in regions:
            host_info = self.get_host_info_specific_args(r, search_pattern)
            if not host_info:
                continue
            subnet_per_region[r] = host_info["subnet"]
        return subnet_per_region

    def get_host_info(self, args, get_all=False, private_ip=None):
        """Override to call the respective AWS specific API for returning hosts by name.

        Required fields in args:
          region: the AWS region to search in
          search_pattern: the regex or direct name to search hosts by
        """
        region = args.region
        search_pattern = args.search_pattern
        return self.get_host_info_specific_args(region, search_pattern, get_all, private_ip)

    def get_host_info_specific_args(self, region, search_pattern, get_all=False,
                                    private_ip=None, filters=None):
        if not filters:
            filters = [
                {
                    "Name": "instance-state-name",
                    "Values": ["running"]
                }
            ]

        # If no argument passed, assume full scan.
        if is_valid_ip_address(search_pattern):
            filters.append({
                "Name": "private-ip-address",
                "Values": [search_pattern]
            })
        elif search_pattern:
            filters.append({
                "Name": "tag:Name",
                "Values": [search_pattern]
            })

        instances = []
        for _, client in self._get_clients(region=region).items():
            instances.extend(list(client.instances.filter(Filters=filters)))
        results = []
        for instance in instances:
            data = instance.meta.data
            if private_ip is not None and private_ip != data["PrivateIpAddress"]:
                logging.warn("Node name {} is not unique. Expected IP {}, got IP {}".format(
                    search_pattern, private_ip, data["PrivateIpAddress"]))
                continue
            zone = data["Placement"]["AvailabilityZone"]
            name_tags = None
            server_tags = None
            launched_by_tags = None
            if data.get("Tags") is not None:
                # Server Type is an optinal flag only for cluster servers.
                server_tags = [t["Value"] for t in data["Tags"] if t["Key"] == "yb-server-type"]
                name_tags = [t["Value"] for t in data["Tags"] if t["Key"] == "Name"]
                launched_by_tags = [t["Value"] for t in data["Tags"] if t["Key"] == "launched-by"]
                node_uuid_tags = [t["Value"] for t in data["Tags"] if t["Key"] == "node-uuid"]
                universe_uuid_tags = [t["Value"] for t in data["Tags"]
                                      if t["Key"] == "universe-uuid"]

            disks = data.get("BlockDeviceMappings")
            root_vol = next(disk for disk in disks if disk.get("DeviceName") == ROOT_VOLUME_LABEL)

            primary_private_ip = None
            secondary_private_ip = None
            primary_subnet = None
            secondary_subnet = None
            network_interfaces = data.get("NetworkInterfaces")
            for interface in network_interfaces:
                if interface.get("Attachment").get("DeviceIndex") == 0:
                    primary_private_ip = interface.get("PrivateIpAddress")
                    primary_subnet = interface.get("SubnetId")
                elif interface.get("Attachment").get("DeviceIndex") == 1:
                    secondary_private_ip = interface.get("PrivateIpAddress")
                    secondary_subnet = interface.get("SubnetId")

            result = dict(
                id=data.get("InstanceId", None),
                name=name_tags[0] if name_tags else None,
                public_ip=data.get("PublicIpAddress", None),
                private_ip=primary_private_ip,
                secondary_private_ip=secondary_private_ip,
                public_dns=data["PublicDnsName"],
                private_dns=data["PrivateDnsName"],
                launch_time=data["LaunchTime"].isoformat(),
                zone=zone,
                subnet=primary_subnet,
                secondary_subnet=secondary_subnet,
                region=region if region is not None else zone[:-1],
                instance_type=data["InstanceType"],
                server_type=server_tags[0] if server_tags else None,
                launched_by=launched_by_tags[0] if launched_by_tags else None,
                node_uuid=node_uuid_tags[0] if node_uuid_tags else None,
                universe_uuid=universe_uuid_tags[0] if universe_uuid_tags else None,
                vpc=data["VpcId"],
                root_volume=root_vol["Ebs"]["VolumeId"]
            )
            if not get_all:
                return result
            results.append(result)
        return results

    def get_device_names(self, args):
        if has_ephemerals(args.instance_type):
            return []
        else:
            return get_device_names(args.instance_type, args.num_volumes)

    def get_subnet_cidr(self, args, subnet_id):
        ec2 = boto3.resource('ec2', args.region)
        subnet = ec2.Subnet(subnet_id)
        return subnet.cidr_block

    def create_instance(self, args):
        # If we are configuring second NIC, ensure that this only happens for a
        # centOS AMI right now.
        if args.cloud_subnet_secondary:
            ec2 = boto3.resource('ec2', args.region)
            image = ec2.Image(args.machine_image)
            if 'centos' not in image.name.lower():
                raise YBOpsRuntimeError("Second NIC can only be configured for CentOS right now")
        create_instance(args)

    def delete_instance(self, region, instance_id, has_elastic_ip=False):
        logging.info("Deleting AWS instance {} in region {}".format(instance_id, region))
        ec2 = boto3.resource('ec2', region)
        instance = ec2.Instance(instance_id)
        if has_elastic_ip:
            client = boto3.client('ec2', region)
            elastic_ip_list = client.describe_addresses(
                Filters=[{'Name': 'public-ip', 'Values': [instance.public_ip_address]}]
            )["Addresses"]
            for elastic_ip in elastic_ip_list:
                client.disassociate_address(
                    AssociationId=elastic_ip["AssociationId"]
                )
                client.release_address(
                    AllocationId=elastic_ip["AllocationId"]
                )
            logging.info(
                "Deleted elastic ip at {} from VM {}".format(elastic_ip["PublicIp"], instance_id))
        instance.terminate()
        instance.wait_until_terminated()

    def mount_disk(self, host_info, vol_id, label):
        ec2 = boto3.client('ec2', region_name=host_info['region'])
        ec2.attach_volume(
            Device=label,
            VolumeId=vol_id,
            InstanceId=host_info['id']
        )
        waiter = ec2.get_waiter('volume_in_use')
        waiter.wait(VolumeIds=[vol_id])

    def unmount_disk(self, host_info, vol_id):
        ec2 = boto3.client('ec2', region_name=host_info['region'])
        ec2.detach_volume(VolumeId=vol_id, InstanceId=host_info['id'])
        waiter = ec2.get_waiter('volume_available')
        waiter.wait(VolumeIds=[vol_id])

    def clone_disk(self, args, volume_id, num_disks):
        output = []
        snapshot = None

        try:
            ec2 = boto3.resource('ec2', args.region)
            volume = ec2.Volume(volume_id)
            logging.info("==> Going to create a snapshot from {}".format(volume_id))
            snapshot = volume.create_snapshot()
            snapshot.wait_until_completed()
            logging.info("==> Created a snapshot {}".format(snapshot.id))

            for _ in range(num_disks):
                vol = ec2.create_volume(
                    AvailabilityZone=args.zone,
                    SnapshotId=snapshot.id
                )
                output.append(vol.id)
        finally:
            if snapshot:
                snapshot.delete()

        return output

    def modify_tags(self, args):
        instance = self.get_host_info(args)
        if not instance:
            raise YBOpsRuntimeError("Could not find instance {}".format(args.search_pattern))
        modify_tags(args.region, instance["id"], args.instance_tags, args.remove_tags)

    def update_disk(self, args):
        instance = self.get_host_info(args)
        if not instance:
            raise YBOpsRuntimeError("Could not find instance {}".format(args.search_pattern))
        update_disk(args, instance["id"])

    def change_instance_type(self, args, newInstanceType):
        change_instance_type(args["region"], args["id"], newInstanceType)

    def stop_instance(self, args):
        ec2 = boto3.resource('ec2', args["region"])
        try:
            instance = ec2.Instance(id=args["id"])
            instance.stop()
            instance.wait_until_stopped()
        except ClientError as e:
            logging.error(e)

    def start_instance(self, args, ssh_port):
        ec2 = boto3.resource('ec2', args["region"])
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            instance = ec2.Instance(id=args["id"])
            instance.start()
            instance.wait_until_running()
            # The OS boot up may take some time,
            # so retry until the instance allows SSH connection.
            self.wait_for_ssh_port(args["private_ip"], args["id"], ssh_port)
        except ClientError as e:
            logging.error(e)
        finally:
            sock.close()

    def get_console_output(self, args):
        instance = self.get_host_info(args)

        try:
            ec2 = boto3.client('ec2', region_name=instance['region'])
            return ec2.get_console_output(InstanceId=instance['id'], Latest=True).get('Output', '')
        except ClientError:
            logging.exception('Failed to get console output from {}'.format(args.search_pattern))

        return ''
