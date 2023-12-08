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
import time

from ybops.cloud.common.cloud import AbstractCloud, InstanceState
from ybops.cloud.gcp.command import (GcpAccessCommand, GcpInstanceCommand, GcpNetworkCommand,
                                     GcpQueryCommand)
from ybops.cloud.gcp.utils import (GCP_SCRATCH, GcpMetadata, GoogleCloudAdmin)
from ybops.common.exceptions import YBOpsRuntimeError, get_exception_message


class GcpCloud(AbstractCloud):
    """Subclass specific to GCP cloud related functionality.
    """
    FREE_OS_LIST = ["Linux"]

    def __init__(self):
        super(GcpCloud, self).__init__("gcp")
        self.admin = None
        self._wait_for_startup_script_command = \
            "while ps -ef | grep 'google_metadata_script_runner startup' | " \
            "grep -v grep ; do sleep 1 ; done"

    def get_admin(self):
        if self.admin is None:
            self.admin = GoogleCloudAdmin(self.metadata)
        return self.admin

    def add_subcommands(self):
        """Override to setup the cloud-specific instances of the subcommands.
        """
        self.add_subcommand(GcpInstanceCommand())
        self.add_subcommand(GcpQueryCommand())
        self.add_subcommand(GcpAccessCommand())
        self.add_subcommand(GcpNetworkCommand())

    def has_machine_credentials(self):
        """
        Override for superclass method to detect if current instance has cloud access credentials.
        """
        content = GcpMetadata.service_accounts()
        return content is not None and len(content) > 0

    def get_image(self, region):
        region_meta = self.metadata["regions"][region]
        return self.get_admin().get_image(region_meta["image"], region_meta["project"])

    def query_vpc(self, args):
        result = {}
        regions = [args.region] if args.region else self.get_regions(args)
        for region in regions:
            result[region] = self.get_admin().query_vpc(region)
            result[region]["default_image"] = self.get_image(region)["selfLink"]
        return result

    def get_subnet_cidr(self, args, subnet_id):
        subnet = self.get_admin().get_subnetwork_by_name(args.region,
                                                         subnet_id)
        return subnet['ipCidrRange']

    def create_instance(self, args, server_type, can_ip_forward, machine_image, ssh_keys):
        # If we are configuring second NIC, ensure that this only happens for a
        # centOS AMI right now.
        if args.cloud_subnet_secondary:
            # GCP machine image for centos is of the form:
            # https://www.googleapis.com/compute/beta/projects/centos-cloud/global/images/*
            supported_os = ['centos', 'almalinux']
            if not any(os_type in machine_image for os_type in supported_os):
                raise YBOpsRuntimeError(
                    "Second NIC can only be configured for CentOS/Alma right now")

        self.get_admin().create_instance(
            args.region, args.zone, args.cloud_subnet, args.search_pattern, args.instance_type,
            server_type, args.use_spot_instance, can_ip_forward, machine_image, args.num_volumes,
            args.volume_type, args.volume_size, args.boot_disk_size_gb, args.assign_public_ip,
            args.assign_static_public_ip, ssh_keys, boot_script=args.boot_script,
            auto_delete_boot_disk=args.auto_delete_boot_disk, tags=args.instance_tags,
            cloud_subnet_secondary=args.cloud_subnet_secondary,
            gcp_instance_template=args.instance_template)

    def create_disk(self, args, body):
        self.get_admin().create_disk(args.zone, args.instance_tags, body)

    def clone_disk(self, args, volume_id, num_disks,
                   snapshot_creation_delay=15, snapshot_creation_max_attempts=80):
        output = []
        # disk names must match regex https://cloud.google.com/compute/docs/reference/rest/v1/disks
        name = args.search_pattern[:58] if len(args.search_pattern) > 58 else args.search_pattern
        for x in range(num_disks):
            res = self.get_admin().create_disk(args.zone, args.instance_tags, body={
                "name": "{}-d{}".format(name, x),
                "sizeGb": args.boot_disk_size_gb,
                "sourceDisk": volume_id})
            output.append(res["targetLink"])

            # GCP throttles disk cloning operations
            # https://cloud.google.com/compute/docs/disks/create-disk-from-source#restrictions
            if x != num_disks - 1:
                time.sleep(30)

        return output

    def mount_disk(self, args, body):
        logging.info("Mounting disk on host {} in zone {}; volume info is {}".format(
                     args['search_pattern'], args['zone'], body))
        self.get_admin().mount_disk(args['zone'], args['search_pattern'], body)

    def unmount_disk(self, args, name):
        logging.info("Unmounting disk {} from host {} in zone {}".format(
                     name, args['search_pattern'], args['zone']))
        self.get_admin().unmount_disk(args['zone'], args['search_pattern'], name)

    def stop_instance(self, args):
        instance = self.get_admin().get_instances(args['zone'], args['search_pattern'])
        if not instance:
            logging.error("Host {} does not exist".format(args['search_pattern']))
            return
        instance_state = instance['instance_state']
        if instance_state == 'TERMINATED':
            pass
        elif instance_state == 'RUNNING':
            self.admin.stop_instance(instance['zone'], instance['name'])
        elif instance_state == 'STOPPING':
            self.admin.wait_for_operation(zone=instance['zone'],
                                          instance=instance['name'],
                                          operation_type='stop')
        else:
            raise YBOpsRuntimeError("Host {} cannot be stopped while in '{}' state".format(
                instance['name'], instance_state))

    def start_instance(self, args, server_ports):
        instance = self.get_admin().get_instances(args['zone'], args['search_pattern'])
        if not instance:
            logging.error("Host {} does not exist".format(args['search_pattern']))
            return
        instance_state = instance['instance_state']
        if instance_state == 'RUNNING':
            pass
        elif instance_state == 'TERMINATED':
            self.admin.start_instance(instance['zone'], instance['name'])
        elif instance_state in ('PROVISIONING', 'STAGING'):
            self.admin.wait_for_operation(zone=instance['zone'],
                                          instance=instance['name'],
                                          operation_type='start')
        else:
            raise YBOpsRuntimeError("Host {} cannot be started while in '{}' state".format(
                instance['name'], instance_state))
        self.wait_for_server_ports(instance['private_ip'], instance['name'], server_ports)

    def delete_instance(self, args, filters=None):
        host_info = self.get_host_info(args, filters=filters)
        if host_info is None:
            logging.error("Host {} does not exist.".format(args.search_pattern))
            return
        if args.node_ip is None:
            if args.node_uuid is None or host_info['node_uuid'] != args.node_uuid:
                logging.error("Host {} UUID does not match.".format(args.search_pattern))
                return
        elif host_info.get('private_ip') != args.node_ip:
            logging.error("Host {} IP does not match.".format(args.search_pattern))
            return
        self.get_admin().delete_instance(
            args.region, args.zone, args.search_pattern, has_static_ip=args.delete_static_public_ip)

    def reboot_instance(self, host_info, server_ports):
        self.admin.reboot_instance(host_info['zone'], host_info['name'])
        self.wait_for_server_ports(host_info["private_ip"], host_info["id"], server_ports)

    def update_user_data(self, args):
        if args.boot_script is None:
            return
        boot_script = ''
        with open(args.boot_script, 'r') as script:
            boot_script = script.read()
        instance = self.get_host_info(args)
        logging.info("[app] Updating the user_data for the instance {}".format(instance['id']))
        self.get_admin().update_boot_script(args, instance, boot_script)

    def get_regions(self, args):
        regions_we_know_of = self.get_admin().get_regions()
        if args.network is None:
            return regions_we_know_of
        else:
            # TODO(WESLEY): CHECK ON WHY THIS WASN"T RETURNING ANYTHING
            return list(self.get_admin().network(
                per_region_meta=self.get_per_region_meta(args)).get_network_data(
                    args.network)["regions"].keys())

    def get_zones(self, args):
        """This method returns a map of regions to zones.
        If region is passed in args, the map has exactly one key: args.region.
        """
        regions = [args.region] if args.region else self.get_regions(args)

        result = {}
        metadata = self.get_per_region_meta(args)
        for region in regions:
            result[region] = {}
            result[region]["zones"] = self.get_admin().get_zones(region)
            subnets = self.get_admin().network(
                args.dest_vpc_id, per_region_meta=metadata).get_subnetworks(
                    region)
            result[region]["subnetworks"] = subnets
        return result

    def get_current_host_info(self):
        try:
            return GoogleCloudAdmin.get_current_host_info()
        except YBOpsRuntimeError as e:
            return get_exception_message(e)

    def get_instance_types_map(self, args):
        """This method returns a dictionary mapping regions to a dictionary of zones
        mapped to a list of dictionaries containing available instance types and their
        descriptions. If region is passed in, we restrict results to the region and if
        both region and zone are passed in, we restrict to zone.
        """
        regions = args.regions if args.regions else self.get_regions(args)
        region_zones_map = {}
        for r in regions:
            region_zones_map[r] = self.get_admin().get_zones(r)

        result = {}
        for region, zones in region_zones_map.items():
            result[region] = {}
            for zone in zones:
                result[region][zone] = self.get_admin().get_instance_types_by_zone(zone)

        return result

    def get_pricing_map(self):
        return self.get_admin().get_pricing_map()

    def get_spot_pricing(self, args):
        pricing_map = self.get_pricing_map()
        return self.fetch_instance_price_from_map(
            pricing_map, args.instance_type, args.region, preemptible=True)

    def get_compute_image(self, name, preemptible=False):
        return self.get_admin().get_full_image_name(name, preemptible)

    def fetch_instance_price_from_map(self, pricing_map, name, region, preemptible=False):
        name_key = self.get_compute_image(name, preemptible)
        try:
            if region in pricing_map[name_key]:
                price_per_hour = pricing_map[name_key][region]
            else:
                price_per_hour = pricing_map[name_key][region[:-1]]
        # Do not enforce pricing requirement PLAT-4790.
        except KeyError as k:
            price_per_hour = 0.0
        except Exception as e:
            raise YBOpsRuntimeError(e)
        return price_per_hour

    def get_os_price_map(self, pricing_map, name, region, numCores, isShared):
        os_price_list = []

        for os in self.FREE_OS_LIST:
            os_map = {
                "os": os,
                "price": self.fetch_instance_price_from_map(pricing_map, name, region)
            }
            os_price_list.append(os_map)

        return os_price_list

    def get_instance_types(self, args):
        region_zones_instances_map = self.get_instance_types_map(args)
        pricing_map = self.get_pricing_map()

        result = {}
        for region, zone_instances_map in region_zones_instances_map.items():
            for zone, instances in zone_instances_map.items():
                for instance in instances:
                    name = instance["name"]
                    if name not in result:
                        result[name] = {
                            "prices": {},
                            "numCores": instance["guestCpus"],
                            "isShared": instance["isSharedCpu"],
                            "description": instance["description"],
                            "memSizeGb": float(instance["memoryMb"]/1024.0)
                        }
                    if region in result[name]["prices"]:
                        continue
                    result[name]["prices"][region] = self.get_os_price_map(pricing_map,
                                                                           name,
                                                                           region,
                                                                           result[name]["numCores"],
                                                                           result[name]["isShared"])
        to_delete_instance_types = []
        for name in result:
            to_delete_regions = []
            for region in result[name]["prices"]:
                for zone in region_zones_instances_map[region]:
                    if name not in region_zones_instances_map[region][zone]:
                        to_delete_regions.append(region)
                        break
            for region in to_delete_regions:
                result[name].pop(region, None)
            if not result[name]:
                to_delete_instance_types.append(name)
        for name in to_delete_instance_types:
            result.pop(name, None)
        return result

    def network_bootstrap(self, args):
        custom_payload = json.loads(args.custom_payload)
        dest_vpc_id = custom_payload.get("destVpcId")
        host_vpc_id = custom_payload.get("hostVpcId")
        per_region_meta = custom_payload.get("perRegionMetadata")
        create_new_vpc = custom_payload.get("createNewVpc")
        return self.get_admin().network(
            dest_vpc_id, host_vpc_id, per_region_meta, create_new_vpc).bootstrap()

    def network_cleanup(self, args):
        custom_payload = json.loads(args.custom_payload)
        dest_vpc_id = custom_payload.get("destVpcId")
        host_vpc_id = custom_payload.get("hostVpcId")
        self.get_admin().network(
            dest_vpc_id, host_vpc_id, per_region_meta=self.get_per_region_meta(args)).cleanup()
        return {"success": "VPC deleted."}

    def get_host_info(self, args, get_all=False, filters=None):
        """Override to call the respective GCP specific API for returning hosts by name.

        Required fields in args:
          zone: the zone to search in
          search_pattern: the regex or direct name to search hosts by
        """
        zone = args.zone
        search_pattern = args.search_pattern
        return self.get_admin().get_instances(zone, search_pattern, get_all, filters=filters)

    def get_device_names(self, args):
        # Boot disk is also a persistent disk, so add persistent disks starting at index 1
        if args.volume_type == GCP_SCRATCH:
            disk_name = "local-ssd"
            first_disk = 0
        else:
            disk_name = "persistent-disk"
            first_disk = 1
        return ["disk/by-id/google-{}-{}".format(
            disk_name, first_disk + i) for i in range(args.num_volumes)]

    def update_disk(self, args):
        instance = self.get_host_info(args)
        self.get_admin().update_disk(args, instance['id'])

    def change_instance_type(self, args, newInstanceType):
        self.get_admin().change_instance_type(args['zone'], args['search_pattern'], newInstanceType)

    def get_per_region_meta(self, args):
        if hasattr(args, "custom_payload") and args.custom_payload:
            metadata = json.loads(args.custom_payload).get("perRegionMetadata")
            if metadata:
                return metadata
        return {}

    def get_console_output(self, args):
        return self.get_admin().get_console_output(args.zone, args.search_pattern)

    def delete_volumes(self, args):
        tags = json.loads(args.instance_tags) if args.instance_tags is not None else {}
        return self.get_admin().delete_disks(args.zone, tags, args.volume_id)

    def modify_tags(self, args):
        instance = self.get_host_info(args)
        self.get_admin().modify_tags(args, instance['id'], args.instance_tags, args.remove_tags)

    def normalize_instance_state(self, instance_state):
        if instance_state:
            instance_state = instance_state.lower()
            if instance_state in ("provisioning", "staging", "repairing"):
                return InstanceState.STARTING
            if instance_state in ("running"):
                return InstanceState.RUNNING
            if instance_state in ("suspending", "suspended", "stopping"):
                return InstanceState.STOPPING
            if instance_state in ("terminated"):
                return InstanceState.STOPPED
        return InstanceState.UNKNOWN
