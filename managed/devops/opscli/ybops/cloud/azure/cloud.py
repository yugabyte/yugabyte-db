#!/usr/bin/env python
#
# Copyright 2020 YugaByte, Inc. and Contributors
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
from ybops.cloud.azure.command import AzureNetworkCommand, AzureInstanceCommand, \
    AzureAccessCommand, AzureQueryCommand, AzureDnsCommand
from ybops.cloud.azure.utils import AzureCloudAdmin, create_resource_group
from ybops.utils.remote_shell import RemoteShell
from azure.identity import DefaultAzureCredential
from azure.core.exceptions import ClientAuthenticationError


class AzureCloud(AbstractCloud):
    """Subclass related to Azure specific functionality.
        Assumes env variable "AZURE_RG" is set to name of resource_group
        used for all cloud operations.
    """
    BASE_INSTANCE_METADATA_API = "http://169.254.169.254/metadata/instance?api-version="
    API_VERSION = "2021-02-01"
    METADATA_API_TIMEOUT_SECONDS = 3

    def __init__(self):
        super(AzureCloud, self).__init__("azu")
        self.admin = None

    def get_admin(self):
        if self.admin is None:
            self.admin = AzureCloudAdmin(self.metadata)
        return self.admin

    def add_subcommands(self):
        """Override to implement cloud specific commands.
        """
        self.add_subcommand(AzureInstanceCommand())
        self.add_subcommand(AzureNetworkCommand())
        self.add_subcommand(AzureAccessCommand())
        self.add_subcommand(AzureQueryCommand())
        self.add_subcommand(AzureDnsCommand())

    def network_bootstrap(self, args):
        # Each region code maps to dictionary containing
        #   "vpcId": String representing Azure VNet name (equivalent to VPC in AWS)
        #   "azToSubnetIds": Dict mapping zones to subnet name
        #   "customSecurityGroupId": (Optional) String representing Azure SG name
        # if provided by the user
        custom_payload = json.loads(args.custom_payload)
        perRegionMetadata = custom_payload.get("perRegionMetadata")

        # First, make sure the resource group exists.
        # If not, place it in arbitrary Azure region about to be bootstrapped.
        create_resource_group(next(iter(perRegionMetadata.keys())))

        added_region_codes = custom_payload.get("addedRegionCodes")
        if added_region_codes is None:
            added_region_codes = perRegionMetadata.keys()
        # Verify that the user provided data
        user_provided_vnets = len([r for k, r in perRegionMetadata.items()
                                   if k in added_region_codes and r.get("vpcId") is not None])
        if user_provided_vnets > 0 and user_provided_vnets != len(added_region_codes):
            raise YBOpsRuntimeError("Either no regions or all regions must have vpcId specified.")

        components = {}
        if user_provided_vnets > 0:
            logging.info("Found custom payload info - simple return.")
            for region, metadata in perRegionMetadata.items():
                # Assume the user has already set up peering/routing for specified network info
                components[region] = self.get_admin().network(metadata).to_components()
        else:
            logging.info("Bootstrapping individual regions.")
            # Bootstrap the individual region items standalone (vnet, subnet, sg, RT, etc).
            for region, metadata in perRegionMetadata.items():
                if region in added_region_codes:
                    components[region] = self.get_admin().network(metadata) \
                                             .bootstrap(region).to_components()
                else:
                    components[region] = self.get_admin().network(metadata).to_components()
            self.get_admin().network().peer(components)
        print(json.dumps(components))

    def network_cleanup(self, args):
        perRegionMetadata = json.loads(args.custom_payload).get("perRegionMetadata")
        for region, metadata in perRegionMetadata.items():
            self.get_admin().network(metadata).cleanup(region)

    def mount_disk(self, host_info, os_disk_id):
        self.get_admin().update_os_disk(host_info["name"], os_disk_id)

    def clone_disk(self, args, volume_id, num_disks):
        zoneParts = args.zone.split('-')
        zone = zoneParts[1] if len(zoneParts) > 1 else None
        return self.get_admin().clone_disk(
            args.search_pattern, args.region, zone, volume_id, num_disks)

    def create_or_update_instance(self, args, adminSSH, tags_to_remove=None):
        vmName = args.search_pattern
        region = args.region
        zoneParts = args.zone.split('-')
        zone = zoneParts[1] if len(zoneParts) > 1 else None
        logging.info("[app] Creating/Updating Azure VM {} in {}/{}.".format(vmName, region, zone))

        # Reject removing internal tags.
        if tags_to_remove and "yb-server-type" in tags_to_remove.split(","):
            raise YBOpsRuntimeError(
                "Was asked to remove tags: {}, which contain internal tags: yb-server-type".format(
                    tags_to_remove
                ))

        subnet = args.cloud_subnet
        numVolumes = args.num_volumes
        volSize = args.volume_size
        volType = args.volume_type
        private_key_file = args.private_key_file
        instanceType = args.instance_type
        image = args.machine_image
        nsg = args.security_group_id
        vnet = args.vpcId
        public_ip = args.assign_public_ip
        disk_iops = args.disk_iops
        disk_throughput = args.disk_throughput
        spot_price = args.spot_price
        use_spot_instance = args.use_spot_instance
        tags = json.loads(args.instance_tags) if args.instance_tags is not None else {}
        vm_params = json.loads(args.custom_vm_params).get(region) \
            if args.custom_vm_params is not None else {}
        if vm_params is None:
            logging.warning("[app] VM parameters not specified for region {}.".format(region))
        disk_params = json.loads(args.custom_disk_params).get(region) \
            if args.custom_disk_params is not None else {}
        if disk_params is None:
            logging.warning("[app] Disk parameters not specified for region {}.".format(region))
        network_params = json.loads(args.custom_network_params).get(region) \
            if args.custom_network_params is not None else {}
        if network_params is None:
            logging.warning("[app] Network parameters not specified for region {}".format(region))
        nicId = self.get_admin().create_or_update_nic(
            vmName, vnet, subnet, zone, nsg, region, public_ip, tags, network_params)
        output = self.get_admin()\
            .create_or_update_vm(vmName, zone, numVolumes, private_key_file, volSize,
                                 instanceType, adminSSH, image, volType, args.type, region,
                                 nicId, tags, disk_iops, disk_throughput, spot_price,
                                 use_spot_instance, vm_params, disk_params,
                                 cloud_instance_types=args.cloud_instance_types)
        logging.info("[app] Updated Azure VM {}.".format(vmName, region, zone))
        return output

    def change_instance_type(self, host_info, instance_type, cloud_instance_types):
        self.get_admin().change_instance_type(host_info['name'], instance_type,
                                              cloud_instance_types)

    def destroy_instance(self, args):
        host_info = self.get_host_info(args)
        if host_info is None:
            logging.error("Host {} does not exist.".format(args.search_pattern))
            self.get_admin().destroy_orphaned_resources(args.search_pattern, args.node_uuid)
            return
        if args.node_ip is None:
            if args.node_uuid is None or host_info['node_uuid'] != args.node_uuid:
                logging.error("Host {} UUID does not match.".format(args.search_pattern))
                return
        elif host_info.get('private_ip') != args.node_ip:
            logging.error("Host {} IP does not match.".format(args.search_pattern))
            return
        self.get_admin().destroy_instance(args.search_pattern, args.node_uuid)

    def query_vpc(self, args):
        result = {}
        regions = [args.region] if args.region else self.get_regions()
        for region in regions:
            result[region] = self.get_admin().query_vpc()
            result[region]["default_image"] = self.get_image(region)
        return result

    def get_image(self, region):
        return self.metadata["regions"][region]["image"]

    def get_regions(self):
        return list(self.metadata.get("regions", {}).keys())

    def get_zones(self, args):
        result = {}
        regions = [args.region] if args.region else self.get_regions()
        for region in regions:
            result[region] = self.get_admin().get_zone_to_subnets(args.dest_vpc_id, region)
        return result

    def get_default_vnet(self, args):
        result = {}
        regions = [args.region] if args.region else self.get_regions()
        for region in regions:
            result[region] = self.get_admin().network().get_default_vnet(region)
        return result

    def get_instance_types(self, args):
        regions = args.regions if args.regions else self.get_regions()
        return self.get_admin().get_instance_types(regions)

    def get_host_info(self, args, get_all=False):
        return self.get_admin().get_host_info(args.search_pattern, get_all)

    def get_device_names(self, args):
        return ["sd{}".format(chr(i)) for i in range(ord('c'), ord('c') + args.num_volumes)]

    def get_ultra_instances(self, args):
        regions = args.regions if args.regions else self.get_regions()
        return self.get_admin().get_ultra_instances(regions, args.folder)

    def update_disk(self, args):
        instance = self.get_host_info(args)
        if not instance:
            raise YBOpsRuntimeError("Could not find instance {}".format(args.search_pattern))
        self.get_admin().update_disk(instance["name"], args.volume_size)

    def list_dns_record_set(self, dns_zone_id):
        return self.get_admin().list_dns_record_set(dns_zone_id)

    def create_dns_record_set(self, dns_zone_id, domain_name_prefix, ip_list):
        return self.get_admin().create_dns_record_set(dns_zone_id, domain_name_prefix, ip_list)

    def edit_dns_record_set(self, dns_zone_id, domain_name_prefix, ip_list):
        return self.get_admin().edit_dns_record_set(dns_zone_id, domain_name_prefix, ip_list)

    def delete_dns_record_set(self, dns_zone_id, domain_name_prefix):
        return self.get_admin().delete_dns_record_set(dns_zone_id, domain_name_prefix)

    def modify_tags(self, args):
        instance = self.get_host_info(args)
        if not instance:
            raise YBOpsRuntimeError("Could not find instance {}".format(args.search_pattern))
        modify_tags(args.region, instance["id"], args.instance_tags, args.remove_tags)

    def start_instance(self, host_info, server_ports):
        vm_name = host_info['name']
        vm_status = self.get_admin().get_vm_status(vm_name)
        if vm_status != 'deallocated' and vm_status != 'stopped':
            logging.warning("Host {} is not stopped, VM status is {}".format(
                vm_name, vm_status))
        else:
            self.get_admin().start_instance(host_info['name'])

        # Refreshing private IP address.
        host_info = self.get_admin().get_host_info(vm_name, False)
        if not host_info:
            logging.error("Error restarting VM {} - unable to get host info.".format(
                vm_name))
            return

        self.wait_for_server_ports(host_info['private_ip'], vm_name, server_ports)
        return host_info

    def stop_instance(self, host_info):
        return self.get_admin().deallocate_instance(host_info['name'])

    def expand_file_system(self, args, connect_options):
        remote_shell = RemoteShell(connect_options)
        mount_points = self.get_mount_points_csv(args).split(',')
        for mount_point in mount_points:
            # need to rescan disks to see changes
            cmd1 = "df | awk '($6 == \"" + mount_point + "\") {print $1}' | grep -o 'sd\\w*$'"
            resp = remote_shell.run_command(cmd1)
            if resp.exited == 1:
                raise YBOpsRuntimeError("Failed to get fs for mount point {}".format(mount_point))
            fsname = resp.stdout.replace('\n', '')
            cmd2 = "sudo bash -c 'echo 1 > /sys/class/block/{}/device/rescan' " \
                "&& sudo fdisk -l /dev/{}".format(fsname, fsname)
            resp2 = remote_shell.run_command(cmd2)
            if resp2.exited == 1:
                raise YBOpsRuntimeError("Failed to do rescan for {}".format(mount_point))
            logging.info("Expanding file system with mount point: {}".format(mount_point))
            remote_shell.run_command('sudo xfs_growfs {}'.format(mount_point))

    def normalize_instance_state(self, instance_state):
        if instance_state:
            instance_state = instance_state.lower()
            if instance_state in ("creating", "starting"):
                return InstanceState.STARTING
            if instance_state in ("running"):
                return InstanceState.RUNNING
            if instance_state in ("stopping"):
                return InstanceState.STOPPING
            if instance_state in ("stopped"):
                return InstanceState.STOPPED
            if instance_state in ("deallocating"):
                return InstanceState.TERMINATING
            if instance_state in ("deallocated"):
                return InstanceState.TERMINATED
        return InstanceState.UNKNOWN

    def has_machine_credentials(self):
        """
        Override for superclass method to detect if current instance has cloud access credentials.
        """
        try:
            # Try to get credentials using DefaultAzureCredential
            credential = DefaultAzureCredential()
            token = credential.get_token("https://management.azure.com/.default")
            # If successful, return True
            return True
        except ClientAuthenticationError as e:
            # If authentication fails return False
            return False

    def get_instance_metadata(self):
        imds_url = self.BASE_INSTANCE_METADATA_API + self.API_VERSION
        headers = {"Metadata": "true"}
        response = requests.get(imds_url, headers=headers,
                                timeout=self.METADATA_API_TIMEOUT_SECONDS)
        response.raise_for_status()  # Check for HTTP errors
        return response.json()

    def get_current_host_info(self, args):
        """This method would fetch current host information by calling Azure metadata api
        to fetch requested metatdata's.
        """
        try:
            res = self.get_instance_metadata()
            return res
        except (requests.exceptions.RequestException, socket.timeout) as e:
            raise YBOpsRuntimeError("Unable to auto-discover AZU provider information")
