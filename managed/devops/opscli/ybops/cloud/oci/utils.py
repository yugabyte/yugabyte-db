# Copyright 2026 YugabyteDB, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

import base64
import json
import logging
import os
import re
import time
import requests

from ybops.common.exceptions import YBOpsRuntimeError, YBOpsRecoverableError
from ybops.cloud.common.utils import request_retry_decorator
from ybops.utils import DNS_RECORD_SET_TTL, MIN_MEM_SIZE_GB, MIN_NUM_CORES
from ybops.utils.ssh import format_rsa_key, validated_key_file
from threading import Thread

import oci
from oci.core import ComputeClient, VirtualNetworkClient, BlockstorageClient
from oci.identity import IdentityClient
from oci.core.models import (
    CaptureConsoleHistoryDetails,
    LaunchInstanceDetails,
    CreateVnicDetails,
    LaunchInstanceShapeConfigDetails,
    InstanceSourceViaImageDetails,
    CreateVolumeDetails,
    AttachVolumeDetails,
    AttachParavirtualizedVolumeDetails,
    CreateSubnetDetails,
    CreateVcnDetails,
    CreateSecurityListDetails,
    IngressSecurityRule,
    EgressSecurityRule,
    TcpOptions,
    PortRange,
    UpdateInstanceDetails,
    UpdateInstanceShapeConfigDetails
)

OCI_TENANCY_ID_ENV = "OCI_TENANCY_ID"
OCI_USER_ID_ENV = "OCI_USER_ID"
OCI_FINGERPRINT_ENV = "OCI_FINGERPRINT"
OCI_PRIVATE_KEY_CONTENT_ENV = "OCI_PRIVATE_KEY_CONTENT"
OCI_REGION_ENV = "OCI_REGION"
OCI_COMPARTMENT_ID_ENV = "OCI_COMPARTMENT_ID"
OCI_AUTH_TYPE_ENV = "OCI_AUTH_TYPE"

OCI_VOLUME_TYPE_STANDARD = "standard"
OCI_VOLUME_TYPE_HIGH_PERFORMANCE = "high_performance"
OCI_VOLUME_TYPE_ULTRA_HIGH_PERFORMANCE = "ultra_high_performance"
OCI_VOLUME_TYPE_BALANCED = "oci_balanced"
OCI_VOLUME_TYPE_HIGHER_PERF = "oci_higherperformance"
OCI_VOLUME_TYPE_LOWER_COST = "oci_lowercost"

OCI_INSTANCE_RUNNING = "RUNNING"
OCI_INSTANCE_STOPPED = "STOPPED"
OCI_INSTANCE_STOPPING = "STOPPING"
OCI_INSTANCE_STARTING = "STARTING"
OCI_INSTANCE_PROVISIONING = "PROVISIONING"
OCI_INSTANCE_TERMINATED = "TERMINATED"
OCI_INSTANCE_TERMINATING = "TERMINATING"

YUGABYTE_VCN_PREFIX = "yugabyte-vcn-{}"
YUGABYTE_SUBNET_PREFIX = "yugabyte-subnet-{}"
YUGABYTE_SG_PREFIX = "yugabyte-sl-{}"

DEFAULT_BOOT_VOLUME_SIZE_GB = 50
MIN_BOOT_VOLUME_SIZE_GB = 50

# Max length of a single DNS label per RFC 1035.
MAX_DNS_LABEL_LENGTH = 63


def oci_instance_action_conflict_handler(e):
    """OCI returns 409 when instance_action is called during background modification."""
    return isinstance(e, oci.exceptions.ServiceError) and e.status == 409


def oci_instance_action_conflict_retry(fn):
    return request_retry_decorator(fn, oci_instance_action_conflict_handler)


def sanitize_dns_label(name, max_length=MAX_DNS_LABEL_LENGTH):
    """Convert an arbitrary instance name into a valid OCI VNIC hostname label.

    OCI hostname labels back the VCN-internal DNS records and must comply with
    RFC 952/1123: only alphanumeric characters and hyphens, must start with a
    letter, must not end with a hyphen, and be at most 63 characters long.
    Returns None if no valid label can be derived from the given name.
    """
    if not name:
        return None
    label = re.sub(r"[^a-zA-Z0-9-]", "-", name).lower()
    # Collapse runs of hyphens and trim leading/trailing ones.
    label = re.sub(r"-+", "-", label).strip("-")
    if not label:
        return None
    # Labels must begin with a letter.
    if not label[0].isalpha():
        label = "h" + label
    return label[:max_length].rstrip("-") or None


