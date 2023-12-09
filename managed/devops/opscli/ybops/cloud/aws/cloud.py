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
import subprocess

import boto3
from botocore.exceptions import ClientError
from botocore.utils import InstanceMetadataFetcher
import requests
from six.moves.urllib.error import URLError
from six.moves.urllib.request import urlopen, Request
from ybops.cloud.aws.command import (AwsAccessCommand, AwsDnsCommand, AwsInstanceCommand,
                                     AwsNetworkCommand, AwsQueryCommand)
from ybops.cloud.aws.utils import (AwsBootstrapClient, YbVpcComponents,
                                   change_instance_type, create_instance, delete_vpc, get_client,
                                   get_clients, get_device_names, get_spot_pricing,
                                   get_vpc_for_subnet, get_zones, has_ephemerals, modify_tags,
                                   query_vpc, update_disk, get_image_arch, get_root_label)
from ybops.cloud.common.cloud import AbstractCloud, InstanceState
from ybops.common.exceptions import YBOpsRecoverableError, YBOpsRuntimeError
from ybops.utils import is_valid_ip_address
from ybops.utils.ssh import (format_rsa_key, validated_key_file)


class AwsCloud(AbstractCloud):
    """Subclass specific to AWS cloud related functionality.
    """
    BASE_INSTANCE_METADATA_API = "http://169.254.169.254/"
    INSTANCE_METADATA_API = os.path.join(BASE_INSTANCE_METADATA_API, "2016-09-02/meta-data/")
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

    def get_image(self, region=None, architecture="x86_64"):
        regions = [region] if region is not None else self.get_regions()
        imageKey = "arm_image" if architecture == "aarch64" else "image"
        output = {}
        for r in regions:
            output[r] = self.metadata["regions"][r][imageKey]
        return output

    def get_image_arch(self, args):
        return get_image_arch(args.region, args.machine_image)

    def get_image_root_label(self, args):
        return get_root_label(args.region, args.machine_image)

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

    def _generate_fingerprints(self, key_file_path):
        """
        Method to generate all possible fingerprints of the key_file to match with KeyPair in AWS.
        https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/ec2-key-pairs.html
        """
        try:
            md5 = subprocess.check_output(
                "ssh-keygen -ef {} -m PEM | openssl rsa -RSAPublicKey_in -outform DER "
                "| openssl md5 -c".format(key_file_path), shell=True
            ).decode('utf-8').strip()
            sha1 = subprocess.check_output(
                "openssl pkcs8 -in {} -inform PEM -outform DER -topk8 -nocrypt "
                "| openssl sha1 -c".format(key_file_path), shell=True
            ).decode('utf-8').strip()
            sha256 = subprocess.check_output(
                "ssh-keygen -ef {} -m PEM | openssl rsa -RSAPublicKey_in -outform DER "
                "| openssl sha256 -c".format(key_file_path), shell=True
            ).decode('utf-8').strip()
        except subprocess.CalledProcessError as e:
            raise YBOpsRuntimeError("Error generating fingerprints for {}. Shell Output {}"
                                    .format(key_file_path, e.output))
        return [md5, sha1, sha256]

    def list_key_pair(self, args):
        key_pair_name = args.key_pair_name if args.key_pair_name else '*'
        filters = [{'Name': 'key-name', 'Values': [key_pair_name]}]
        result = {}
        for region, client in self._get_clients(args.region).items():
            result[region] = [(keyInfo.name, keyInfo.key_fingerprint)
                              for keyInfo in client.key_pairs.filter(Filters=filters)]
        return result

    def delete_key_pair(self, args):
        for region, client in self._get_clients(args.region).items():
            try:
                client.KeyPair(args.key_pair_name).delete()
            except ClientError as e:
                code = e.response['Error']['Code']
                if code == 'AuthFailure' and args.ignore_auth_failure:
                    logging.warn("Ignoring error: {}".format(str(e)))
                else:
                    raise e

    def add_key_pair(self, args):
        """
        Method to add key pair to AWS EC2.
        True if new key pair with given name is added to AWS by Platform.
        False if key pair with same name already exists and fingerprint is verified
        Raises error if key is invalid, fingerprint generation fails, or fingerprint mismatches
        """
        key_pair_name = args.key_pair_name
        # If we were provided with a private key file, we use that to generate the public
        # key using RSA. If not we will use the public key file (assuming the private key exists).
        key_file = args.private_key_file if args.private_key_file else args.public_key_file
        key_file_path = os.path.join(args.key_file_path, key_file)

        if not os.path.exists(key_file_path):
            raise YBOpsRuntimeError("Key: {} file not found".format(key_file_path))

        # This call would throw a exception if the file is not valid key file.
        rsa_key = validated_key_file(key_file_path)

        # Validate the key pair if name already exists in AWS
        result = list(self.list_key_pair(args).values())[0]
        if len(result) > 0:
            # Try to validate the keypair with KeyPair fingerprint in AWS
            fingerprint = result[0][1]
            possible_fingerprints = self._generate_fingerprints(key_file_path)
            if fingerprint in possible_fingerprints:
                return False
            raise YBOpsRuntimeError("KeyPair {} already exists but fingerprint is invalid."
                                    .format(key_pair_name))

        result = {}
        for region, client in self._get_clients(args.region).items():
            result[region] = client.import_key_pair(
                KeyName=key_pair_name,
                PublicKeyMaterial=format_rsa_key(rsa_key, public_key=True)
            )
        return True

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
        added_region_codes = custom_payload.get("addedRegionCodes")
        if added_region_codes is None:
            added_region_codes = per_region_meta.keys()

        user_provided_vpc_ids = len([r for k, r in per_region_meta.items()
                                    if k in added_region_codes and r.get("vpcId") is not None])
        if user_provided_vpc_ids > 0 and user_provided_vpc_ids != len(added_region_codes):
            raise YBOpsRuntimeError("Either no regions or all regions must have vpcId specified.")

        components = {}
        if user_provided_vpc_ids > 0:
            for region in metadata_subset:
                components[region] = YbVpcComponents.from_user_json(
                    region, per_region_meta.get(region))
        else:
            # Bootstrap the individual region items standalone (vpc, subnet, sg, RT, etc).
            for region in metadata_subset:
                if region in added_region_codes:
                    components[region] = client.bootstrap_individual_region(region)
                else:
                    components[region] = YbVpcComponents.from_user_json(
                        region, per_region_meta.get(region))
            # Cross link all the regions together.
            client.cross_link_regions(components, added_region_codes)
        return {region: c.as_json() for region, c in components.items()}

    def query_vpc(self, args):
        result = {}
        for region in self._get_all_regions_or_arg(args.region):
            result[region] = query_vpc(region)
            result[region]["default_image"] = self.get_image(region, args.architecture).get(region)
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

    def get_and_append_imdsv2_token_header(self, url):
        # Fetch IMDSv2 token from the instance metadata service
        try:
            headers = {'X-aws-ec2-metadata-token-ttl-seconds': '60'}
            token_url = os.path.join(self.BASE_INSTANCE_METADATA_API, "latest", "api", "token")
            response = requests.put(token_url, headers=headers, timeout=20)
            response.raise_for_status()
            token = response.text
        except Exception as e:
            logging.info("[app] Token retrieval via imdsv2 failed, falling back to imds, {}".
                         format(e))
            token = None

        request = Request(url)
        if token:
            request.add_header('X-aws-ec2-metadata-token', token)

        return request

    def get_instance_metadata(self, metadata_type):
        """This method fetches instance metadata using AWS metadata api
        Args:
            metadata_type (str): Metadata to fetch
        Returns:
            metadata (str): metadata information for the requested metadata key or
            raises a runtime exception.
        """
        if metadata_type in ["mac", "instance-id", "security-groups"]:
            request = self.get_and_append_imdsv2_token_header(os.path.join(
                self.INSTANCE_METADATA_API, metadata_type))
            return urlopen(request,
                           timeout=self.METADATA_API_TIMEOUT_SECONDS).read().decode('utf-8')
        elif metadata_type in ["vpc-id", "subnet-id"]:
            mac = self.get_instance_metadata("mac")
            request = self.get_and_append_imdsv2_token_header(os.path.join(
                self.NETWORK_METADATA_API, mac, metadata_type))
            return urlopen(request,
                           timeout=self.METADATA_API_TIMEOUT_SECONDS).read().decode('utf-8')
        elif metadata_type in ["region", "privateIp"]:
            request = self.get_and_append_imdsv2_token_header(self.INSTANCE_IDENTITY_API)
            identity_data = urlopen(request,
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
                                    private_ip=None, filters=None, node_uuid=None):
        if not filters:
            filters = [
                {
                    "Name": "instance-state-name",
                    # Include all non-terminating states.
                    "Values": ["rebooting", "pending", "running", "stopping", "stopped"]
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
        if node_uuid:
            filters.append({
                "Name": "tag:node-uuid",
                "Values": [node_uuid]
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
            instance_state = data["State"].get("Name") if data.get("State") is not None else None
            logging.info("VM state {}".format(instance_state))
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
                instance_state=instance_state,
                is_running=True if instance_state == "running" else False
            )
            disks = data.get("BlockDeviceMappings")
            root_vol = next((d for d in disks if
                            d.get("DeviceName") == data.get("RootDeviceName")), None)
            ebs = root_vol.get("Ebs") if root_vol else None
            result["root_volume"] = ebs.get("VolumeId") if ebs else None
            if not get_all:
                return result
            results.append(result)
        return results

    def get_device_names(self, args):
        if has_ephemerals(args.instance_type, args.region):
            return []
        else:
            return get_device_names(args.instance_type, args.num_volumes, args.region)

    def get_subnet_cidr(self, args, subnet_id):
        ec2 = boto3.resource('ec2', args.region)
        subnet = ec2.Subnet(subnet_id)
        return subnet.cidr_block

    def create_instance(self, args):
        # If we are configuring second NIC, ensure that this only happens for a
        # centOS AMI right now.
        if args.cloud_subnet_secondary:
            supported_os = ['centos', 'almalinux']
            ec2 = boto3.resource('ec2', args.region)
            image = ec2.Image(args.machine_image)
            if not any(os_type in image.name.lower() for os_type in supported_os):
                raise YBOpsRuntimeError(
                    "Second NIC can only be configured for CentOS/Alma right now")
        create_instance(args)

    def delete_instance(self, region, instance_id, has_elastic_ip=False):
        logging.info("[app] Deleting AWS instance {} in region {}".format(
            instance_id, region))
        ec2 = boto3.resource('ec2', region)
        instance = ec2.Instance(instance_id)
        if has_elastic_ip:
            client = boto3.client('ec2', region)
            for network_interfaces in instance.network_interfaces_attribute:
                if 'Association' not in network_interfaces:
                    continue
                public_ip_address = network_interfaces['Association'].get('PublicIp')
                if public_ip_address:
                    elastic_ip_list = client.describe_addresses(
                        Filters=[{'Name': 'public-ip', 'Values': [public_ip_address]}]
                    )["Addresses"]
                    for elastic_ip in elastic_ip_list:
                        client.disassociate_address(
                            AssociationId=elastic_ip["AssociationId"]
                        )
                        client.release_address(
                            AllocationId=elastic_ip["AllocationId"]
                        )
                logging.info("[app] Deleted elastic ip {}".format(public_ip_address))

        # persistent spot requests might have more than one instance associated with them
        # so terminate them all
        if (instance.spot_instance_request_id):
            spot_request_id = instance.spot_instance_request_id
            client = boto3.client('ec2', region)
            response = client.describe_spot_instance_requests(
                SpotInstanceRequestIds=[spot_request_id])
            spot_requests = response['SpotInstanceRequests']
            instance_ids = [request['InstanceId']
                            for request in spot_requests if 'InstanceId' in request]
            if instance_ids:
                client.terminate_instances(InstanceIds=instance_ids)
                logging.info(f"Terminated {len(instance_ids)} instances")

            logging.info(f"[app] Deleting spot instance request {spot_request_id}")
            client.cancel_spot_instance_requests(
                SpotInstanceRequestIds=[spot_request_id]
            )

        instance.terminate()
        instance.wait_until_terminated()

    def reboot_instance(self, host_info, server_ports):
        boto3.client('ec2', region_name=host_info['region']).reboot_instances(
            InstanceIds=[host_info["id"]]
        )
        self.wait_for_server_ports(host_info["private_ip"], host_info["name"], server_ports)

    def mount_disk(self, host_info, vol_id, label):
        logging.info("Mounting volume {} on host {}; label {}".format(
                     vol_id, host_info['id'], label))
        ec2 = boto3.client('ec2', region_name=host_info['region'])
        ec2.attach_volume(
            Device=label,
            VolumeId=vol_id,
            InstanceId=host_info['id']
        )
        waiter = ec2.get_waiter('volume_in_use')
        waiter.wait(VolumeIds=[vol_id])
        logging.info("Setting delete on termination for {} attached to {}"
                     .format(vol_id, host_info['id']))
        # Even if this fails, volumes are already tagged for cleanup.
        ec2.modify_instance_attribute(InstanceId=host_info['id'],
                                      Attribute='blockDeviceMapping',
                                      BlockDeviceMappings=[{
                                          'DeviceName': label,
                                          'Ebs': {'DeleteOnTermination': True}}])

    def unmount_disk(self, host_info, vol_id):
        logging.info("Unmounting volume {} from host {}".format(vol_id, host_info['id']))
        ec2 = boto3.client('ec2', region_name=host_info['region'])
        ec2.detach_volume(VolumeId=vol_id, InstanceId=host_info['id'])
        waiter = ec2.get_waiter('volume_available')
        waiter.wait(VolumeIds=[vol_id])

    def clone_disk(self, args, volume_id, num_disks,
                   snapshot_creation_delay=15, snapshot_creation_max_attempts=80):
        output = []
        snapshot = None
        ec2 = boto3.client('ec2', args.region)

        try:
            resource_tags = []
            tags = json.loads(args.instance_tags) if args.instance_tags is not None else {}
            for tag in tags:
                resource_tags.append({
                    'Key': tag,
                    'Value': tags[tag]
                })
            snapshot_tag_specs = [{
                'ResourceType': 'snapshot',
                'Tags': resource_tags
            }]
            volume_tag_specs = [{
                'ResourceType': 'volume',
                'Tags': resource_tags
            }]
            wait_config = {
                'Delay': snapshot_creation_delay,
                'MaxAttempts': snapshot_creation_max_attempts
            }
            logging.info("==> Going to create a snapshot from {}".format(volume_id))
            snapshot = ec2.create_snapshot(VolumeId=volume_id, TagSpecifications=snapshot_tag_specs)
            snapshot_id = snapshot['SnapshotId']
            waiter = ec2.get_waiter('snapshot_completed')
            waiter.wait(SnapshotIds=[snapshot_id], WaiterConfig=wait_config)
            logging.info("==> Created a snapshot {}".format(snapshot_id))

            for _ in range(num_disks):
                vol = ec2.create_volume(
                    AvailabilityZone=args.zone,
                    SnapshotId=snapshot_id,
                    TagSpecifications=volume_tag_specs
                )
                output.append(vol['VolumeId'])
        finally:
            if snapshot:
                ec2.delete_snapshot(SnapshotId=snapshot['SnapshotId'])

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

    def stop_instance(self, host_info):
        ec2 = boto3.resource('ec2', host_info["region"])
        try:
            instance = ec2.Instance(id=host_info["id"])
            if instance.state['Name'] != 'stopped':
                if instance.state['Name'] != 'stopping':
                    instance.stop()
                instance.wait_until_stopped()
        except ClientError as e:
            logging.error(e)
            raise YBOpsRuntimeError("Failed to start instance {}: {}".format(host_info["id"], e))

    def start_instance(self, host_info, server_ports):
        ec2 = boto3.resource('ec2', host_info["region"])
        try:
            instance = ec2.Instance(id=host_info["id"])
            if instance.state['Name'] != 'running':
                if instance.state['Name'] != 'pending':
                    instance.start()
                # Increase wait timeout to 15 * 80 = 1200 seconds
                # to work around failures in provisioning instances.
                wait_config = {
                    'Delay': 15,
                    'MaxAttempts': 80
                }
                instance.wait_until_running(WaiterConfig=wait_config)
            # The OS boot up may take some time,
            # so retry until the instance allows connection to either SSH or RPC.
            self.wait_for_server_ports(host_info["private_ip"], host_info["id"], server_ports)
        except ClientError as e:
            logging.error(e)
            if e.response.get("Error", {}).get("code") == "InsufficientInstanceCapacity":
                raise YBOpsRecoverableError("Aws InsufficientInstanceCapacity error")
            else:
                raise YBOpsRuntimeError("Failed to start instance {}: {}".format(
                    host_info["id"], e))

    def update_user_data(self, args):
        if args.boot_script is None:
            return
        new_user_data = ''
        with open(args.boot_script, 'r') as script:
            new_user_data = script.read()
        instance = self.get_host_info(args)
        logging.info("[app] Updating the user_data for the instance {}".format(instance['id']))
        try:
            ec2 = boto3.client('ec2', region_name=instance['region'])
            ec2.modify_instance_attribute(InstanceId=instance['id'],
                                          UserData={'Value': new_user_data})
        except ClientError as e:
            logging.exception('Failed to update user_data for {}'.format(args.search_pattern))
            raise e

    def get_console_output(self, args):
        instance = self.get_host_info(args)

        if not instance:
            logging.warning('Could not find instance {}, no console output available'.format(
                args.search_pattern))
            return ''

        try:
            ec2 = boto3.client('ec2', region_name=instance['region'])
            return ec2.get_console_output(InstanceId=instance['id'], Latest=True).get('Output', '')
        except ClientError:
            logging.exception('Failed to get console output from {}'.format(args.search_pattern))

        return ''

    def delete_volumes(self, args):
        tags = json.loads(args.instance_tags) if args.instance_tags is not None else {}
        if not tags:
            raise YBOpsRuntimeError('Tags must be specified')
        universe_uuid = tags.get('universe-uuid')
        if universe_uuid is None:
            raise YBOpsRuntimeError('Universe UUID must be specified')
        node_uuid = tags.get('node-uuid')
        if node_uuid is None:
            raise YBOpsRuntimeError('Node UUID must be specified')
        filters = []
        tagPairs = {}
        tagPairs['universe-uuid'] = universe_uuid
        tagPairs['node-uuid'] = node_uuid
        for tag in tagPairs:
            value = tagPairs[tag]
            filters.append({
                'Name': "tag:{}".format(tag),
                'Values': [value]
            })
        filters.append({
            'Name': 'status',
            'Values': ['available']
        })
        volume_ids = []
        describe_volumes_args = {
            'Filters': filters
        }
        if args.volume_id:
            describe_volumes_args.update('VolumeIds', args.volume_id)
        client = boto3.client('ec2', args.region)
        while True:
            response = client.describe_volumes(**describe_volumes_args)
            for volume in response.get('Volumes', []):
                status = volume['State']
                volume_id = volume['VolumeId']
                present_tags = volume['Tags']
                logging.info('[app] Volume {} is in state {}'.format(volume_id, status))
                if status.lower() != 'available':
                    continue
                tag_match_count = 0
                # Extra caution to make sure tags are present.
                for tag in tagPairs:
                    value = tagPairs[tag]
                    for present_tag in present_tags:
                        if present_tag['Key'] == tag and present_tag['Value'] == value:
                            tag_match_count += 1
                            break
                if tag_match_count == len(tagPairs):
                    volume_ids.append(volume_id)
            if 'NextToken' in response:
                describe_volumes_args['NextToken'] = response['NextToken']
            else:
                break
        for volume_id in volume_ids:
            logging.info('[app] Deleting volume {}'.format(volume_id))
            client.delete_volume(VolumeId=volume_id)

    def normalize_instance_state(self, instance_state):
        if instance_state:
            instance_state = instance_state.lower()
            if instance_state in ("pending", "rebooting"):
                return InstanceState.STARTING
            if instance_state in ("running"):
                return InstanceState.RUNNING
            if instance_state in ("stopping"):
                return InstanceState.STOPPING
            if instance_state in ("stopped"):
                return InstanceState.STOPPED
            if instance_state in ("terminated"):
                return InstanceState.TERMINATED
        return InstanceState.UNKNOWN
