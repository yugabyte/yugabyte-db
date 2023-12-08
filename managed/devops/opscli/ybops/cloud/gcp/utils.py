# Copyright 2019 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

import logging
import os
import requests
import socket
import time
import json


from googleapiclient import discovery
from googleapiclient.errors import HttpError
import oauth2client
from six.moves import http_client

from ybops.common.exceptions import YBOpsRuntimeError
from ybops.cloud.common.utils import request_retry_decorator


RESOURCE_BASE_URL = "https://www.googleapis.com/compute/beta/projects/"
REGIONS_RESOURCE_URL_FORMAT = RESOURCE_BASE_URL + "{}/regions/{}"
PRICING_JSON_URL = "https://cloudpricingcalculator.appspot.com/static/data/pricelist.json"
IMAGE_NAME_PREFIX = "CP-COMPUTEENGINE-VMIMAGE-"
IMAGE_NAME_PREEMPTIBLE_SUFFIX = "-PREEMPTIBLE"
OPERATION_WAIT_TIME = 5
MAX_NUM_RETRY_COUNT = 120
LIST_MAX_RESULTS = 500


YB_NETWORK_NAME = "yb-gcp-network"
YB_SUBNET_FORMAT = "yb-subnet-{}"
YB_PEERING_CONNECTION_FORMAT = "yb-peering-{}-with-{}"
YB_FIREWALL_NAME = "yb-internal-firewall"
YB_FIREWALL_TARGET_TAGS = "cluster-server"

STARTUP_SCRIPT_META_KEY = "startup-script"
SERVER_TYPE_META_KEY = "server_type"
SSH_KEYS_META_KEY = "ssh-keys"
BLOCK_PROJECT_SSH_KEY = "block-project-ssh-keys"

META_KEYS = [STARTUP_SCRIPT_META_KEY, SERVER_TYPE_META_KEY, SSH_KEYS_META_KEY]

GCP_SCRATCH = "scratch"
GCP_PERSISTENT = "persistent"


# Code 429 does not have a name in httplib.
TOO_MANY_REQUESTS = 429


def gcp_exception_handler(e):
    """GCP specific exception handler. Modeled after:
    http://testcompany.info/google-cloud-sdk/lib/third_party/apitools/base/py/http_wrapper.py

    Args:
        e: the exception that was raised by the underlying API call that just failed.
    Returns:
        True if this exception can be retried, False otherwise.
    """
    # Transport failures
    if isinstance(e, (http_client.BadStatusLine,
                      http_client.IncompleteRead,
                      http_client.ResponseNotReady)):
        logging.warning('Caught HTTP error %s, retrying: %s', type(e).__name__, e)
    elif isinstance(e, socket.error):
        # Note: this also catches ssl.SSLError flavors of:
        # ssl.SSLError: ('The read operation timed out',)
        logging.warning('Caught socket error, retrying: %s', e)
    elif isinstance(e, socket.gaierror):
        logging.warning('Caught socket address error, retrying: %s', e)
    elif isinstance(e, ValueError):
        # oauth2client tries to JSON-decode the response, which can result
        # in a ValueError if the response was invalid. Until that is fixed in
        # oauth2client, need to handle it here.
        logging.warning('Response content was invalid (%s), retrying', e)
    elif (isinstance(e, oauth2client.client.HttpAccessTokenRefreshError) and
          (e.status == TOO_MANY_REQUESTS or
           e.status >= 500)):
        logging.warning('Caught transient credential refresh error (%s), retrying', e)
    elif (isinstance(e, HttpError) and
          (e.resp.status == TOO_MANY_REQUESTS or
           e.resp.status >= 500)):
        logging.warning('Caught transient server error (%s), retrying', e)
    else:
        return False
    return True


def gcp_request_limit_retry(fn):
    """A decorator for retrying GCP operations in case of expected transient errors.
    """
    return request_retry_decorator(fn, gcp_exception_handler)


def get_firewall_tags():
    return os.environ.get('YB_FIREWALL_TAGS', YB_FIREWALL_TARGET_TAGS).split(',')


class GcpMetadata():
    METADATA_URL_BASE = "http://metadata.google.internal/computeMetadata/v1"
    CUSTOM_HEADERS = {
        "Metadata-Flavor": "Google"
    }

    @staticmethod
    def _query_endpoint(endpoint):
        try:
            url = "{}/{}".format(GcpMetadata.METADATA_URL_BASE, endpoint)
            req = requests.get(url, headers=GcpMetadata.CUSTOM_HEADERS, timeout=2)

            if req.status_code != requests.codes.ok:
                logging.warning("Request {} returned http error code {}".format(
                    url, req.status_code))
                return None

            return req.content.decode('utf-8')

        except requests.exceptions.ConnectionError as e:
            logging.warning("Request {} had a connection error {}".format(url, str(e)))
            return None

    @staticmethod
    def project():
        return GcpMetadata._query_endpoint("project/project-id")

    @staticmethod
    def host_project():
        network_data = GcpMetadata._query_endpoint("instance/network-interfaces/0/network")
        try:
            # Network data is of format projects/PROJECT_NUMBER/networks/NETWORK_NAME
            return str(network_data).split('/')[1]
        except (IndexError, AttributeError):
            return None

    @staticmethod
    def network():
        return GcpMetadata._query_endpoint("instance/network-interfaces/0/network")

    @staticmethod
    def service_accounts():
        return GcpMetadata._query_endpoint("instance/service-accounts/")