def uses_instance_principal():
    return os.environ.get(OCI_AUTH_TYPE_ENV, "").upper() == "INSTANCE_PRINCIPAL"


def get_oci_config():
    region = os.environ.get(OCI_REGION_ENV)

    if uses_instance_principal():
        if not region:
            raise YBOpsRuntimeError(
                "OCI_REGION is required when using instance principal authentication.")
        return {"region": region}

    tenancy_id = os.environ.get(OCI_TENANCY_ID_ENV)
    user_id = os.environ.get(OCI_USER_ID_ENV)
    fingerprint = os.environ.get(OCI_FINGERPRINT_ENV)
    private_key_content = os.environ.get(OCI_PRIVATE_KEY_CONTENT_ENV)

    if tenancy_id and user_id and fingerprint and private_key_content and region:
        config = {
            "user": user_id,
            "fingerprint": fingerprint,
            "tenancy": tenancy_id,
            "region": region,
            "key_content": private_key_content
        }

        try:
            oci.config.validate_config(config)
        except Exception as e:
            raise YBOpsRuntimeError(
                "Invalid OCI configuration: {}".format(str(e)))

        return config

    raise YBOpsRuntimeError(
        "OCI configuration not found. Set environment variables "
        "(OCI_TENANCY_ID, OCI_USER_ID, OCI_FINGERPRINT, "
        "OCI_PRIVATE_KEY_CONTENT, "
        "OCI_REGION, OCI_COMPARTMENT_ID) or set OCI_AUTH_TYPE=INSTANCE_PRINCIPAL "
        "when running on an OCI instance with instance principal.")


def get_compartment_id():
    compartment_id = os.environ.get(OCI_COMPARTMENT_ID_ENV)
    if compartment_id:
        return compartment_id
    config = get_oci_config()
    return config.get("tenancy")


