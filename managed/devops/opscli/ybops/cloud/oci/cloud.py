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

    def create_instance(self, args, server_type, ssh_keys):
        machine_image = getattr(args, 'machine_image', None)
        if not machine_image:
            machine_image = self.get_image(args.region, shape=args.instance_type)

        if not machine_image:
            raise YBOpsRuntimeError(
                "No OCI image available for region {} compatible with shape {}. "
                "Please configure an image bundle with OCI images or ensure "
                "Oracle Linux images are available.".format(args.region, args.instance_type))

        logging.info("Creating OCI instance with image: {}".format(machine_image))

        tags = {}
        if args.instance_tags:
            tags = json.loads(args.instance_tags)

        ocpus = getattr(args, 'ocpus', None)
        memory_in_gbs = getattr(args, 'memory_in_gbs', None)

        host_info = self.get_admin().create_instance(
            region=args.region,
            availability_domain=args.zone,
            subnet_id=args.cloud_subnet,
            instance_name=args.search_pattern,
            shape=args.instance_type,
            server_type=server_type,
            image_id=machine_image,
            num_volumes=args.num_volumes,
            volume_size=args.volume_size,
            boot_volume_size_gb=args.boot_disk_size_gb,
            assign_public_ip=args.assign_public_ip,
            ssh_public_key=ssh_keys,
            user_data=self._get_user_data(args),
            tags=tags,
            volume_type=getattr(args, 'volume_type', OCI_VOLUME_TYPE_STANDARD),
            ocpus=ocpus,
            memory_in_gbs=memory_in_gbs,
            node_uuid=getattr(args, 'node_uuid', None)
        )
        return host_info

    def _get_user_data(self, args):
        if hasattr(args, 'boot_script') and args.boot_script:
            try:
                with open(args.boot_script, 'r') as f:
                    return f.read()
            except Exception as e:
                logging.warning("Failed to read boot script: {}".format(e))
        return None

    def destroy_instance(self, args):
        host_info = self.get_host_info(args)
        if host_info is None:
            logging.error("Host {} does not exist.".format(args.search_pattern))
            return

        if args.node_ip is not None:
            if host_info.get('private_ip') != args.node_ip:
                logging.error("Host {} IP does not match.".format(args.search_pattern))
                return
        elif args.node_uuid is not None:
            if host_info.get('node_uuid') != args.node_uuid:
                logging.error("Host {} UUID does not match.".format(args.search_pattern))
                return

        self.get_admin().terminate_instance(host_info['id'])

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

    def start_instance(self, host_info, server_ports):
        instance_id = host_info['id']
        instance = self.get_admin().get_instance(instance_id)

        if instance.lifecycle_state == OCI_INSTANCE_RUNNING:
            logging.info("Instance {} is already running".format(host_info['name']))
        elif instance.lifecycle_state == OCI_INSTANCE_STOPPED:
            self.get_admin().start_instance(instance_id)
        else:
            raise YBOpsRuntimeError(
                "Instance {} cannot be started from state {}".format(
                    host_info['name'], instance.lifecycle_state))

        updated_info = self.get_admin().get_instances(
            search_pattern=host_info['name'],
            get_all=False
        )

        if updated_info:
            ssh_host = updated_info['private_ip']
            self.wait_for_server_ports(
                ssh_host,
                host_info['name'],
                server_ports
            )
        return updated_info

    def stop_instance(self, host_info):
        instance_id = host_info['id']
        instance = self.get_admin().get_instance(instance_id)

        if instance.lifecycle_state == OCI_INSTANCE_STOPPED:
            logging.info("Instance {} is already stopped".format(host_info['name']))
        elif instance.lifecycle_state == OCI_INSTANCE_RUNNING:
            self.get_admin().stop_instance(instance_id)
        else:
            raise YBOpsRuntimeError(
                "Instance {} cannot be stopped from state {}".format(
                    host_info['name'], instance.lifecycle_state))

    def reboot_instance(self, host_info, server_ports):
        self.get_admin().reboot_instance(host_info['id'])
        ssh_host = host_info['private_ip']
        self.wait_for_server_ports(
            ssh_host,
            host_info['name'],
            server_ports
        )

    def change_instance_type(self, host_info, instance_type, ocpus=None, memory_in_gbs=None):
        self.get_admin().change_instance_type(
            host_info['id'],
            instance_type,
            ocpus=ocpus,
            memory_in_gbs=memory_in_gbs
        )

    def update_disk(self, args):
        host_info = self.get_host_info(args)
        if not host_info:
            raise YBOpsRuntimeError("Could not find instance {}".format(args.search_pattern))

        attachments = self.get_admin().get_volume_attachments(instance_id=host_info['id'])
        for attachment in attachments:
            if attachment.lifecycle_state == "ATTACHED":
                self.get_admin().update_volume_size(attachment.volume_id, args.volume_size)

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

        filter_tags = {
            'universe-uuid': universe_uuid,
            'node-uuid': node_uuid
        }

        volumes = self.get_admin().list_volumes_by_tags(filter_tags)
        deleted_count = 0

        for volume in volumes:
            if volume.lifecycle_state == "AVAILABLE":
                try:
                    logging.info("Deleting volume: {} ({})".format(
                        volume.display_name, volume.id))
                    self.get_admin().delete_volume(volume.id)
                    deleted_count += 1
                except Exception as e:
                    logging.warning(
                        "Failed to delete volume {}: {}. "
                        "deletion can fail due to errors, "
                        "Use `--force` to ignore errors and force delete.".format(
                            volume.id, e))

        logging.info("Deleted {} volumes for node {}".format(deleted_count, node_uuid))

    def modify_tags(self, args):
        host_info = self.get_host_info(args)
        if not host_info:
            raise YBOpsRuntimeError("Could not find instance {}".format(args.search_pattern))

        tags_to_add = json.loads(args.instance_tags) if args.instance_tags else None
        tags_to_remove = None
        if hasattr(args, 'remove_tags') and args.remove_tags:
            tags_to_remove = args.remove_tags.split(",")

        self.get_admin().modify_tags(host_info['id'], tags_to_add, tags_to_remove)

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

    def expand_file_system(self, args, connect_options):
        from ybops.utils.remote_shell import RemoteShell
        remote_shell = RemoteShell(connect_options)
        mount_points = self.get_mount_points_csv(args).split(',')
        for mount_point in mount_points:
            logging.info("Expanding file system with mount point: {}".format(mount_point))
            remote_shell.check_exec_command('sudo xfs_growfs {}'.format(mount_point))

    def clone_disk(self, args, volume_id, num_disks):
        raise YBOpsRuntimeError("Disk cloning not yet implemented for OCI")

    def mount_disk(self, args, body):
        instance_id = args.get('instance_id') or args.get('search_pattern')
        volume_id = body.get('volume_id')
        self.get_admin().attach_volume(instance_id, volume_id)

    def unmount_disk(self, args, attachment_id):
        self.get_admin().detach_volume(attachment_id)