class Waiter():
    def __init__(self, project, compute):
        self.project = project
        self.compute = compute

    def get_in_progress_operation(self, zone, instance, operation_type):
        target_link = \
            RESOURCE_BASE_URL + "{}/zones/{}/instances/{}".format(self.project, zone, instance)
        cmd = self.compute.zoneOperations().list(
            project=self.project,
            zone=zone,
            filter='targetLink = "{}" AND status != "DONE" AND '
                   'operationType = "{}"'.format(target_link, operation_type),
            maxResults=1)
        result = cmd.execute()
        if 'error' in result:
            raise RuntimeError(result['error'])
        if 'items' not in result or len(result['items']) == 0:
            return None
        return result['items'][0]['id']

    def wait(self, operation, region=None, zone=None):
        # This allows easier chaining of waits on functions that are NOOPs if items already exist.
        if operation is None:
            logging.warning("Returning waiting for a None Operation")
            return
        retry_count = 0
        name = operation["name"]
        while retry_count < MAX_NUM_RETRY_COUNT:
            if zone is not None:
                cmd = self.compute.zoneOperations().get(
                    project=self.project,
                    zone=zone,
                    operation=name)
            elif region is not None:
                cmd = self.compute.regionOperations().get(
                    project=self.project,
                    region=region,
                    operation=name)
            else:
                cmd = self.compute.globalOperations().get(
                    project=self.project,
                    operation=name)
            result = cmd.execute()

            if result['status'] == 'DONE':
                if 'error' in result:
                    raise YBOpsRuntimeError(result['error'])
                return result

            time.sleep(OPERATION_WAIT_TIME)
            retry_count += 1

        raise YBOpsRuntimeError("Operation {} timed out".format(name))