class OciCloudAdmin:

    def __init__(self, metadata=None):
        self.metadata = metadata or {}
        self._config = None
        self._compute_client = None
        self._network_client = None
        self._blockstorage_client = None
        self._identity_client = None
        self._compartment_id = None

    @property
    def config(self):
        if self._config is None:
            self._config = get_oci_config()
        return self._config

    def _build_client(self, client_class):
        if uses_instance_principal():
            signer = oci.auth.signers.InstancePrincipalsSecurityTokenSigner()
            return client_class(self.config, signer=signer)
        return client_class(self.config)

    @property
    def compartment_id(self):
        if self._compartment_id is None:
            self._compartment_id = get_compartment_id()
        return self._compartment_id

    @property
    def compute_client(self):
        if self._compute_client is None:
            self._compute_client = self._build_client(ComputeClient)
        return self._compute_client

    @property
    def network_client(self):
        if self._network_client is None:
            self._network_client = self._build_client(VirtualNetworkClient)
        return self._network_client

    @property
    def blockstorage_client(self):
        if self._blockstorage_client is None:
            self._blockstorage_client = self._build_client(BlockstorageClient)
        return self._blockstorage_client

    @property
    def identity_client(self):
        if self._identity_client is None:
            self._identity_client = self._build_client(IdentityClient)
        return self._identity_client

    def set_region(self, region):
        self.config["region"] = region
        self._compute_client = None
        self._network_client = None
        self._blockstorage_client = None
        self._identity_client = None

    def get_regions(self):
        regions = self.identity_client.list_regions()
        return [r.name for r in regions.data]

    def get_availability_domains(self, compartment_id=None):
        comp_id = compartment_id or self.compartment_id
        ads = self.identity_client.list_availability_domains(comp_id)
        return [ad.name for ad in ads.data]

    def resolve_availability_domain(self, ad_name, compartment_id=None):
        comp_id = compartment_id or self.compartment_id
        ads = self.identity_client.list_availability_domains(comp_id).data
        if ad_name.startswith('ocid1.'):
            return ad_name
        target = ad_name.lower()
        # Already in the "<prefix>:<REGION>-AD-N" shape; normalize against the API list.
        if ':' in ad_name:
            for ad in ads:
                if ad.name.lower() == target:
                    return ad.name
            # Fall through; let it raise below.
        # Match "Availability Domain N" or "...-AD-N" by AD number, case-insensitive.
        suffix = None
        if target.startswith("availability domain "):
            suffix = "-ad-" + target.split()[-1]
        elif "-ad-" in target:
            suffix = "-ad-" + target.rsplit("-ad-", 1)[1]
        if suffix:
            for ad in ads:
                if ad.name.lower().endswith(suffix):
                    return ad.name
        # Last resort: exact case-insensitive match against the full API name.
        for ad in ads:
            if ad.name.lower() == target:
                return ad.name
        raise YBOpsRuntimeError(
            "Could not resolve OCI availability domain '{}' in compartment {}. "
            "Available: {}".format(ad_name, comp_id, [ad.name for ad in ads]))

    def get_fault_domains(self, availability_domain, compartment_id=None):
        comp_id = compartment_id or self.compartment_id
        fds = self.identity_client.list_fault_domains(comp_id, availability_domain)
        return [fd.name for fd in fds.data]

    def get_shapes(self, compartment_id=None, availability_domain=None):
        comp_id = compartment_id or self.compartment_id
        shapes = self.compute_client.list_shapes(comp_id, availability_domain=availability_domain)
        return shapes.data

    def get_instance_types(self, region=None):
        if region:
            self.set_region(region)

        shapes = self.get_shapes()
        result = {}
        for shape in shapes:
            ocpus = getattr(shape, 'ocpus', None) or getattr(shape, 'ocpu_count', 0)
            memory_gb = getattr(shape, 'memory_in_gbs', 0)

            if ocpus and memory_gb:
                result[shape.shape] = {
                    "numCores": float(ocpus),
                    "memSizeGb": float(memory_gb),
                    "description": shape.shape,
                    "isShared": "Flex" in shape.shape or "Micro" in shape.shape
                }
        return result

    def get_images(self, compartment_id=None, operating_system=None, shape=None):
        comp_id = compartment_id or self.compartment_id
        images = self.compute_client.list_images(
            comp_id,
            operating_system=operating_system,
            shape=shape
        )
        return images.data

    def get_image(self, image_id):
        return self.compute_client.get_image(image_id).data

    def get_vcns(self, compartment_id=None):
        comp_id = compartment_id or self.compartment_id
        vcns = self.network_client.list_vcns(comp_id)
        return vcns.data

    def get_vcn(self, vcn_id):
        return self.network_client.get_vcn(vcn_id).data

    def get_subnets(self, compartment_id=None, vcn_id=None):
        comp_id = compartment_id or self.compartment_id
        subnets = self.network_client.list_subnets(comp_id, vcn_id=vcn_id)
        return subnets.data

    def get_subnet(self, subnet_id):
        return self.network_client.get_subnet(subnet_id).data

    def create_vcn(self, region, cidr_block="10.0.0.0/16", display_name=None):
        self.set_region(region)
        vcn_name = display_name or YUGABYTE_VCN_PREFIX.format(region)
        vcn_details = CreateVcnDetails(
            compartment_id=self.compartment_id,
            cidr_block=cidr_block,
            display_name=vcn_name,
            dns_label=vcn_name.replace("-", "")[:15]
        )
        vcn = self.network_client.create_vcn(vcn_details)
        return self._wait_for_resource_state(
            self.network_client.get_vcn, vcn.data.id, "AVAILABLE")

    def create_subnet(self, vcn_id, cidr_block, availability_domain=None, display_name=None):
        vcn = self.get_vcn(vcn_id)
        ad_or_default = availability_domain or "default"
        subnet_name = display_name or YUGABYTE_SUBNET_PREFIX.format(ad_or_default)

        subnet_details = CreateSubnetDetails(
            compartment_id=self.compartment_id,
            vcn_id=vcn_id,
            cidr_block=cidr_block,
            display_name=subnet_name,
            availability_domain=availability_domain,
            dns_label=subnet_name.replace("-", "")[:15]
        )
        subnet = self.network_client.create_subnet(subnet_details)
        return self._wait_for_resource_state(
            self.network_client.get_subnet, subnet.data.id, "AVAILABLE")

    def delete_vcn(self, vcn_id):
        self.network_client.delete_vcn(vcn_id)

    def delete_subnet(self, subnet_id):
        self.network_client.delete_subnet(subnet_id)

    def create_instance(self, region, availability_domain, subnet_id, instance_name,
                        shape, server_type, image_id, num_volumes, volume_size,
                        boot_volume_size_gb=None, assign_public_ip=True,
                        ssh_public_key=None, user_data=None, tags=None,
                        fault_domain=None, volume_type=OCI_VOLUME_TYPE_STANDARD,
                        ocpus=None, memory_in_gbs=None, node_uuid=None, **kwargs):
        self.set_region(region)

        availability_domain = self.resolve_availability_domain(availability_domain)

        metadata = {}
        if ssh_public_key:
            metadata["ssh_authorized_keys"] = ssh_public_key
        if user_data:
            metadata["user_data"] = base64.b64encode(user_data.encode()).decode()

        shape_config = None
        if "Flex" in shape:
            shape_config = LaunchInstanceShapeConfigDetails(
                ocpus=float(ocpus) if ocpus else 2.0,
                memory_in_gbs=float(memory_in_gbs) if memory_in_gbs else 16.0
            )

        actual_boot_size = max(boot_volume_size_gb or DEFAULT_BOOT_VOLUME_SIZE_GB,
                               MIN_BOOT_VOLUME_SIZE_GB)
        source_details = InstanceSourceViaImageDetails(
            image_id=image_id,
            boot_volume_size_in_gbs=actual_boot_size
        )

        # Assign a VCN-internal DNS hostname when the target subnet has DNS
        # enabled (i.e. has a dns_label). This makes OCI publish an A record so
        # the node is reachable via its FQDN in addition to its private IP.
        # Subnets without a dns_label reject hostname_label, so we skip it there.
        hostname_label = None
        try:
            subnet = self.network_client.get_subnet(subnet_id).data
            if getattr(subnet, "dns_label", None):
                hostname_label = sanitize_dns_label(instance_name)
        except Exception as e:
            logging.warning(
                "Could not look up subnet {} for DNS configuration: {}".format(subnet_id, e))

        vnic_details = CreateVnicDetails(
            subnet_id=subnet_id,
            assign_public_ip=assign_public_ip,
            display_name="{}-vnic".format(instance_name),
            hostname_label=hostname_label
        )

        freeform_tags = tags or {}
        freeform_tags["yb-server-type"] = server_type
        freeform_tags["Name"] = instance_name
        if node_uuid:
            freeform_tags["node-uuid"] = node_uuid

        launch_details = LaunchInstanceDetails(
            compartment_id=self.compartment_id,
            availability_domain=availability_domain,
            fault_domain=fault_domain,
            shape=shape,
            shape_config=shape_config,
            display_name=instance_name,
            source_details=source_details,
            create_vnic_details=vnic_details,
            metadata=metadata,
            freeform_tags=freeform_tags
        )

        response = self.compute_client.launch_instance(launch_details)
        instance = response.data
        created_volume_ids = []

        try:
            instance = self._wait_for_instance_state(instance.id, OCI_INSTANCE_RUNNING)

            if num_volumes and num_volumes > 0:
                volume_tags = dict(freeform_tags) if freeform_tags else {}
                for i in range(num_volumes):
                    volume = self.create_volume(
                        availability_domain=availability_domain,
                        size_in_gbs=volume_size,
                        display_name="{}-vol-{}".format(instance_name, i),
                        volume_type=volume_type,
                        tags=volume_tags
                    )
                    created_volume_ids.append(volume.id)
                    self.attach_volume(instance.id, volume.id)

            host_info = self._wait_for_instance_network(instance.id, instance_name, region)
            return host_info
        except Exception:
            logging.error(
                "Instance creation failed partially for {}, cleaning up instance {} "
                "and {} volume(s)".format(instance_name, instance.id, len(created_volume_ids)))
            for vol_id in created_volume_ids:
                try:
                    self.blockstorage_client.delete_volume(vol_id)
                except Exception as vol_err:
                    logging.warning(
                        "Failed to cleanup volume {}: {}".format(vol_id, vol_err))
            try:
                self.compute_client.terminate_instance(instance.id)
            except Exception as term_err:
                logging.warning(
                    "Failed to cleanup instance {}: {}".format(instance.id, term_err))
            raise

    def _wait_for_instance_network(self, instance_id, instance_name, region, timeout=120):
        start_time = time.time()
        while time.time() - start_time < timeout:
            instance = self.compute_client.get_instance(instance_id).data
            vnic_attachments = self.compute_client.list_vnic_attachments(
                self.compartment_id, instance_id=instance_id).data

            for attachment in vnic_attachments:
                if attachment.lifecycle_state == "ATTACHED":
                    vnic = self.network_client.get_vnic(attachment.vnic_id).data
                    if vnic.private_ip:
                        tags = instance.freeform_tags or {}
                        return {
                            "id": instance.id,
                            "name": instance.display_name,
                            "public_ip": vnic.public_ip,
                            "private_ip": vnic.private_ip,
                            "private_dns": self._get_private_dns(vnic),
                            "region": region,
                            "zone": instance.availability_domain,
                            "instance_type": instance.shape,
                            "server_type": tags.get("yb-server-type", "unknown"),
                            "instance_state": instance.lifecycle_state,
                            "subnet": vnic.subnet_id,
                            "node_uuid": tags.get("node-uuid"),
                            "is_running": instance.lifecycle_state == OCI_INSTANCE_RUNNING
                        }
            time.sleep(5)

        raise YBOpsRuntimeError(
            "Timeout waiting for VNIC to be attached for instance {}".format(instance_name))

    def get_instance(self, instance_id):
        return self.compute_client.get_instance(instance_id).data

    def _get_private_dns(self, vnic, subnet_cache=None):
        """Build the VCN-internal FQDN for a VNIC, or None if DNS is not enabled.

        OCI exposes the fully qualified domain name as
        <hostname_label>.<subnet_domain_name>, where subnet_domain_name already
        includes the subnet DNS label, the VCN DNS label and the oraclevcn.com
        domain (e.g. "subnet1.vcn1.oraclevcn.com"). Returns None when the VNIC
        has no hostname label or the subnet has DNS disabled.
        """
        hostname_label = getattr(vnic, "hostname_label", None)
        if not hostname_label:
            return None

        subnet_id = vnic.subnet_id
        subnet_domain_name = None
        if subnet_cache is not None and subnet_id in subnet_cache:
            subnet_domain_name = subnet_cache[subnet_id]
        else:
            try:
                subnet = self.network_client.get_subnet(subnet_id).data
                subnet_domain_name = getattr(subnet, "subnet_domain_name", None)
            except Exception as e:
                logging.warning(
                    "Could not look up subnet {} for DNS name: {}".format(subnet_id, e))
            if subnet_cache is not None:
                subnet_cache[subnet_id] = subnet_domain_name

        if not subnet_domain_name:
            return None
        return "{}.{}".format(hostname_label, subnet_domain_name.rstrip("."))

    def get_instances(self, region=None, search_pattern=None, compartment_id=None,
                      get_all=False, node_uuid=None):
        if region:
            self.set_region(region)

        comp_id = compartment_id or self.compartment_id
        if get_all:
            instances = self.compute_client.list_instances(comp_id)
        else:
            instances = self.compute_client.list_instances(
                comp_id,
                lifecycle_state=OCI_INSTANCE_RUNNING
            )

        results = []
        subnet_cache = {}
        for instance in instances.data:
            if search_pattern and search_pattern not in instance.display_name:
                continue

            if node_uuid:
                instance_uuid = (instance.freeform_tags or {}).get("node-uuid")
                if instance_uuid != node_uuid:
                    continue

            if instance.lifecycle_state in (OCI_INSTANCE_TERMINATED, OCI_INSTANCE_TERMINATING):
                continue

            vnic_attachments = self.compute_client.list_vnic_attachments(
                comp_id, instance_id=instance.id).data

            private_ip = None
            public_ip = None
            subnet_id = None
            private_dns = None

            for attachment in vnic_attachments:
                if attachment.lifecycle_state == "ATTACHED":
                    vnic = self.network_client.get_vnic(attachment.vnic_id).data
                    private_ip = vnic.private_ip
                    public_ip = vnic.public_ip
                    subnet_id = vnic.subnet_id
                    private_dns = self._get_private_dns(vnic, subnet_cache=subnet_cache)
                    break

            inst_tags = instance.freeform_tags or {}
            host_info = {
                "id": instance.id,
                "name": instance.display_name,
                "public_ip": public_ip,
                "private_ip": private_ip,
                "private_dns": private_dns,
                "region": instance.region,
                "zone": instance.availability_domain,
                "instance_type": instance.shape,
                "server_type": inst_tags.get("yb-server-type", "unknown"),
                "instance_state": instance.lifecycle_state,
                "subnet": subnet_id,
                "node_uuid": inst_tags.get("node-uuid"),
                "is_running": instance.lifecycle_state == OCI_INSTANCE_RUNNING
            }
            results.append(host_info)

        if not get_all and results:
            return results[0]
        return results if get_all else None

    def terminate_instance(self, instance_id, preserve_boot_volume=False,
                           delete_data_volumes=True):
        # Delete attached data volumes before terminating instance
        # Need to do this manually
        if delete_data_volumes:
            self._delete_attached_volumes(instance_id)

        self.compute_client.terminate_instance(
            instance_id,
            preserve_boot_volume=preserve_boot_volume
        )
        self._wait_for_instance_state(instance_id, OCI_INSTANCE_TERMINATED, allow_not_found=True)

    def _delete_attached_volumes(self, instance_id):
        """Delete all block volumes attached to an instance (not boot volume)."""
        try:
            attachments = self.get_volume_attachments(instance_id=instance_id)
            for attachment in attachments:
                if attachment.lifecycle_state == "ATTACHED":
                    volume_id = attachment.volume_id
                    logging.info("Detaching and deleting volume {} from instance {}".format(
                        volume_id, instance_id))
                    try:
                        self.detach_volume(attachment.id)
                        self._wait_for_attachment_detached(attachment.id)
                        self.delete_volume(volume_id)
                        logging.info("Deleted volume {}".format(volume_id))
                    except Exception as e:
                        logging.warning("Failed to delete volume {}: {}".format(volume_id, e))
        except Exception as e:
            logging.warning("Failed to list volume attachments for instance {}: {}".format(
                instance_id, e))

    def _wait_for_attachment_detached(self, attachment_id, timeout=120):
        """Wait for a volume attachment to be detached."""
        start_time = time.time()
        while time.time() - start_time < timeout:
            try:
                attachment = self.compute_client.get_volume_attachment(attachment_id).data
                if attachment.lifecycle_state == "DETACHED":
                    return
            except oci.exceptions.ServiceError as e:
                if e.status == 404:
                    return  # Attachment already gone
                raise
            time.sleep(5)
        logging.warning("Timeout waiting for attachment {} to detach".format(attachment_id))

    def stop_instance(self, instance_id):
        self.compute_client.instance_action(instance_id, "STOP")
        return self._wait_for_instance_state(instance_id, OCI_INSTANCE_STOPPED)

    @oci_instance_action_conflict_retry
    def _invoke_instance_action(self, instance_id, action):
        self.compute_client.instance_action(instance_id, action)

    def start_instance(self, instance_id):
        self._invoke_instance_action(instance_id, "START")
        return self._wait_for_instance_state(instance_id, OCI_INSTANCE_RUNNING)

    def reboot_instance(self, instance_id):
        self.compute_client.instance_action(instance_id, "RESET")
        return self._wait_for_instance_state(instance_id, OCI_INSTANCE_RUNNING)

    def change_instance_type(self, instance_id, new_shape, ocpus=None, memory_in_gbs=None):
        shape_config = None
        if "Flex" in new_shape and (ocpus or memory_in_gbs):
            shape_config = UpdateInstanceShapeConfigDetails(
                ocpus=ocpus,
                memory_in_gbs=memory_in_gbs
            )

        update_details = UpdateInstanceDetails(
            shape=new_shape,
            shape_config=shape_config
        )
        self.compute_client.update_instance(instance_id, update_details)
        return self._wait_for_instance_state(
            instance_id,
            OCI_INSTANCE_STOPPED,
            ready_check=lambda instance: instance.shape == new_shape)

    def create_volume(self, availability_domain, size_in_gbs, display_name=None,
                      volume_type=OCI_VOLUME_TYPE_BALANCED, vpus_per_gb=None, tags=None):
        if vpus_per_gb is None:
            if volume_type in (OCI_VOLUME_TYPE_HIGH_PERFORMANCE, OCI_VOLUME_TYPE_HIGHER_PERF):
                vpus_per_gb = 20
            elif volume_type == OCI_VOLUME_TYPE_ULTRA_HIGH_PERFORMANCE:
                vpus_per_gb = 30
            elif volume_type == OCI_VOLUME_TYPE_LOWER_COST:
                vpus_per_gb = 0
            else:
                vpus_per_gb = 10

        volume_details = CreateVolumeDetails(
            compartment_id=self.compartment_id,
            availability_domain=availability_domain,
            size_in_gbs=size_in_gbs,
            display_name=display_name,
            vpus_per_gb=vpus_per_gb,
            freeform_tags=tags
        )
        response = self.blockstorage_client.create_volume(volume_details)
        return self._wait_for_volume_state(response.data.id, "AVAILABLE")

    def attach_volume(self, instance_id, volume_id, device_path=None):
        attach_details = AttachParavirtualizedVolumeDetails(
            instance_id=instance_id,
            volume_id=volume_id,
            display_name="attachment-{}".format(volume_id[-8:]),
            device=device_path,
            is_read_only=False,
            is_shareable=False
        )
        response = self.compute_client.attach_volume(attach_details)
        return self._wait_for_attachment_state(response.data.id, "ATTACHED")

    def detach_volume(self, attachment_id):
        self.compute_client.detach_volume(attachment_id)

    def delete_volume(self, volume_id):
        self.blockstorage_client.delete_volume(volume_id)

    def get_volume_attachments(self, instance_id=None, compartment_id=None):
        comp_id = compartment_id or self.compartment_id
        return self.compute_client.list_volume_attachments(
            comp_id, instance_id=instance_id).data

    def update_volume_size(self, volume_id, new_size_in_gbs):
        from oci.core.models import UpdateVolumeDetails
        update_details = UpdateVolumeDetails(size_in_gbs=new_size_in_gbs)
        self.blockstorage_client.update_volume(volume_id, update_details)

    def list_volumes_by_tags(self, tags, compartment_id=None):
        comp_id = compartment_id or self.compartment_id
        volumes = self.blockstorage_client.list_volumes(compartment_id=comp_id).data
        matching_volumes = []
        for volume in volumes:
            if volume.lifecycle_state not in ("AVAILABLE", "PROVISIONING"):
                continue
            if volume.freeform_tags:
                matches = all(
                    volume.freeform_tags.get(k) == v for k, v in tags.items()
                )
                if matches:
                    matching_volumes.append(volume)
        return matching_volumes

    def modify_tags(self, instance_id, tags_to_add=None, tags_to_remove=None):
        instance = self.get_instance(instance_id)
        current_tags = dict(instance.freeform_tags) if instance.freeform_tags else {}

        if tags_to_remove:
            for tag in tags_to_remove:
                current_tags.pop(tag, None)

        if tags_to_add:
            current_tags.update(tags_to_add)

        update_details = UpdateInstanceDetails(freeform_tags=current_tags)
        self.compute_client.update_instance(instance_id, update_details)

    def get_console_history(self, instance_id):
        details = CaptureConsoleHistoryDetails(instance_id=instance_id)
        response = self.compute_client.capture_console_history(details)

        history_id = response.data.id
        for _ in range(30):
            history = self.compute_client.get_console_history(history_id).data
            if history.lifecycle_state == "SUCCEEDED":
                content = self.compute_client.get_console_history_content(history_id).data
                return content.decode() if isinstance(content, bytes) else content
            time.sleep(2)
        return ""

    def _wait_for_instance_state(
            self, instance_id, target_state, timeout=600, allow_not_found=False,
            ready_check=None):
        start_time = time.time()
        while time.time() - start_time < timeout:
            try:
                instance = self.compute_client.get_instance(instance_id).data
                if instance.lifecycle_state == target_state:
                    if ready_check is None or ready_check(instance):
                        return instance
                if instance.lifecycle_state == OCI_INSTANCE_TERMINATED:
                    if allow_not_found:
                        return None
                    raise YBOpsRuntimeError(
                        "Instance {} terminated unexpectedly".format(instance_id))
            except oci.exceptions.ServiceError as e:
                if e.status == 404 and allow_not_found:
                    return None
                raise
            time.sleep(10)
        raise YBOpsRuntimeError(
            "Timeout waiting for instance {} to reach state {}".format(
                instance_id, target_state))

    def _wait_for_volume_state(self, volume_id, target_state, timeout=300):
        start_time = time.time()
        while time.time() - start_time < timeout:
            volume = self.blockstorage_client.get_volume(volume_id).data
            if volume.lifecycle_state == target_state:
                return volume
            time.sleep(5)
        raise YBOpsRuntimeError(
            "Timeout waiting for volume {} to reach state {}".format(volume_id, target_state))

    def _wait_for_attachment_state(self, attachment_id, target_state, timeout=300):
        start_time = time.time()
        while time.time() - start_time < timeout:
            attachment = self.compute_client.get_volume_attachment(attachment_id).data
            if attachment.lifecycle_state == target_state:
                return attachment
            time.sleep(5)
        raise YBOpsRuntimeError(
            "Timeout waiting for attachment {} to reach state {}".format(
                attachment_id, target_state))

    def _wait_for_resource_state(self, get_func, resource_id, target_state, timeout=300):
        start_time = time.time()
        while time.time() - start_time < timeout:
            resource = get_func(resource_id).data
            if resource.lifecycle_state == target_state:
                return resource
            time.sleep(5)
        raise YBOpsRuntimeError(
            "Timeout waiting for resource {} to reach state {}".format(resource_id, target_state))

    def network(self, per_region_meta=None):
        return OciNetworkManager(self, per_region_meta)