class NetworkManager():
    def __init__(self, project, compute, metadata, dest_vpc_id, host_vpc_id,
                 per_region_meta, create_new_vpc=False):
        self.project = project
        self.compute = compute
        self.metadata = metadata
        # This dictates if we want to provision a new network or just query an existing one.
        self.dest_vpc_id = dest_vpc_id
        # This will be the network we currently are in and that we will be peering with.
        # TODO: fix the network/project combos
        # Allow setting host_vpc_id and check if you do not have a target, meaning you are
        # asked to bootstrap, then the host's project ID matches self.project, else we'll try to
        # peer across projects...
        self.host_vpc_id = host_vpc_id if host_vpc_id is not None else GcpMetadata.network()
        if self.host_vpc_id is not None:
            self.host_vpc_id = self.host_vpc_id.split("/")[-1]
        self.per_region_meta = per_region_meta
        self.create_new_vpc = create_new_vpc

        self.waiter = Waiter(self.project, self.compute)

    def network_info_as_json(self, network_name, region_to_subnet_map):
        return {
            "network": network_name,
            "regions": region_to_subnet_map
        }

    def get_network_data(self, network_name):
        # If user has specified network data through per_region_meta, then do not validate any
        # data through GCP API and simply return the relevant fields.
        output_region_to_subnet_map = {}
        for region in self.per_region_meta.keys():
            output_region_to_subnet_map[region] = self.per_region_meta.get(
                region, {}).get("subnetId")

        if output_region_to_subnet_map:
            return self.network_info_as_json(network_name, output_region_to_subnet_map)

        networks = self.get_networks(network_name)
        if len(networks) == 0:
            raise YBOpsRuntimeError("Invalid target VPC: {}".format(network_name))
        network = networks[0]

        subnet_map = self.get_region_subnet_map(network.get("selfLink"))
        for region_name, scope in subnet_map.items():
            region = region_name.split("/")[1]
            subnets = [s["name"] for s in scope.get("subnetworks", [])]
            if len(subnets) > 0:
                output_region_to_subnet_map[region] = subnets
        return self.network_info_as_json(network_name, output_region_to_subnet_map)

    def bootstrap(self):
        # If create_new_vpc is not specified than use the specified the VPC.
        if self.dest_vpc_id and not self.create_new_vpc:
            # Try to fetch the specified network & fail in case it does not exist.
            networks = self.get_networks(self.dest_vpc_id)
            if len(networks) == 0:
                raise YBOpsRuntimeError("Invalid target VPC: {}".format(self.dest_vpc_id))
            return self.get_network_data(self.dest_vpc_id)
        # If create_new_vpc is specified we will be creating a new VPC with specified
        # name if not present, else we will fail.
        if self.dest_vpc_id:
            global YB_NETWORK_NAME
            YB_NETWORK_NAME = self.dest_vpc_id
        networks = self.get_networks(YB_NETWORK_NAME)
        if len(networks) > 0:
            raise YBOpsRuntimeError(
                "Failed to create VPC as vpc with same name already exists: {}"
                .format(YB_NETWORK_NAME))
        else:
            # Create the network if it didn't already exist.
            op = self.waiter.wait(self.create_network())
            network_url = op.get("targetLink")
        ops_by_region = {}
        # List all relevant subnets.
        output_region_to_subnet_map = {}
        subnet_map = self.get_region_subnet_map(network_url)
        for region_name, scope in subnet_map.items():
            region = region_name.split("/")[1]
            # Ignore regions not asked to bootstrap.
            if region not in self.metadata["regions"]:
                continue
            custom_subnet_name = YB_SUBNET_FORMAT.format(region)
            output_region_to_subnet_map[region] = [custom_subnet_name]
            subnet_names = [s["name"] for s in scope.get("subnetworks", [])]
            if custom_subnet_name not in subnet_names:
                ops_by_region[region] = self.create_subnetwork(
                    network_url, region, custom_subnet_name)
        # Wait for all created subnets.
        for r, op in ops_by_region.items():
            self.waiter.wait(op, region=r)
        peer_network_url = None
        # Setup the VPC peering.
        if self.host_vpc_id is not None:
            peer_networks = self.get_networks(self.host_vpc_id)
            if len(peer_networks) != 1:
                raise YBOpsRuntimeError("Invalid peer VPC network: {}".format(
                    self.host_vpc_id))
            # Prepare the connection to the peer
            self.setup_peering(YB_NETWORK_NAME, self.host_vpc_id)
            peer_network_url = peer_networks[0].get("selfLink")
        # Setup the firewall rules now that we have all subnets.
        self.setup_firewall_rules(network_url, peer_network_url)
        return self.network_info_as_json(YB_NETWORK_NAME, output_region_to_subnet_map)

    def cleanup(self):
        if self.dest_vpc_id:
            return
        # Check if network exists.
        networks = self.get_networks(YB_NETWORK_NAME)
        if len(networks) == 0:
            return
        network = networks[0]
        network_url = network.get("selfLink")
        ops_by_region = {}
        # List all relevant subnets.
        subnet_map = self.get_region_subnet_map(network_url)
        for region_name, scope in subnet_map.items():
            region = region_name.split("/")[1]
            subnets = scope.get("subnetworks", [])
            for s in subnets:
                subnet = s["name"]
                # Delete each subnet and keep track of op by region to wait for.
                op = self.delete_subnetwork(region, subnet)
                ops_by_region.setdefault(region, []).append(op)
        # Wait for all subnet deletions.
        for region, ops in ops_by_region.items():
            for op in ops:
                self.waiter.wait(op, region=region)
        # Remove all firewall rules.
        self.teardown_firewall_rules(network_url)
        # Remove the VPC peerings.
        if self.host_vpc_id is not None:
            self.teardown_peering(YB_NETWORK_NAME, self.host_vpc_id)
        # Delete the actual network.
        self.waiter.wait(self.delete_network(YB_NETWORK_NAME))

    def get_networks(self, network_name):
        filter = "(name eq {})".format(network_name)
        networks = self.compute.networks().list(project=self.project, filter=filter).execute()
        return networks.get("items", [])

    def create_network(self):
        body = {
            "name": YB_NETWORK_NAME,
            "autoCreateSubnetworks": False
        }
        return self.compute.networks().insert(project=self.project, body=body).execute()

    def delete_network(self, network_name):
        networks = self.get_networks(network_name)
        if len(networks) == 0:
            return
        # Delete the actual network.
        return self.compute.networks().delete(
            project=self.project, network=network_name).execute()

    def get_subnetworks(self, region, name=None):
        saved_subnet = self.per_region_meta.get(region, {}).get("subnetId")
        if saved_subnet:
            return saved_subnet

        network_name = YB_NETWORK_NAME
        if self.dest_vpc_id is not None:
            network_name = self.dest_vpc_id
        networks = self.get_networks(network_name)
        if len(networks) == 0:
            raise YBOpsRuntimeError("Invalid network: {}".format(network_name))
        filter = "(network eq {})".format(networks[0].get("selfLink"))
        if name is not None:
            filter += "(name eq {})".format(name)
        subnets = self.compute.subnetworks().list(
            project=self.project, region=region, filter=filter).execute()
        return [s["name"] for s in subnets.get("items", [])]

    def get_region_subnet_map(self, network_url):
        return self.compute.subnetworks().aggregatedList(
            project=self.project,
            filter="(network eq {})".format(network_url)
        ).execute().get("items", {})

    def create_subnetwork(self, network_url, region, name):
        cidr = self.metadata["region_cidr_format"].format(
            self.metadata["regions"][region]["cidr_prefix"])
        body = {
            "name": name,
            "network": network_url,
            "region": region,
            "ipCidrRange": cidr
        }
        return self.compute.subnetworks().insert(
            project=self.project, region=region, body=body).execute()

    def delete_subnetwork(self, region, subnet):
        return self.compute.subnetworks().delete(
            project=self.project, region=region, subnetwork=subnet).execute()

    def setup_firewall_rules(self, network_url, peer_network_url=None):
        ip_cidr_list = []
        # Get all the relevant subnets and their CIDR blocks.
        for url in [network_url, peer_network_url]:
            # Skip the peer_network_url if we did not set up peering.
            if url is None:
                continue
            data = self.compute.subnetworks().aggregatedList(
                project=self.project,
                filter="(network eq {})".format(url)).execute()
            data = data.get("items", {})
            subnet_cidrs = [s["ipCidrRange"]
                            for i in data.values()
                            for s in i.get("subnetworks", [])]
            ip_cidr_list.extend(subnet_cidrs)
        self.waiter.wait(self.update_firewall_rules(
            network_url, ip_cidr_list, YB_FIREWALL_NAME))

    def teardown_firewall_rules(self, network_url):
        firewall_name = YB_FIREWALL_NAME
        firewalls = self.get_firewall_rules(network_url, firewall_name)
        if len(firewalls) == 0:
            return
        return self.waiter.wait(self.compute.firewalls().delete(
            project=self.project,
            firewall=firewall_name).execute())

    def get_firewall_rules(self, network_url, name):
        filter = "(network eq {})(name eq {})".format(network_url, name)
        firewalls = self.compute.firewalls().list(
            project=self.project,
            filter=filter).execute()
        return firewalls.get("items", [])

    def update_firewall_rules(self, network_url, ip_cidr_list, firewall_name):
        # If we have the firewall rule already, we need an update, else insert.
        firewalls = self.get_firewall_rules(network_url, firewall_name)
        firewall_exists = len(firewalls) != 0
        # Setup the body of the request.
        body = {
            "name": firewall_name,
            "network": network_url,
            "description": "Open up all ports for internal IPs",
            "targetTags": get_firewall_tags(),
            "sourceRanges": ip_cidr_list,
            "allowed": [{"IPProtocol": p} for p in ["tcp", "udp", "icmp"]]
        }
        fw_object = self.compute.firewalls()
        if firewall_exists:
            # Only update if any of these CIDRs are not already there or if targetFlags should
            # be updated.
            firewall = firewalls[0]
            source_cidrs = set(firewall.get("sourceRanges"))
            new_cidrs = set(ip_cidr_list)
            union_cidrs = source_cidrs | new_cidrs

            current_tags = set(firewall.get("targetTags"))
            new_tags = set(get_firewall_tags())
            union_tags = current_tags | new_tags
            if source_cidrs != union_cidrs or current_tags != union_tags:
                body["sourceRanges"] = list(union_cidrs)
                body["targetTags"] = list(union_tags)
                # Use 'fw_object.update' and not 'fw_object.patch' to update firewall metadata
                # without removing pre-existing info.
                return fw_object.update(
                    project=self.project,
                    firewall=firewall_name,
                    body=body).execute()
            return None
        else:
            return fw_object.insert(
                project=self.project,
                body=body).execute()

    def setup_peering(self, src_network_name, dest_network_name):
        src_network = self.get_networks(src_network_name)
        if len(src_network) != 1:
            raise YBOpsRuntimeError("Invalid network peer: {}".format(src_network_name))
        src_network = src_network[0]
        dest_network = self.get_networks(dest_network_name)
        if len(dest_network) != 1:
            raise YBOpsRuntimeError("Invalid network peer: {}".format(dest_network_name))
        dest_network = dest_network[0]
        src_peerings = [p.get("network") for p in src_network.get("peerings", [])]
        dest_peerings = [p.get("network") for p in dest_network.get("peerings", [])]
        body = {
            # TODO: this is mandatory in the API for current version of peering...
            "autoCreateRoutes": True
        }
        ops_to_wait_on = []
        if dest_network.get("selfLink") not in src_peerings:
            body.update({
                "name": YB_PEERING_CONNECTION_FORMAT.format(
                    src_network_name, dest_network_name),
                "peerNetwork": dest_network.get("selfLink")
            })
            ops_to_wait_on.append(self.compute.networks().addPeering(
                project=self.project,
                network=src_network_name,
                body=body).execute())
        if src_network.get("selfLink") not in dest_peerings:
            body.update({
                "name": YB_PEERING_CONNECTION_FORMAT.format(
                    dest_network_name, src_network_name),
                "peerNetwork": src_network.get("selfLink")
            })
            ops_to_wait_on.append(self.compute.networks().addPeering(
                project=self.project,
                network=dest_network_name,
                body=body).execute())
        for op in ops_to_wait_on:
            self.waiter.wait(op)

    def teardown_peering(self, src_network_name, dest_network_name):
        # TODO: dedup handling of peering.
        src_network = self.get_networks(src_network_name)
        if len(src_network) != 1:
            raise YBOpsRuntimeError("Invalid network peer: {}".format(src_network_name))
        src_network = src_network[0]
        dest_network = self.get_networks(dest_network_name)
        if len(dest_network) != 1:
            raise YBOpsRuntimeError("Invalid network peer: {}".format(dest_network_name))
        dest_network = dest_network[0]
        src_peerings = [p.get("network") for p in src_network.get("peerings", [])]
        dest_peerings = [p.get("network") for p in dest_network.get("peerings", [])]
        if dest_network.get("selfLink") in src_peerings:
            body = {
                "name": YB_PEERING_CONNECTION_FORMAT.format(
                    src_network_name, dest_network_name)
            }
            self.waiter.wait(self.compute.networks().removePeering(
                project=self.project,
                network=src_network_name,
                body=body).execute())
        if src_network.get("selfLink") in dest_peerings:
            body = {
                "name": YB_PEERING_CONNECTION_FORMAT.format(
                    dest_network_name, src_network_name)
            }
            self.waiter.wait(self.compute.networks().removePeering(
                project=self.project,
                network=dest_network_name,
                body=body).execute())


class GoogleCloudAdmin():
    def __init__(self, metadata):
        # The following environment variables are required, if we want to use custom credentials:
        #   GCE_PROJECT: set to the name of the project
        #   GOOGLE_APPLICATION_CREDENTIALS: set to the path of the credentials json file
        # If these are not provided, then get_application_default will try to use the service
        # account associated with the instance we're running on, if one exists.
        self.credentials = oauth2client.client.GoogleCredentials.get_application_default()
        self.compute = discovery.build(
            "compute", "beta", credentials=self.credentials, num_retries=3)
        # If we have specified a GCE_PROJECT, use that, else, try the instance metadata, else fail.
        self.project = os.environ.get("GCE_PROJECT") or GcpMetadata.project()
        if self.project is None:
            raise YBOpsRuntimeError(
                "Could not determine GCP project from either env or server metadata!")

        self.metadata = metadata
        self.waiter = Waiter(self.project, self.compute)

    def network(self, dest_vpc_id=None, host_vpc_id=None, per_region_meta=None,
                create_new_vpc=False):
        if per_region_meta is None:
            per_region_meta = {}
        return NetworkManager(
            self.project, self.compute, self.metadata, dest_vpc_id,
            host_vpc_id, per_region_meta, create_new_vpc)

    @staticmethod
    def get_current_host_info():
        network = GcpMetadata.network()
        host_project = GcpMetadata.host_project()
        project = GcpMetadata.project()
        if network is None or project is None:
            raise YBOpsRuntimeError("Unable to auto-discover GCP provider information")
        return {
            "network": network.split("/")[-1],
            "host_project": host_project,
            "project": project
        }

    def get_image(self, image, project=None):
        return self.compute.images().get(
            project=project if project else self.project,
            image=image).execute()

    def get_full_image_name(self, name, preemptible=False):
        return IMAGE_NAME_PREFIX + name.upper() + \
            (IMAGE_NAME_PREEMPTIBLE_SUFFIX if preemptible else "")

    def create_disk(self, zone, instance_tags, body):
        if instance_tags is not None:
            body.update({"labels": json.loads(instance_tags)})
        # Create a persistent disk with wait
        operation = self.compute.disks().insert(project=self.project,
                                                zone=zone,
                                                body=body).execute()
        return self.waiter.wait(operation, zone=zone)

    def delete_disks(self, zone, tags, filter_disk_names=None):
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
            filters.append('labels.{}={}'.format(tag, value))
        disk_names = []
        list_disks_args = {
            'project': self.project,
            'zone': zone,
            'filter': ' AND '.join(filters)
        }
        while True:
            response = self.compute.disks().list(**list_disks_args).execute()
            for disk in response.get('items', []):
                status = disk['status']
                disk_name = disk['name']
                present_tags = disk['labels']
                users = disk.get('users', [])
                # API returns READY for used disks as it is the creation state.
                # Users refer to the users (instance).
                logging.info('[app] Disk {} is in state {}'.format(disk_name, status))
                if status.lower() != 'ready' or users:
                    continue
                if filter_disk_names and disk_name not in filter_disk_names:
                    continue
                tag_match_count = 0
                # Extra caution to make sure tags are present.
                for tag in tagPairs:
                    value = tagPairs[tag]
                    for present_tag in present_tags:
                        if present_tag == tag and present_tags[present_tag] == value:
                            tag_match_count += 1
                            break
                if tag_match_count == len(tagPairs):
                    disk_names.append(disk_name)
            if 'nextPageToken' in response:
                list_disks_args['pageToken'] = response['nextPageToken']
            else:
                break
        for disk_name in disk_names:
            logging.info('[app] Deleting disk {}'.format(disk_name))
            self.compute.disks().delete(project=self.project, zone=zone, disk=disk_name).execute()

    def mount_disk(self, zone, instance, body):
        logging.info("Attaching disk on instance {} in zone {}".format(instance, zone))
        operation = self.compute.instances().attachDisk(project=self.project,
                                                        zone=zone,
                                                        instance=instance,
                                                        body=body).execute()
        output = self.waiter.wait(operation, zone=zone)
        response = self.compute.instances().get(project=self.project,
                                                zone=zone,
                                                instance=instance).execute()
        for disk in response.get("disks"):
            if disk.get("source") != body.get("source"):
                continue
            device_name = disk.get("deviceName")
            logging.info("Setting disk auto delete for {} attached to {}"
                         .format(device_name, instance))
            # Even if this fails, volumes are already tagged for cleanup.
            operation = self.compute.instances().setDiskAutoDelete(project=self.project,
                                                                   zone=zone,
                                                                   instance=instance,
                                                                   deviceName=device_name,
                                                                   autoDelete=True).execute()
            self.waiter.wait(operation, zone=zone)
            break
        return output

    def unmount_disk(self, zone, instance, name):
        logging.info("Detaching disk {} from instance {}".format(name, instance))
        operation = self.compute.instances().detachDisk(project=self.project,
                                                        zone=zone,
                                                        instance=instance,
                                                        deviceName=name).execute()
        return self.waiter.wait(operation, zone=zone)

    def update_disk(self, args, instance):
        zone = args.zone
        instance_info = self.compute.instances().get(project=self.project, zone=zone,
                                                     instance=instance).execute()
        body = {
            "sizeGb": args.volume_size
        }
        for disk in instance_info['disks']:
            # The source is the complete URL of the disk, with the last
            # component being the name.
            disk_name = self.get_disk_name(disk)
            # Bootdisk should be ignored.
            # GCP does not allow disk resize to same volume size.
            if disk['index'] != 0 and int(disk["diskSizeGb"]) != args.volume_size:
                logging.info(
                    "Instance %s's volume %s changed to %s",
                    instance, disk_name, args.volume_size)
                operation = self.compute.disks().resize(project=self.project,
                                                        zone=zone,
                                                        disk=disk_name,
                                                        body=body).execute()
                self.waiter.wait(operation, zone=zone)
            elif disk['index'] != 0:
                logging.info(
                    "Instance %s's volume %s has not changed from %s",
                    instance, disk["deviceName"], disk["diskSizeGb"])

    def change_instance_type(self, zone, instance_name, newInstanceType):
        new_machine_type = f"zones/{zone}/machineTypes/{newInstanceType}"
        body = {
            "machineType": new_machine_type
        }
        operation = self.compute.instances().setMachineType(project=self.project,
                                                            zone=zone,
                                                            instance=instance_name,
                                                            body=body).execute()
        self.waiter.wait(operation, zone=zone)

    def delete_instance(self, region, zone, instance_name, has_static_ip=False):
        if has_static_ip:
            address = "ip-" + instance_name
            logging.info("[app] Deleting static ip {} attached to VM {}".format(
                address, instance_name))
            self.compute.addresses().delete(
                project=self.project, region=region, address=address).execute()
            logging.info("[app] Deleted static ip {} attached to VM {}".format(
                address, instance_name))
        operation = self.compute.instances().delete(
            project=self.project, zone=zone, instance=instance_name).execute()
        self.waiter.wait(operation, zone=zone)

    def stop_instance(self, zone, instance_name):
        operation = self.compute.instances().stop(project=self.project,
                                                  zone=zone,
                                                  instance=instance_name).execute()
        self.waiter.wait(operation, zone=zone)

    def wait_for_operation(self, zone, instance, operation_type):
        operation = self.waiter.get_in_progress_operation(zone=zone,
                                                          instance=instance,
                                                          operation_type=operation_type)
        if operation:
            self.waiter.wait(operation=operation, zone=zone)

    def start_instance(self, zone, instance_name):
        operation = self.compute.instances().start(project=self.project,
                                                   zone=zone,
                                                   instance=instance_name).execute()
        return self.waiter.wait(operation, zone=zone)

    def reboot_instance(self, zone, instance_name):
        operation = self.compute.instances().reset(project=self.project,
                                                   zone=zone,
                                                   instance=instance_name).execute()
        self.waiter.wait(operation, zone=zone)

    def query_vpc(self, region):
        """
        TODO: implement this once we get GCP custom region support...
        """
        return {}

    def get_subnetwork_by_name(self, region, name):
        host_project = self.get_host_project()
        return self.compute.subnetworks().get(project=host_project,
                                              region=region, subnetwork=name).execute()

    def get_regions(self):
        return list(self.metadata['regions'].keys())
        """
        # TODO: revert back to this if we decide to go back to the full list of regions from GCP.
        filter = "(status eq UP)"
        fields = "items(name)"
        regions = self.compute.regions().list(project=self.project,
                                              filter=filter,
                                              fields=fields).execute()

        return [r["name"] for r in regions.get("items", [])]
        """

    def get_zones(self, region, max_results=LIST_MAX_RESULTS):
        return ["{}-{}".format(region, zone)
                for zone in self.metadata['regions'].get(region, {}).get('zones', [])]

    def get_instance_types_by_zone(self, zone):
        fields = "items(name,description,guestCpus,memoryMb,isSharedCpu)"
        instance_type_items = self.compute.machineTypes().list(project=self.project,
                                                               fields=fields,
                                                               zone=zone).execute()
        return instance_type_items["items"]

    def get_host_project(self):
        return os.environ.get("GCE_HOST_PROJECT", self.project)

    def get_pricing_map(self, ):
        # Requests encounters an SSL bug when run on portal, so verify is set to false
        pricing_url_response = requests.get(PRICING_JSON_URL, verify=False)
        pricing_map_all = pricing_url_response.json()["gcp_price_list"]
        pricing_map = {}
        for key in pricing_map_all:
            if key.startswith(IMAGE_NAME_PREFIX):
                pricing_map[key] = pricing_map_all[key]
        return pricing_map

    @gcp_request_limit_retry
    def get_instances(self, zone, instance_name, get_all=False, filters=None):
        # TODO: filter should work to do (zone eq args.zone), but it doesn't right now...
        filter_params = []
        if filters:
            filter_params.append(filters)
        if instance_name is not None:
            filter_params.append("(name = \"{}\")".format(instance_name))
        if len(filter_params) > 0:
            filters = " AND ".join(filter_params)
        instances = self.compute.instances().aggregatedList(
            project=self.project,
            filter=filters,
            maxResults=(LIST_MAX_RESULTS if get_all else 1)).execute()
        instances = instances.get("items", [])
        if zone is not None:
            instances = instances.get("zones/{}".format(zone), None)
            if instances is None:
                raise YBOpsRuntimeError("Invalid zone: {}".format(zone))
            else:
                instances = instances.get("instances", [])
        else:
            instances = [i for sublist in instances.values() for i in sublist.get("instances", [])]
        if len(instances) == 0:
            return None
        results = []
        for data in instances:
            metadata = data.get("metadata", {}).get("items", {})
            disks = data.get("disks", [])
            root_vol = next((disk for disk in disks if disk.get("boot", False)), None)
            server_types = [i["value"] for i in metadata if i["key"] == "server_type"]
            node_uuid_tags = [i["value"] for i in metadata if i["key"] == "node-uuid"]
            universe_uuid_tags = [i["value"] for i in metadata if i["key"] == "universe-uuid"]
            private_ip = None
            primary_subnet = None
            secondary_private_ip = None
            secondary_subnet = None
            public_ip = None
            if data.get("networkInterfaces"):
                interface = data.get("networkInterfaces")
                if len(interface):
                    # Interface names are of form
                    # nic0, nic1, nic2, etc.
                    for i in interface:
                        # This will only be true for one of the NICs.
                        access_config = i.get("accessConfigs", [None])[0]
                        if access_config:
                            public_ip = access_config.get("natIP")
                        if i.get("name") == 'nic0':
                            private_ip = i.get("networkIP")
                            primary_subnet = i.get("subnetwork").split("/")[-1]
                        elif i.get("name") == 'nic1':
                            secondary_private_ip = i.get("networkIP")
                            secondary_subnet = i.get("subnetwork").split("/")[-1]
            zone = data["zone"].split("/")[-1]
            region = zone[:-2]
            machine_type = data["machineType"].split("/")[-1]
            instance_state = data.get("status")
            logging.info("VM state {}".format(instance_state))
            result = dict(
                id=data.get("name"),
                name=data.get("name"),
                public_ip=public_ip,
                private_ip=private_ip,
                secondary_private_ip=secondary_private_ip,
                subnet=primary_subnet,
                secondary_subnet=secondary_subnet,
                region=region,
                zone=zone,
                instance_type=machine_type,
                server_type=server_types[0] if server_types else None,
                node_uuid=node_uuid_tags[0] if node_uuid_tags else None,
                universe_uuid=universe_uuid_tags[0] if universe_uuid_tags else None,
                launched_by=None,
                launch_time=data.get("creationTimestamp"),
                root_volume=root_vol.get("source") if root_vol else None,
                root_volume_device_name=root_vol.get("deviceName") if root_vol else None,
                instance_state=instance_state,
                is_running=True if instance_state == "RUNNING" else False,
                metadata=data.get("metadata")
            )
            if not get_all:
                return result
            results.append(result)
        return results

    def get_image_disk_size(self, machine_image):
        tokens = machine_image.split("/")
        image_project = self.project
        image_project_idx = tokens.index("projects")
        if image_project_idx >= 0:
            image_project = tokens[image_project_idx + 1]
        image = self.get_image(tokens[-1], image_project)
        return image["diskSizeGb"]

    def create_instance(self, region, zone, cloud_subnet, instance_name, instance_type, server_type,
                        use_spot_instance, can_ip_forward, machine_image, num_volumes, volume_type,
                        volume_size, boot_disk_size_gb=None, assign_public_ip=True,
                        assign_static_public_ip=False, ssh_keys=None, boot_script=None,
                        auto_delete_boot_disk=True, tags=None, cloud_subnet_secondary=None,
                        gcp_instance_template=None):
        # Name of the project that target VPC network belongs to.
        host_project = self.get_host_project()

        boot_disk_json = {
            "autoDelete": auto_delete_boot_disk,
            "boot": True,
            "index": 0,
        }
        boot_disk_init_params = {}
        boot_disk_init_params["sourceImage"] = machine_image
        if boot_disk_size_gb is not None:
            # Default: 10GB
            min_disk_size = self.get_image_disk_size(machine_image)
            disk_size = min_disk_size if min_disk_size \
                and int(min_disk_size) > int(boot_disk_size_gb) \
                else boot_disk_size_gb
            boot_disk_init_params["diskSizeGb"] = disk_size
        # Create boot disk backed by a zonal persistent SSD
        boot_disk_init_params["diskType"] = "zones/{}/diskTypes/pd-ssd".format(zone)
        boot_disk_json["initializeParams"] = boot_disk_init_params

        access_configs = [{"natIP": None}
                          ] if assign_public_ip and not cloud_subnet_secondary else None

        if assign_static_public_ip:
            # Create external static ip.
            static_ip_name = "ip-" + instance_name
            static_ip_description = "Static IP {} for GCP VM {} in region {}".format(
                static_ip_name, instance_name, region)
            static_ip_body = {"name": static_ip_name, "description": static_ip_description}
            logging.info("[app] Creating " + static_ip_description)
            self.waiter.wait(self.compute.addresses().insert(
                project=self.project,
                region=region,
                body=static_ip_body).execute(), region=region)
            static_ip = self.compute.addresses().get(
                project=self.project,
                region=region,
                address=static_ip_name
            ).execute().get("address", str)
            logging.info("[app] Created Static IP at {} for VM {}".format(static_ip, instance_name))
            access_configs = [{"natIP": static_ip, "description": static_ip_description}]

        body = {
            "canIpForward": can_ip_forward,
            "disks": [boot_disk_json],
            "machineType": "zones/{}/machineTypes/{}".format(
                zone, instance_type),
            "metadata": {
                "items": [
                    {
                        "key": SERVER_TYPE_META_KEY,
                        "value": server_type
                    },
                    {
                        "key": SSH_KEYS_META_KEY,
                        "value": ssh_keys
                    },
                    {
                        "key": BLOCK_PROJECT_SSH_KEY,
                        "value": True
                    }
                ]
            },
            "name": instance_name,
            "networkInterfaces": [{
                "accessConfigs": access_configs,
                "subnetwork": "projects/{}/regions/{}/subnetworks/{}".format(
                    host_project, region, cloud_subnet)
            }],
            "tags": {
                "items": get_firewall_tags()
            }
        }
        if use_spot_instance:
            logging.info(f'[app] Using GCP spot instances')
            body["scheduling"] = {
                "provisioningModel": "SPOT"
            }
        # Attach a secondary network interface if present.
        if cloud_subnet_secondary:
            body["networkInterfaces"].append({
                "accessConfigs": [{"natIP": None}],
                "subnetwork": "projects/{}/regions/{}/subnetworks/{}".format(
                    host_project, region, cloud_subnet_secondary)
            })

        if boot_script:
            with open(boot_script, 'r') as script:
                body["metadata"]["items"].append({
                    "key": STARTUP_SCRIPT_META_KEY,
                    "value": script.read()
                })

        initial_params = {}
        if volume_type == GCP_SCRATCH:
            initial_params.update({
                "diskType": "zones/{}/diskTypes/local-ssd".format(zone)
            })
            # Default size: 375GB
        elif volume_type == GCP_PERSISTENT:
            initial_params.update({
                "diskType": "zones/{}/diskTypes/pd-ssd".format(zone),
                "sourceImage": None,
                "diskSizeGb": volume_size
            })
        disk_config = {
            "autoDelete": True,
            "type": volume_type,
            "initializeParams": initial_params
        }

        if tags is not None:
            tags_dict = json.loads(tags)
            body.update({"labels": tags_dict})
            if volume_type != GCP_SCRATCH:
                initial_params.update({"labels": tags_dict})
            boot_disk_init_params.update({"labels": tags_dict})
            body["metadata"]["items"].append(
                [{"key": k, "value": v} for (k, v) in tags_dict.items()])

        for _ in range(num_volumes):
            body["disks"].append(disk_config)

        args = {
            "project": self.project,
            "zone": zone,
            "body": body
        }
        logging.info("[app] About to create GCP VM {} in region {}.".format(
            instance_name, region))
        if gcp_instance_template:
            template_url_format = "projects/{}/global/instanceTemplates/{}"
            template_url = template_url_format.format(self.project, gcp_instance_template)
            logging.info("[app] Creating VM {} using instance template {}"
                         .format(instance_name, gcp_instance_template))
            args["sourceInstanceTemplate"] = template_url
        self.waiter.wait(self.compute.instances().insert(**args).execute(), zone=zone)
        logging.info("[app] Created GCP VM {}".format(instance_name))

    def get_console_output(self, zone, instance_name):
        try:
            return self.compute.instances().getSerialPortOutput(
                project=self.project,
                zone=zone,
                instance=instance_name).execute().get('contents', '')
        except HttpError:
            logging.exception('Failed to get console output from {}'.format(instance_name))
            return ''

    def update_boot_script(self, args, instance, boot_script):
        metadata = instance['metadata']
        # Get the current metadata 'items' list or initialize it if not present
        current_items = metadata.get('items', [])

        # Find the index of the 'startup-script' metadata item, or -1 if not found
        startup_script_index = next((index for index, item in enumerate(current_items)
                                     if item['key'] == 'startup-script'), -1)

        # If the 'startup-script' metadata item exists, update the value;
        # otherwise, append a new item
        if startup_script_index != -1:
            current_items[startup_script_index]['value'] = boot_script
        else:
            current_items.append({'key': 'startup-script', 'value': boot_script})

        # Update the instance metadata with the new items list
        self.compute.instances().setMetadata(
            project=self.project,
            zone=args.zone,
            instance=instance['name'],
            body={'fingerprint': metadata.get('fingerprint'), 'items': current_items}
        ).execute()

    def modify_tags(self, args, instance, tags_to_set_str, tags_to_remove_str):
        tags_to_set = json.loads(tags_to_set_str) if tags_to_set_str is not None else {}
        tags_to_remove = set(tags_to_remove_str.split(",") if tags_to_remove_str else [])

        zone = args.zone
        instance_info = self.compute.instances().get(project=self.project,
                                                     zone=zone,
                                                     instance=instance).execute()
        # modifying metadata
        metadata_items = instance_info["metadata"]["items"]
        meta_to_set = dict(tags_to_set)
        for entry in metadata_items:
            key = entry["key"]
            value = entry["value"]
            if key in META_KEYS or (key not in tags_to_set and key not in tags_to_remove):
                meta_to_set[key] = value
        metadata_items = [{"key": k, "value": v} for (k, v) in meta_to_set.items()]
        metadata_body = {
            "items": metadata_items,
            "kind": instance_info["metadata"]["kind"],
            "fingerprint": instance_info["metadata"]["fingerprint"]
        }
        operations = []
        operations.append(self.compute.instances().setMetadata(project=self.project,
                                                               zone=zone,
                                                               instance=instance,
                                                               body=metadata_body).execute())
        # modifying labels
        cur_labels = instance_info.get("labels", {})
        for (k, v) in tags_to_set.items():
            cur_labels[k] = v
        for k in tags_to_remove:
            if k in cur_labels:
                del cur_labels[k]
        body = {
            "labels": cur_labels,
            "labelFingerprint": instance_info["labelFingerprint"]
        }
        # modifying instance labels
        operations.append(self.compute.instances().setLabels(project=self.project,
                                                             zone=zone,
                                                             instance=instance,
                                                             body=body).execute())
        # modifying disks labels
        for disk in instance_info.get("disks", []):
            disk = self.compute.disks().get(project=self.project,
                                            zone=zone,
                                            disk=self.get_disk_name(disk)).execute()
            disk_body = {
                "labels": cur_labels,
                "labelFingerprint": disk["labelFingerprint"]
            }
            operations.append(self.compute.disks().setLabels(project=self.project,
                                                             zone=zone,
                                                             resource=disk["id"],
                                                             body=disk_body).execute())
        # waiting for all operations to complete
        for operation in operations:
            self.waiter.wait(operation, zone=zone)

    @staticmethod
    def get_disk_name(disk):
        return disk['source'].split('/')[-1]