class OciNetworkManager:

    def __init__(self, admin, per_region_meta=None):
        self.admin = admin
        self.per_region_meta = per_region_meta or {}

    def bootstrap(self, region):
        self.admin.set_region(region)

        vcn_id = self.per_region_meta.get("vpcId")
        if not vcn_id:
            vcn = self.admin.create_vcn(region)
            vcn_id = vcn.id

        subnet_id = None
        az_to_subnets = self.per_region_meta.get("azToSubnetIds", {})
        if not az_to_subnets:
            vcn = self.admin.get_vcn(vcn_id)
            subnet = self.admin.create_subnet(
                vcn_id,
                cidr_block="10.0.1.0/24"
            )
            subnet_id = subnet.id
            az_to_subnets["default"] = subnet_id

        return {
            "vpcId": vcn_id,
            "azToSubnetIds": az_to_subnets,
            "customSecurityGroupId": self.per_region_meta.get("customSecurityGroupId")
        }

    def cleanup(self, region):
        self.admin.set_region(region)

        vcn_id = self.per_region_meta.get("vpcId")
        if vcn_id:
            subnets = self.admin.get_subnets(vcn_id=vcn_id)
            for subnet in subnets:
                try:
                    self.admin.delete_subnet(subnet.id)
                except Exception as e:
                    logging.warning("Failed to delete subnet {}: {}".format(subnet.id, str(e)))

            try:
                self.admin.delete_vcn(vcn_id)
            except Exception as e:
                logging.warning("Failed to delete VCN {}: {}".format(vcn_id, str(e)))

    def get_zone_to_subnets(self, vcn_id, region):
        self.admin.set_region(region)

        result = {"zones": [], "subnetworks": {}}

        ads = self.admin.get_availability_domains()
        result["zones"] = ads

        if vcn_id:
            subnets = self.admin.get_subnets(vcn_id=vcn_id)
            for subnet in subnets:
                ad = subnet.availability_domain or "regional"
                if ad not in result["subnetworks"]:
                    result["subnetworks"][ad] = []
                result["subnetworks"][ad].append({
                    "id": subnet.id,
                    "name": subnet.display_name,
                    "cidr": subnet.cidr_block
                })

        return result

    def to_components(self):
        return {
            "vpcId": self.per_region_meta.get("vpcId"),
            "azToSubnetIds": self.per_region_meta.get("azToSubnetIds", {}),
            "customSecurityGroupId": self.per_region_meta.get("customSecurityGroupId")
        }


class OciMetadata:

    METADATA_URL = "http://169.254.169.254/opc/v2"
    METADATA_HEADERS = {"Authorization": "Bearer Oracle"}

    @classmethod
    def get_metadata(cls, path=""):
        try:
            url = "{}/{}".format(cls.METADATA_URL, path.lstrip("/"))
            response = requests.get(url, headers=cls.METADATA_HEADERS, timeout=3)
            response.raise_for_status()
            return response.json()
        except requests.exceptions.ConnectionError:
            logging.info("Not running on OCI instance (metadata service unreachable)")
            return None
        except requests.exceptions.Timeout:
            logging.info("OCI metadata service timed out")
            return None

    @classmethod
    def get_instance_info(cls):
        return cls.get_metadata("instance")

    @classmethod
    def get_region(cls):
        info = cls.get_instance_info()
        return info.get("region") if info else None

    @classmethod
    def get_availability_domain(cls):
        info = cls.get_instance_info()
        return info.get("availabilityDomain") if info else None
