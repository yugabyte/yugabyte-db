#!/usr/bin/env python
#
# Copyright 2019 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

import boto3
import json
import logging
import os
import re
import time

from ipaddress import ip_network
from ybops.utils import get_or_create, get_and_cleanup, DNS_RECORD_SET_TTL
from ybops.common.exceptions import YBOpsRuntimeError, YBOpsRecoverableError
from ybops.cloud.common.utils import request_retry_decorator
from ybops.cloud.common.cloud import AbstractCloud

RESOURCE_PREFIX_FORMAT = "yb-{}"
IGW_CIDR = "0.0.0.0/0"
SUBNET_PREFIX_FORMAT = RESOURCE_PREFIX_FORMAT
IGW_PREFIX_FORMAT = RESOURCE_PREFIX_FORMAT + "-igw"
ROUTE_TABLE_PREFIX_FORMAT = RESOURCE_PREFIX_FORMAT + "-rt"
SG_YUGABYTE_PREFIX_FORMAT = RESOURCE_PREFIX_FORMAT + "-sg"
PEER_CONN_FORMAT = "yb-peer-conn-{}-to-{}"


class AwsBootstrapRegion():
    def __init__(self, region, metadata, region_cidrs):
        self.region = region
        self.metadata = metadata
        self.region_cidrs = region_cidrs

        self.client = get_client(self.region)

        # Outputs.
        self.vpc = None
        self.igw = None
        self.peer_vpc = None
        self.sg_yugabyte = None
        self.subnets = []
        self.route_table = None

    def bootstrap(self):
        self.setup_vpc()
        self.setup_igw()
        self.setup_subnets()
        self.setup_yugabyte_sg()
        self.setup_rt()

    def setup_vpc(self):
        vpc_region_tag = RESOURCE_PREFIX_FORMAT.format(self.region)
        vpc = create_vpc(client=self.client, tag_name=vpc_region_tag,
                         cidr=get_region_cidr(self.metadata, self.region))
        vpc.wait_until_available()

        self.vpc = vpc

    def setup_igw(self):
        igw_tag = IGW_PREFIX_FORMAT.format(self.region)
        igw = create_igw(client=self.client, tag_name=igw_tag, vpc=self.vpc)

        self.igw = igw

    def setup_subnets(self):
        zones = get_zones(self.region)
        subnets = {}
        for zone_index, zone in enumerate(sorted(zones.keys())):
            vpc_zone_tag = SUBNET_PREFIX_FORMAT.format(zone)
            zone_cidr = self.metadata["zone_cidr_format"].format(
                get_cidr_prefix(self.metadata, self.region), (zone_index + 1) * 16)
            subnet = create_subnet(self.client, self.vpc, zone, zone_cidr, vpc_zone_tag)
            subnets[zone] = subnet

        self.subnets = subnets

    def setup_yugabyte_sg(self):
        sg_group_name = get_yb_sg_name(self.region)
        rules = list(self.metadata["sg_rules"])
        for r in rules:
            r.update({"cidr_ip": IGW_CIDR})
        sg = create_security_group(client=self.client, group_name=sg_group_name,
                                   description="YugaByte SG", vpc=self.vpc,
                                   rules=rules)
        self.sg_yugabyte = sg

    def setup_rt(self):
        route_table_tag = ROUTE_TABLE_PREFIX_FORMAT.format(self.region)
        route_table = create_route_table(client=self.client, tag_name=route_table_tag,
                                         vpc=self.vpc)
        # TODO: handle private/public case at somepoint, also NAT.
        add_route_to_rt(route_table, IGW_CIDR, "GatewayId", self.igw.id)
        current_associated_subnet_ids = [assoc.subnet_id for assoc in route_table.associations]
        missing_ids = [subnet.id for subnet in self.subnets.values()
                       if subnet.id not in current_associated_subnet_ids]
        for subnet_id in missing_ids:
            route_table.associate_with_subnet(SubnetId=subnet_id)

        self.route_table = route_table

    def add_sg_ingress_to_sg(self, incoming_sg, target_sg):
        current_sg_ids = set([pair["GroupId"]
                              for perm in target_sg.ip_permissions
                              for pair in perm["UserIdGroupPairs"]])
        if incoming_sg.id not in current_sg_ids:
            target_sg.authorize_ingress(
                IpPermissions=[{
                    "IpProtocol": "-1",
                    "UserIdGroupPairs": [{"GroupId": incoming_sg.id}]}])


def add_route_to_rt(route_table, cidr, target_type, target_id):
    kwargs = {target_type: target_id}
    route = get_route_by_cidr(route_table, cidr)
    if route is None:
        route_table.create_route(DestinationCidrBlock=cidr, **kwargs)
    elif getattr(route, dumb_camel_to_snake(target_type)) != target_id:
        route.replace(**kwargs)


def add_cidr_to_rules(rules, cidr):
    rule_block = {
        "ip_protocol": "-1",
        "from_port": 0,
        "to_port": 65535,
        "cidr_ip": cidr
    }
    rules.append(rule_block)


def get_cidr_prefix(metadata, region):
    return metadata["regions"][region]["cidr_prefix"]


def get_region_cidr(metadata, region):
    return metadata["region_cidr_format"].format(get_cidr_prefix(metadata, region))


def get_region_cidrs(metadata):
    return dict([(r, get_region_cidr(metadata, r)) for r in metadata["regions"].keys()])


def dumb_camel_to_snake(s):
    return re.sub("([A-Z])", "_\\1", s).lower()[1:]


class YbVpcComponents:
    def __init__(self):
        self.region = None
        self.vpc = None
        self.sg_yugabyte = None
        self.customer_sgs = None
        self.route_table = None
        self.subnets = None

    @staticmethod
    def from_pieces(region, vpc_id, sg_id, rt_id, az_to_subnet_ids):
        c = YbVpcComponents()
        c.region = region
        client = get_client(region)
        c.vpc = client.Vpc(vpc_id)
        c.sg_yugabyte = client.SecurityGroup(sg_id)
        c.route_table = client.RouteTable(rt_id)
        c.subnets = {az: client.Subnet(subnet_id)
                     for az, subnet_id in az_to_subnet_ids.items()}
        return c

    @staticmethod
    def from_user_json(region, per_region_meta):
        c = YbVpcComponents()
        c.region = region
        client = get_client(region)
        vpc_id = per_region_meta.get("vpcId")
        if vpc_id:
            c.vpc = client.Vpc(vpc_id)
        else:
            c.vpc = get_vpc(client, RESOURCE_PREFIX_FORMAT.format(region))
        sg_ids = per_region_meta.get("customSecurityGroupId")
        if sg_ids:
            c.customer_sgs = [client.SecurityGroup(sg_id) for sg_id in sg_ids.split(",")]
        else:
            c.sg_yugabyte = get_security_group(
                client, SG_YUGABYTE_PREFIX_FORMAT.format(region), c.vpc)
        if not vpc_id:
            c.route_table = get_route_table(client, ROUTE_TABLE_PREFIX_FORMAT.format(region))
        az_to_subnet_ids = {}
        if vpc_id:
            az_to_subnet_ids = per_region_meta.get("azToSubnetIds", {})
        else:
            az_to_subnet_ids = get_zones(region)
        c.subnets = {az: client.Subnet(subnet_id)
                     for az, subnet_id in az_to_subnet_ids.items()}
        return c

    def as_json(self):
        sgs = self.customer_sgs if self.customer_sgs else [self.sg_yugabyte]
        return vpc_components_as_json(self.vpc, sgs, self.subnets)


class AwsBootstrapClient():
    def __init__(self, metadata, host_vpc_id, host_vpc_region):
        self.metadata = metadata
        self.host_vpc_id = host_vpc_id
        self.host_vpc_region = host_vpc_region
        self.region_cidrs = get_region_cidrs(self.metadata)
        # Validation.
        self._validate_cidr_overlap()

    def _validate_cidr_overlap(self):
        region_networks = [ip_network(cidr) for cidr in self.region_cidrs.values()]
        all_networks = region_networks
        for i in range(len(all_networks)):
            for j in range(i + 1, len(all_networks)):
                left = all_networks[i]
                right = all_networks[j]
                if left.overlaps(right):
                    raise YBOpsRuntimeError(
                        "IP blocks in the CIDRs overlap: {} - {}".format(left, right))

    def bootstrap_individual_region(self, region):
        if region is None:
            raise YBOpsRuntimeError("Must provider region to bootstrap!")
        client = AwsBootstrapRegion(region, self.metadata, self.region_cidrs)
        client.bootstrap()
        return YbVpcComponents.from_pieces(
            region, client.vpc.id, client.sg_yugabyte.id, client.route_table.id,
            {az: s.id for az, s in client.subnets.items()})

    def cross_link_regions(self, components, added_region_codes):
        # Do the cross linking, adding CIDR entries to RTs and SGs, as well as doing vpc peerings.
        region_and_vpc_tuples = [(r, c.vpc) for r, c in components.items()]
        host_vpc = None
        if self.host_vpc_id and self.host_vpc_region:
            host_vpc = get_client(self.host_vpc_region).Vpc(self.host_vpc_id)
            if self.host_vpc_id not in [c.vpc.id for _, c in components.items()]:
                region_and_vpc_tuples.append((self.host_vpc_region, host_vpc))
        # Setup VPC peerings.
        for i in range(len(region_and_vpc_tuples) - 1):
            i_region, i_vpc = region_and_vpc_tuples[i]
            for j in range(i + 1, len(region_and_vpc_tuples)):
                # skip linking existing regions
                if i_region not in added_region_codes and j_region not in added_region_codes:
                    continue
                j_region, j_vpc = region_and_vpc_tuples[j]
                peerings = create_vpc_peering(
                    # i is the host, j is the target.
                    client=get_client(i_region), vpc=j_vpc, host_vpc=i_vpc, target_region=j_region)
                if len(peerings) != 1:
                    raise YBOpsRuntimeError(
                        "Expecting one peering connection from {} to {}, got {}".format(
                            i_vpc.id,
                            j_vpc.id,
                            len(peerings)))
                peering = peerings[0]
                # Add route i -> j.
                add_route_to_rt(components[i_region].route_table, j_vpc.cidr_block,
                                "VpcPeeringConnectionId", peering.id)
                # Add route j -> i.
                # Note: If we have a host_vpc, it is the last in the list, and it doesn't have an
                # associated component, so we special case it.
                if host_vpc is None or j != len(region_and_vpc_tuples) - 1:
                    add_route_to_rt(components[j_region].route_table, i_vpc.cidr_block,
                                    "VpcPeeringConnectionId", peering.id)
                else:
                    # TODO: should ideally filter to the RT that is relevant, but we do not really
                    # know the subnets which matter from this host_vpc...
                    for rt in list(host_vpc.route_tables.all()):
                        add_route_to_rt(rt, i_vpc.cidr_block, "VpcPeeringConnectionId", peering.id)
        # Setup SG entries for all the CIDRs.
        all_cidrs = [vpc.cidr_block for r, vpc in region_and_vpc_tuples]
        rules = []
        # Add CIDRs from all the VPCs, including the host.
        for cidr in all_cidrs:
            add_cidr_to_rules(rules, cidr)
        # Add CIDRs from any custom networks we have internally, primarily the OpenVPN in AWS.
        # TODO(bogdan): custom CIDR entries
        for cidr in self.metadata.get("custom_network_whitelisted_ip_cidrs", []):
            add_cidr_to_rules(rules, cidr)
        for region, component in components.items():
            sg = component.sg_yugabyte
            ip_perms = sg.ip_permissions
            for rule in rules:
                found = False
                for perm in ip_perms:
                    if perm.get("FromPort") == rule["from_port"] and \
                            perm.get("ToPort") == rule["to_port"] and \
                            perm.get("IpProtocol") == rule["ip_protocol"] and \
                            len([True for r in perm.get("IpRanges", [])
                                 if r.get("CidrIp") == rule["cidr_ip"]]) > 0:
                        # This rule matches this permission, so no need to add it.
                        found = True
                        break
                if not found:
                    try:
                        sg.authorize_ingress(IpProtocol=rule["ip_protocol"],
                                             CidrIp=rule["cidr_ip"],
                                             FromPort=rule["from_port"],
                                             ToPort=rule["to_port"])
                    except Exception as e:
                        if "InvalidPermission.Duplicate" not in str(e):
                            raise YBOpsRuntimeError(
                                "Authorize Security Group Ingress failed: {}".format(repr(e)))


def aws_exception_handler(e):
    """AWS specific exception handler.
    Args:
        e: the exception that was raised by the underlying API call that just failed.
    Returns:
        True if this exception can be retried, False otherwise.
    """
    return "Request limit exceeded" in str(e)


def aws_request_limit_retry(fn):
    """A decorator for retrying an AWS operation after exceeding request limit. Does retries with
    randomized jitter. Ideally, we should reconfigure boto3 to do the right kind of retries
    internally, but as of May 2017 there does not seem to be a good way of doing that.

    Initially not adding this decorator to all functions in this module. This should be done
    gradually as we encounter rate limiting errors.

    Relevant boto issues:

    https://github.com/boto/boto3/issues/770
    https://github.com/boto/botocore/issues/882
    """
    return request_retry_decorator(fn, aws_exception_handler)


def get_raw_client(region):
    """
    Returns:
        boto3 client
    """
    return boto3.client("ec2", region_name=region)


def get_client(region):
    """Method to get boto3 ec2 resource for given region
    Args:
        region (str): Region name
    Returns:
        boto3 resource
    """
    return boto3.resource("ec2", region_name=region)


def get_clients(regions):
    """Method to get boto3 clients for given region or all the regions if none specified.
    Args:
        regions (list): List of regions to return clients for
    Returns:
        clients(obj): Map of region to boto3 resource
    """
    return {region: get_client(region) for region in regions}


def get_available_regions(metadata):
    return list(metadata["regions"].keys())


def get_spot_pricing(region, zone, instance_type):
    client = boto3.client('ec2', region_name=region)
    prod_desc = ['Linux/UNIX (Amazon VPC)']
    spot_price = client.describe_spot_price_history(InstanceTypes=[instance_type],
                                                    MaxResults=1,
                                                    ProductDescriptions=prod_desc,
                                                    AvailabilityZone=zone)
    if len(spot_price['SpotPriceHistory']) == 0:
        raise YBOpsRuntimeError('Invalid instance type {} for zone {}'.format(instance_type, zone))
    return spot_price['SpotPriceHistory'][0]['SpotPrice']


def describe_ami(region, ami):
    client = boto3.client("ec2", region_name=region)
    images = client.describe_images(ImageIds=[ami]).get("Images", [])
    if len(images) == 0:
        raise YBOpsRuntimeError('Could not find image for AMI {} in region {}'.format(ami, region))
    return images[0]


def get_image_arch(region, ami):
    return describe_ami(region, ami).get("Architecture")


def get_root_label(region, ami):
    return describe_ami(region, ami).get("RootDeviceName")


def get_zones(region, dest_vpc_id=None):
    """Method to fetch zones for given region or all the regions if none specified.
    Args:
        region (str): Name of region to get zones of.
    Returns:
        zones (obj): Map of zone -> subnet
    """
    result = {}
    filters = get_filters("state", "available")
    client = boto3.client("ec2", region_name=region)
    zones = client.describe_availability_zones(Filters=filters).get("AvailabilityZones", [])
    new_client = get_client(region)
    zone_mapping = {}
    for z in zones:
        zone_name = z["ZoneName"]
        zone_tag = SUBNET_PREFIX_FORMAT.format(zone_name)
        region_vpc = None
        if dest_vpc_id:
            region_vpc = new_client.Vpc(dest_vpc_id)
        else:
            region_vpc = get_vpc(new_client, RESOURCE_PREFIX_FORMAT.format(region))
        subnet = next(iter(fetch_subnets(region_vpc, zone_tag)), None)
        if subnet is None:
            subnet = next(iter([s for s in region_vpc.subnets.all()
                                if s.availability_zone == zone_name]), None)
        zone_mapping[zone_name] = subnet.id if subnet is not None else None
    return zone_mapping


def same_networks(net1, net2):
    """Method to check if two networks are the same.
    """
    try:
        # Always false if one is v4 and the other is v6.
        if net1._version != net2._version:
            return False
        return (net1.network_address == net2.network_address and
                net1.broadcast_address == net2.broadcast_address)
    except AttributeError:
        raise YBOpsRuntimeError("Failed to check equality of networks {} and {}"
                                .format(net1, net2))


def get_vpc(client, tag_name, **kwargs):
    """Method to fetch vpc based on the tag_name and cidr optionally if present.
    Args:
        client (boto client): Boto Client for the region to query.
        tag_name (str): VPC tag name.
    Returns:
        VPC obj: VPC object or None.
    """
    filters = get_tag_filter(tag_name)
    vpcs = client.vpcs.filter(Filters=filters)
    if 'cidr' in kwargs:
        net1 = ip_network(kwargs.get('cidr'))
        for vpc in vpcs:
            net2 = ip_network(vpc.cidr_block)
            if same_networks(net1, net2):
                return vpc
            raise YBOpsRuntimeError("VPC {} with tag {} already exists with different CIDR {}"
                                    .format(vpc.id, tag_name, vpc.cidr_block))
        return None
    return next(iter(vpcs), None)


def fetch_subnets(vpc, tag_name):
    """Method to fetch subnets based on the tag_name.
    Args:
        vpc (vpc obj): VPC object to search for subnets
        tag_name (str): subnet tag name.
    Returns:
        subnets (list): list of aws subnets for given vpc.
    """
    filters = get_tag_filter(tag_name)
    return vpc.subnets.filter(Filters=filters)


def create_subnet(client, vpc, zone, cidr, tag_name):
    """Method to create subnet based on cidr and tag name.
    Args:
        client (boto client): Region specific boto client
        vpc (VPC object): VPC object to create subnet.
        zone (str): Availability zone name
        cidr (str): CIDR string
        tag_name (str): Tag name for subnet.
    Returns:
        subnet: Newly created subnet object.
    """
    subnet = next((s for s in fetch_subnets(vpc, tag_name) if s.cidr_block == cidr), None)
    if subnet is None:
        subnet = vpc.create_subnet(CidrBlock=cidr, AvailabilityZone=zone)
        # TODO: no direct waiter on subnet just yet, it seems...
        client.meta.client.get_waiter("subnet_available").wait(SubnetIds=[subnet.id])
        tag_resource_name(client, subnet.id, tag_name)
    return subnet


def get_security_group(client, group_name, vpc, **kwargs):
    """Method to fetch security group based on the group_name.
    Args:
        client (boto client): Region specific boto client
        group_name (str): Security Group name
        vpc (VPC object): The VPC in which to check for the SG
    Returns:
        SecurityGroup: Matching security group.
    """
    filters = get_filters("group-name", group_name) + get_filters("vpc-id", vpc.id)
    return next(iter(client.security_groups.filter(Filters=filters)), None)


@get_or_create(get_security_group)
def create_security_group(client, group_name, vpc, description, rules):
    """Method to create a security group based on the group_name and authorize ingress with
    the rules provided.
    Args:
        client (boto client): Region specific boto client
        group_name (str): security group name
        description (str): description of the security group
        vpc (VPC Object): VPC object to create the security group
        rules (dict): List of rules to add to security group.
    """
    sg = vpc.create_security_group(GroupName=group_name, Description=description)
    try:
        for rule in rules:
            sg.authorize_ingress(IpProtocol=rule["ip_protocol"],
                                 CidrIp=rule["cidr_ip"],
                                 FromPort=rule["from_port"],
                                 ToPort=rule["to_port"])
    except Exception as e:
        logging.error("Authorize Security Group Ingress failed: {}".format(e))
        sg.delete()
        raise YBOpsRuntimeError("Security Group creation failed.")
    return sg


def get_igw(client, tag_name, **kwargs):
    """Method to fetch Internet Gateway based on tag_name.
    Args:
        client (boto client): Region specific boto client
        tag_name (str): Internet Gateway tag name.
    Returns:
        internet_gateway: internet gateway object.
    """
    filters = get_tag_filter(tag_name)
    return next(iter(client.internet_gateways.filter(Filters=filters)), None)


@get_or_create(get_igw)
def create_igw(client, tag_name, vpc):
    """Method to create Internet Gateway based on tag_name in given VPC. If the gateway
    already exists, it would return that object. If the object doesn't have a tag, we
    would tag it accordingly.
    Args:
        client (boto client): Region specific boto client
        tag_name (str): Tag name for internet gateway.
        vpc (VPC object): VPC object to create Internet Gateway
    Returns:
        internet gateway: newly internet gateway object.
    """
    # Query to make sure the region doesn't have any IGW already attached.
    existing_igw = next(iter(vpc.internet_gateways.all()), None)
    if existing_igw is not None:
        # If we have existing igw for the region, lets just tag it with yb-XX-igw
        tag_resource_name(client, existing_igw.id, tag_name)
        return existing_igw

    # If we don't have a internet gateway, lets create one and attach it to vpc
    igw = client.create_internet_gateway()
    tag_resource_name(client, igw.id, tag_name)
    vpc.attach_internet_gateway(InternetGatewayId=igw.id)
    return igw


def get_route_table(client, tag_name, **kwargs):
    """Method to fetch route table based on tag_name
    Args:
        client (boto client): Region specific boto client
        tag_name (str): Route table tag name to search for.
    Returns:
        RouteTable (obj): Matching route table object or None.
    """
    filters = get_tag_filter(tag_name)
    return next(iter(client.route_tables.filter(Filters=filters)), None)


@get_or_create(get_route_table)
def create_route_table(client, tag_name, vpc):
    """Method to create route table based on tag_name in given VPC. It will first
    query for the tag name to see if the route table already exists or if one is already
    attached to the VPC, if so it will return that route table.
    Args:
        client (boto client): Region specific boto client
        tag_name (str): Route table tag name
        vpc (vpc object): VPC object to create the route table against
    Returns:
        RouteTable (obj): newly created RouteTable object.
    """
    # Check to see if there is a route table attached to VPC, if so, we can just tag it
    existing_route_table = next(iter(vpc.route_tables.all()), None)
    if existing_route_table is not None:
        tag_resource_name(client, existing_route_table.id, tag_name)
        return existing_route_table

    # If no route table exists, we can create one and tag it.
    route_table = vpc.create_route_table()
    tag_resource_name(client, route_table.id, tag_name)
    return route_table


@get_and_cleanup(get_security_group)
def cleanup_security_group(sg, **kwargs):
    """Method to cleanup security group for the matching group_name.
    Args:
        sg: Instance of security group matching the group_name.
    """
    sg.delete()


@get_and_cleanup(get_igw)
def cleanup_igw(igw, **kwargs):
    """Method to cleanup Internet Gateway matching the tag name. And also remove any vpc
    that is attached to the Internet Gateway.
    Args:
        igw: Instance of Internet Gateway matching tag_name.
    """
    for vpc in igw.attachments:
        igw.detach_from_vpc(VpcId=vpc['VpcId'])
    igw.delete()


@get_and_cleanup(get_route_table)
def cleanup_route_table(rt, **kwargs):
    """Method to cleanup the Route Table matching the tag name.
    Args:
        rt: Instance of Route Table matching tag_name.
    """
    rt.delete()


def get_route_by_cidr(route_table, cidr):
    """Method to check if given CIDR already attached to route table.
    Args:
        RouteTable (obj): Route Table object.
        cidr (str): CIDR string to check in route table.
    Returns:
        Route: the route for this CIDR or None if not found
    """
    return dict((r.destination_cidr_block, r) for r in route_table.routes).get(cidr)


@get_or_create(get_vpc)
def create_vpc(client, tag_name, cidr):
    """Method to create vpc based on the cidr and tag with tag_name.
    Args:
        client (boto client): Region specific boto client
        tag_name (str): VPC tag name
        cidr (str): CIDR string.
    Returns:
        VPC(Object): Newly created VPC object.
    """
    vpc = client.create_vpc(CidrBlock=cidr)
    vpc.modify_attribute(EnableDnsHostnames={'Value': True})
    tag_resource_name(client, vpc.id, tag_name)
    return vpc


def set_yb_sg_and_fetch_vpc(metadata, region, dest_vpc_id):
    """Method to bootstrap vpc and security group, and enable vpc peering
    with the host_instance vpc.
    Args:
        metadata (obj): Cloud metadata object with cidr prefix and other metadata.
        region (str): Region name to create the vpc in.
        dest_vpc_id (str): Id of the VPC that yugabyte machines will reside in.
    Returns:
        vpc_info (json): return vpc, subnet and security group as json.
    """
    client = get_client(region)
    dest_vpc = client.Vpc(dest_vpc_id)
    subnets = {subnet.availability_zone: subnet for subnet in dest_vpc.subnets.all()}

    sg_group_name = get_yb_sg_name(region)
    rules = metadata["sg_rules"]
    for r in rules:
        r.update({"cidr_ip": IGW_CIDR})
    add_cidr_to_rules(rules, dest_vpc.cidr_block)
    sgs = [create_security_group(client=client, group_name=sg_group_name, vpc=dest_vpc,
                                 description="YugaByte SG", rules=rules)]
    return vpc_components_as_json(dest_vpc, sgs, subnets)


def query_vpc(region):
    """Method to query VPC against given region and respective subnets.
    Args:
        region (str): Region name to query the VPC.
    Returns:
        vpc and subnet info (obj): Object with region and zone subnet id.
    """
    per_vpc_info = {}
    # Fetch all available AZs, as we want to group subnets by AZ.
    raw_client = boto3.client("ec2", region_name=region)
    zones = [z["ZoneName"]
             for z in raw_client.describe_availability_zones(
            Filters=get_filters("state", "available")).get("AvailabilityZones", [])]
    # Default to empty lists, in case some zones do not have subnets, so we can use this as a query
    # for all available AZs in this region.
    subnets_by_zone = {z: [] for z in zones}
    # Fetch SGs and group them by VPC ID.
    client = get_client(region)
    per_vpc_sgs = {}
    sgs = client.security_groups.all()
    for sg in sgs:
        sg_list = per_vpc_sgs.setdefault(sg.vpc_id, [])
        sg_list.append({
            "sg_id": sg.group_id,
            # Note: Name tag is not mandatory or always present but group_name is!
            "sg_name": sg.group_name
        })
    # Fetch all available VPCs so we can group by VPC ID.
    region_vpcs = client.vpcs.all()
    for vpc in region_vpcs:
        # Filter for available subnets and group by AZ.
        subnets = vpc.subnets.filter(Filters=get_filters("state", "available"))
        for s in subnets:
            subnets_for_this_az = subnets_by_zone.setdefault(s.availability_zone, [])
            subnets_for_this_az.append({
                "subnet_id": s.subnet_id,
                "name": _get_name_from_tags(s.tags),
                "public": s.map_public_ip_on_launch

            })
        vpc_info = {
            "subnets_by_zone": subnets_by_zone,
            # In case we somehow did not find any SGs, default to empty list.
            "security_groups": per_vpc_sgs.get(vpc.id, [])
        }
        per_vpc_info[vpc.id] = vpc_info
    region_json = {
        "per_vpc_info": per_vpc_info
    }
    return region_json


def _get_name_from_tags(tags):
    for t in tags if tags else []:
        if t.get("Key") == "Name":
            return t.get("Value", None)
    return None


def vpc_components_as_json(vpc, sgs, subnets):
    """Method takes VPC, Security Group and Subnets and returns a json data format with ids.
    Args:
        vpc (VPC Object): Region specific VPC object
        sgs (List of Security Group Object): Region specific Security Group object
        subnets (subnet object map): Map of Subnet objects keyed of zone.
    Retuns:
        json (str): A Json string for yugaware to consume with necessary ids.
    """
    result = {}
    result["vpc_id"] = vpc.id
    result["security_group"] = [{"id": sg.group_id, "name": sg.group_name} for sg in sgs]
    result["zones"] = {}
    for zone, subnet in subnets.items():
        result["zones"][zone] = subnet.id
    return result


def delete_vpc(region, host_vpc_id=None, host_vpc_region=None):
    """Method to delete VPC, Subnet, Internet Gateway, Route Table and VPC peering.
    Args:
        region (str): Region name to query the VPC.
    """
    vpc_region_tag = RESOURCE_PREFIX_FORMAT.format(region)
    client = get_client(region)
    region_vpc = get_vpc(client, vpc_region_tag)
    if region_vpc is None:
        raise YBOpsRuntimeError("VPC not setup.")
    zones = get_zones(region)
    # Remove the yugabyte SG first.
    sg_group_name = get_yb_sg_name(region)
    cleanup_security_group(client=client, group_name=sg_group_name, vpc=region_vpc)
    # Cleanup the subnets.
    for zone, subnet_id in zones.items():
        vpc_zone_tag = SUBNET_PREFIX_FORMAT.format(zone)
        if subnet_id is not None:
            client.Subnet(subnet_id).delete()
    # Remove the IGW.
    igw_tag = IGW_PREFIX_FORMAT.format(region)
    cleanup_igw(client=client, tag_name=igw_tag)
    # Remove this region's CIDR from the RT of the host vpc.
    host_vpc = None
    if host_vpc_id is not None and host_vpc_region is not None:
        host_vpc = get_client(host_vpc_region).Vpc(host_vpc_id)
        for rt in list(host_vpc.route_tables.all()):
            delete_route(rt, region_vpc.cidr_block)
    # Remove all of the VPC peerings of this vpc.
    cleanup_vpc_peering(client=client, vpc=region_vpc, host_vpc=None)
    # Delete the VPC itself.
    region_vpc.delete()
    # Finally cleanup the Routing Table.
    route_table_tag = ROUTE_TABLE_PREFIX_FORMAT.format(region)
    cleanup_route_table(client=client, tag_name=route_table_tag)
    return {"success": "VPC deleted."}


def tag_resource_name(client, resource_id, tag_name):
    """Method to create name tag for given resource.
    Args:
        client (boto3 client): Region specific boto client
        resource_id (str): EC2 resource id to tag
        tag_name (str): Tag name.
    """
    tag_resource(client, resource_id, "Name", tag_name)


def tag_resource(client, resource_id, tag_key, tag_value):
    """Method to attach arbitrary key-value tags to resources.
    Args:
        client (boto3 client): Region specific boto client
        resource_id (str): EC2 resource id to tag
        tag_key: Tag key
        tag_value: Tag value
    """
    tags = [{"Key": tag_key, "Value": tag_value}]
    client.create_tags(Resources=[resource_id], Tags=tags)


def get_filters(key, value):
    return [{'Name': key, 'Values': [value]}]


def get_tag_filter(tag_name):
    return get_filters("tag:Name", tag_name)


def get_vpc_peerings(vpc, host_vpc, **kwargs):
    """Method to fetch all the VPC peerings against given VPC. If host_vpc is provided
    it will check if there is a peering against that vpc.
    Args:
        vpc(VPC object): VPC Object to search for peerings
        host_vpc (Host VPC object): Can be Null as well, to check if specific host_vpc
                                    peering is done.
    Returns:
        VPC peering (array): Array list of vpc peerings.
    """
    output = []
    # Search through accepted vpc peerings.
    vpc_peerings = vpc.accepted_vpc_peering_connections.all()
    output.extend([vp for vp in vpc_peerings
                   if vp.status.get('Code') == "active" and
                   (host_vpc is None or vp.requester_vpc == host_vpc)])
    # Also search through requested vpc peerings.
    vpc_peerings = vpc.requested_vpc_peering_connections.all()
    output.extend([vp for vp in vpc_peerings
                   if vp.status.get('Code') == "active" and
                   (host_vpc is None or vp.accepter_vpc == host_vpc)])
    return output


@get_and_cleanup(get_vpc_peerings)
def cleanup_vpc_peering(vpc_peerings, **kwargs):
    for vpc_peering in vpc_peerings:
        vpc_peering.delete()


@get_or_create(get_vpc_peerings)
def create_vpc_peering(client, vpc, host_vpc, target_region):
    """Method would create a vpc peering between the newly created VPC and caller's VPC
    Args:
        client (boto client): Region specific boto client
        vpc (VPC object): Newly created VPC object
        host_vpc(Host VPC object): Host VPC to peer with.
        target_region (region name): Region name in which peering is being created.
    Returns:
        VPC peering (array): Array list of vpc peerings.
    """
    try:
        peer_conn = client.create_vpc_peering_connection(
            VpcId=host_vpc.id, PeerVpcId=vpc.id, PeerRegion=target_region)
        peer_conn.wait_until_exists()
        # Need to accept from the other end.
        remote_peer_conn = get_client(target_region).VpcPeeringConnection(peer_conn.id)
        remote_peer_conn.wait_until_exists()
        remote_peer_conn.accept()
        return [peer_conn]
    except Exception as e:
        logging.error(e)
        raise YBOpsRuntimeError("Unable to create VPC peering.")


def get_instance_details(instance_type, region):
    c = get_raw_client(region)
    instances = c.describe_instance_types(InstanceTypes=[instance_type]).get("InstanceTypes", [])
    if (len(instances) == 0):
        raise YBOpsRuntimeError("Could not find instance type {}".format(instance_type))
    return instances[0]


def get_device_names(instance_type, num_volumes, region, predefined_volumes):
    device_names = []
    instance = get_instance_details(instance_type, region)
    i = 0
    while len(device_names) < num_volumes:
        device_name_format = "nvme{}n1" if is_nvme(instance) else "xvd{}"
        index = "{}".format(i if is_nvme(instance) else chr(ord('b') + i))
        device_name = device_name_format.format(index)
        if not (device_name in predefined_volumes):
            device_names.append(device_name)
        i = i + 1
    logging.info("Device names: %s", device_names)
    return device_names


def is_ebs_only(instance):
    """
    Determines whether or not an instance only supports EBS volumes.
    Must be called on instance type dictionary details as returned by get_instance_details()
    """
    return not instance.get("InstanceStorageSupported")


def is_nvme(instance):
    """
    Determines whether or not an instance has instance storage.
    """
    return instance.get("InstanceStorageSupported")


def is_burstable(instance):
    """
    Determines whether or not an instance has burstable performance.
    """
    return instance.get("BurstablePerformanceSupported")


def has_ephemerals(instance_type, region):
    instance = get_instance_details(instance_type, region)
    return not is_nvme(instance) and not is_ebs_only(instance)


def __get_security_group(client, args):
    sg_ids = args.security_group_id.split(",") if args.security_group_id else None
    if sg_ids is None:
        # Figure out which VPC this instance will be brought up in and search for the SG in there.
        # This is for a bit of backwards compatibility with the previous mode of potentially using
        # YW's VPC, in which we would still deploy a SG with the same name as in our normal VPCs.
        # This means there could be customers that had that deployment mode from the start AND have
        # a SG we created back then, with the internal naming convention we use, but NOT in the YB
        # VPC (which they likely will not even have).
        vpc = get_vpc_for_subnet(client, args.cloud_subnet)
        sg_name = get_yb_sg_name(args.region)
        sg = get_security_group(client, sg_name, vpc)
        sg_ids = [sg.id]
    return sg_ids


def create_instance(args):
    client = get_client(args.region)
    instance = get_instance_details(args.instance_type, args.region)
    vars = {
        "ImageId": args.machine_image,
        "KeyName": args.key_pair_name,
        "MinCount": 1,
        "MaxCount": 1,
        "InstanceType": args.instance_type,
    }
    # Network setup.
    # Lets assume they have provided security group id comma delimited.
    sg_ids = __get_security_group(client, args)

    vars["NetworkInterfaces"] = [{
        "DeviceIndex": 0,
        "SubnetId": args.cloud_subnet,
        "Groups": sg_ids,
        "DeleteOnTermination": True
    }]
    if args.cloud_subnet_secondary:
        vars["NetworkInterfaces"].append({
            "DeviceIndex": 1,
            "SubnetId": args.cloud_subnet_secondary,
            "Groups": sg_ids,
            "DeleteOnTermination": True
        })
    # AWS limitation that no public IP can be assigned if using two network interfaces.
    else:
        vars["NetworkInterfaces"][0]["AssociatePublicIpAddress"] = args.assign_public_ip

    # Volume setup.
    volumes = []

    ami_descr = describe_ami(args.region, args.machine_image)
    root_volume_size = args.boot_disk_size_gb
    root_device_name = ami_descr.get("RootDeviceName")
    block_device_mappings = ami_descr.get("BlockDeviceMappings")
    if block_device_mappings:
        root_volume_info = [v.get("Ebs") for v in block_device_mappings
                            if v.get("DeviceName") == root_device_name]
        if root_volume_info and root_volume_info[0].get("VolumeSize") > root_volume_size:
            root_volume_size = root_volume_info[0].get("VolumeSize")
            logging.warning("Predefined boot volume has larger size: {} vs {}".format(
                root_volume_size, args.boot_disk_size_gb))

    ebs = {
        "DeleteOnTermination": args.auto_delete_boot_disk,
        "VolumeSize": root_volume_size,
        "VolumeType": args.volume_type
    }
    if args.volume_type == "io1" or args.volume_type == "gp3":
        ebs["Iops"] = args.disk_iops
    if args.volume_type == "gp3":
        ebs["Throughput"] = args.disk_throughput

    if args.cmk_res_name is not None:
        ebs["Encrypted"] = True
        ebs["KmsKeyId"] = args.cmk_res_name

    if args.iam_profile_arn is not None:
        vars["IamInstanceProfile"] = {
            "Arn": args.iam_profile_arn
        }

    volumes.append({
        "DeviceName": root_device_name,
        "Ebs": ebs
    })
    device_names = get_device_names(
        args.instance_type, args.num_volumes, args.region, get_predefined_devices(ami_descr))
    # TODO: Clean up semantics on nvme vs "next-gen" vs ephemerals, as this is currently whack...
    for i, device_name in enumerate(device_names):
        volume = {}
        if has_ephemerals(args.instance_type, args.region):
            volume = {
                "DeviceName": "/dev/{}".format(device_name),
                "VirtualName": "ephemeral{}".format(i)
            }
        elif is_ebs_only(instance):
            ebs = {
                "DeleteOnTermination": True,
                "VolumeType": args.volume_type,
                "VolumeSize": args.volume_size
            }
            if args.cmk_res_name is not None:
                ebs["Encrypted"] = True
                ebs["KmsKeyId"] = args.cmk_res_name
            if args.volume_type == "io1" or args.volume_type == "gp3":
                ebs["Iops"] = args.disk_iops
            if args.volume_type == "gp3":
                ebs["Throughput"] = args.disk_throughput
            volume = {
                "DeviceName": "/dev/{}".format(device_name),
                "Ebs": ebs
            }
        volumes.append(volume)
    vars["BlockDeviceMappings"] = volumes

    if args.boot_script:
        with open(args.boot_script, 'r') as script:
            vars["UserData"] = script.read()

    # Tag setup.
    def __create_tag(k, v):
        return {"Key": k, "Value": v}
    # Add Name all the time.
    instance_tags = [
        __create_tag("Name", args.search_pattern),
        __create_tag("launched-by", os.environ.get("USER", "unknown")),
        __create_tag("yb-server-type", args.type)
    ]
    custom_tags = args.instance_tags if args.instance_tags is not None else '{}'
    user_tags = []
    for k, v in json.loads(custom_tags).items():
        instance_tags.append(__create_tag(k, v))
        user_tags.append(__create_tag(k, v))
    resources_to_tag = [
        "network-interface", "volume"
    ]
    tag_dicts = []
    tag_dicts.append({
        "ResourceType": "instance",
        "Tags": instance_tags
    })
    if user_tags:
        for tagged_resource in resources_to_tag:
            resources_tag_dict = {
                "ResourceType": tagged_resource,
                "Tags": user_tags
            }
            tag_dicts.append(resources_tag_dict)
    vars["TagSpecifications"] = tag_dicts

    if args.use_spot_instance:
        options = {"MarketType": "spot"}
        spotOptions = {"SpotInstanceType": "persistent",
                       "InstanceInterruptionBehavior": "stop"}
        if args.spot_price is not None:
            spotOptions["MaxPrice"] = args.spot_price
        options["SpotOptions"] = spotOptions
        vars["InstanceMarketOptions"] = options
        logging.info(f"[app] Using AWS spot instances with {options} options")

    if args.imdsv2required:
        vars["MetadataOptions"] = {"HttpTokens": "required",
                                   "HttpEndpoint": "enabled"}

    # Newer instance types have Credit Specification set to unlimited by default
    if is_burstable(instance):
        vars["CreditSpecification"] = {
            "CpuCredits": 'standard'
        }

    # TODO: user_data > templates/cloud_init.yml.j2, still needed?
    logging.info("[app] About to create AWS VM {}. ".format(args.search_pattern))
    instance_ids = client.create_instances(**vars)
    if len(instance_ids) != 1:
        logging.error("Invalid create_instances response: {}".format(instance_ids))
        raise YBOpsRuntimeError("Expected to create 1 instance, got {}".format(
            len(instance_ids)))
    instance = instance_ids[0]
    instance.wait_until_running()

    logging.info("[app] AWS VM {} created.".format(args.search_pattern))

    if args.assign_static_public_ip:
        # Create elastic IP.
        eip_tags = list(user_tags)
        eip_tags.extend([
            __create_tag("Name", "ip-" + args.search_pattern),
            __create_tag("launched-by", os.environ.get("USER", "unknown")),
            __create_tag("Created", time.asctime(time.gmtime()))
        ])
        ec2_client = boto3.client("ec2", region_name=args.region)
        eip = ec2_client.allocate_address(
            Domain="vpc",
            TagSpecifications=[{
                "ResourceType": "elastic-ip",
                "Tags": eip_tags
            }]
        )
        # Re-fetch instance to get latest state.
        instance = client.Instance(instance.id)
        # Get instance's primary network interface and attach IP.
        interface_id = None
        # If secondary subnet, we want to set the public IP on the customer
        # facing NIC.
        req_index = 1 if args.cloud_subnet_secondary else 0
        for i in instance.network_interfaces:
            if i.attachment.get("DeviceIndex") == req_index:
                interface_id = i.id
                break
        # Associate elastic IP with instance.
        ec2_client.associate_address(
            AllocationId=eip["AllocationId"],
            NetworkInterfaceId=interface_id
        )
        logging.info("[app] Created Elastic IP address at {} in region {} for AWS VM {}"
                     .format(eip["PublicIp"], args.region, args.search_pattern))

    if args.use_spot_instance:
        logging.info(f"spot instance request id = {instance.spot_instance_request_id}")
        client.create_tags(
            Resources=[instance.spot_instance_request_id],
            Tags=user_tags
        )
    return instance.id


def get_predefined_devices(ami):
    result = []
    root_device_name = ami.get("RootDeviceName")
    block_device_mappings = ami.get("BlockDeviceMappings")
    if block_device_mappings:
        for v in block_device_mappings:
            if v.get("DeviceName") != root_device_name:
                result.append(v.get("DeviceName").replace("/dev/", ""))
    logging.info("Predefined devices: %s", result)
    return result


def modify_tags(region, instance_id, tags_to_set_str, tags_to_remove_str):
    instance = get_client(region).Instance(instance_id)
    client = boto3.client('ec2', region)
    # Remove all the tags we were asked to, except the internal ones.
    tags_to_remove = set(tags_to_remove_str.split(",") if tags_to_remove_str else [])
    # TODO: combine these with the above instance creation function.
    internal_tags = {"Name", "launched-by", "yb-server-type"}
    if tags_to_remove & internal_tags:
        raise YBOpsRuntimeError(
            "Was asked to remove tags: {}, which contain internal tags: {}".format(
                tags_to_remove, internal_tags
            ))

    resources_list = []
    if instance.spot_instance_request_id:
        resources_list.append(instance.spot_instance_request_id)
    for volume in instance.volumes.all():
        resources_list.append(volume.id)
    for network_interfaces in instance.network_interfaces_attribute:
        resources_list.append(network_interfaces.get('NetworkInterfaceId'))
        if 'Association' not in network_interfaces:
            continue
        public_ip_address = network_interfaces['Association'].get('PublicIp')
        if public_ip_address:
            elastic_ip_list = client.describe_addresses(
                Filters=[{'Name': 'public-ip', 'Values': [public_ip_address]}]
            )["Addresses"]
            for elastic_ip in elastic_ip_list:
                resources_list.append(elastic_ip["AllocationId"])

    # Note: passing an empty list to Tags will remove all tags from the instance.
    if tags_to_remove:
        to_remove_obj = [{"Key": k} for k in tags_to_remove]
        instance.delete_tags(Tags=to_remove_obj)
        client.delete_tags(Resources=resources_list, Tags=to_remove_obj)

    # Set all the tags provided.
    tags_to_set = json.loads(tags_to_set_str if tags_to_set_str else "{}")
    customer_tags = []
    for k, v in tags_to_set.items():
        customer_tags.append({"Key": k, "Value": v})
    instance.create_tags(Tags=customer_tags)
    client.create_tags(Resources=resources_list, Tags=customer_tags)


def update_disk(args, instance_id):
    ec2_client = boto3.client('ec2', region_name=args.region)
    instance = get_client(args.region).Instance(instance_id)
    ami_descr = describe_ami(args.region, args.machine_image if args.machine_image else instance.image_id)
    device_names = set(get_device_names(args.instance_type, args.num_volumes, args.region,
                                        get_predefined_devices(ami_descr)))
    vol_ids = list()
    for volume in instance.volumes.all():
        for attachment in volume.attachments:
            device_name = attachment['Device'].replace('/dev/', '')
            # Format of device name is /dev/xvd{} or /dev/nvme{}n1
            if device_name in device_names:
                modify_size = (args.force or
                               bool(args.volume_size and volume.size != args.volume_size))
                modify_iops = args.force or bool(args.disk_iops and volume.iops != args.disk_iops)
                modify_throughput = args.force or bool(args.disk_throughput and
                                                       volume.throughput != args.disk_throughput)
                if modify_size or modify_iops or modify_throughput:
                    logging.info(
                        "Existing instance %s's volume %s: size=%s, iops=%s, throughput=%s",
                        instance_id, volume.id, volume.size, volume.iops, volume.throughput)
                    _args = {"VolumeId": volume.id}

                    if modify_size:
                        if args.volume_size:
                            _args["Size"] = args.volume_size
                        elif volume.size:
                            _args["Size"] = volume.size
                    if modify_iops:
                        if args.disk_iops:
                            _args["Iops"] = args.disk_iops
                        elif volume.iops:
                            _args["Iops"] = volume.iops
                    if modify_throughput:
                        if args.disk_throughput:
                            _args["Throughput"] = args.disk_throughput
                        elif volume.throughput:
                            _args["Throughput"] = volume.throughput

                    logging.info(
                        "Modified volume %s: size=%s, iops=%s, throughput=%s",
                        volume.id, _args.get("Size", volume.size), _args.get("Iops", volume.iops),
                        _args.get("Throughput", volume.throughput))
                    vol_ids.append(volume.id)
                    ec2_client.modify_volume(**_args)

    # Wait for volumes to be ready.
    if vol_ids:
        _wait_for_disk_modifications(ec2_client, vol_ids)


def change_instance_type(region, instance_id, instance_type):
    instance = get_client(region).Instance(instance_id)

    try:
        # Change instance type
        instance.modify_attribute(Attribute='instanceType', Value=instance_type)
    except Exception as e:
        raise YBOpsRuntimeError('error executing \"instance.modify_attribute\": {}'.format(repr(e)))


def delete_route(rt, cidr):
    route = get_route_by_cidr(rt, cidr)
    if route is not None:
        route.delete()


def get_vpc_for_subnet(client, subnet):
    return client.Subnet(subnet).vpc


def get_yb_sg_name(region):
    return SG_YUGABYTE_PREFIX_FORMAT.format(region)


def list_dns_record_set(hosted_zone_id):
    return boto3.client('route53').get_hosted_zone(Id=hosted_zone_id)


def create_dns_record_set(hosted_zone_id, domain_name_prefix, ip_list):
    return _update_dns_record_set(hosted_zone_id, domain_name_prefix, ip_list, 'CREATE')


def edit_dns_record_set(hosted_zone_id, domain_name_prefix, ip_list):
    return _update_dns_record_set(hosted_zone_id, domain_name_prefix, ip_list, 'UPSERT')


def delete_dns_record_set(hosted_zone_id, domain_name_prefix, ip_list):
    return _update_dns_record_set(hosted_zone_id, domain_name_prefix, ip_list, 'DELETE')


def _update_dns_record_set(hosted_zone_id, domain_name_prefix, ip_list, action):
    client = boto3.client('route53')

    records = []
    for ip in ip_list:
        records.append({'Value': ip})
    result = list_dns_record_set(hosted_zone_id)
    hosted_zone_name = result['HostedZone']['Name']
    change_batch = {
        'Comment': "YugaWare driven Record Set",
        'Changes': [{
            'Action': action,
            'ResourceRecordSet': {
                'Name': "{}.{}".format(domain_name_prefix, hosted_zone_name),
                'Type': 'A',
                'TTL': DNS_RECORD_SET_TTL,
                'ResourceRecords': records
            }
        }]
    }
    result = client.change_resource_record_sets(
        HostedZoneId=hosted_zone_id,
        ChangeBatch=change_batch)
    client.get_waiter('resource_record_sets_changed').wait(
        Id=result['ChangeInfo']['Id'],
        WaiterConfig={
            'Delay': 10,
            'MaxAttempts': 60
        })


def _wait_for_disk_modifications(ec2_client, vol_ids):
    # This function returns as soon as the volume state is optimizing, not completed.
    num_vols_to_modify = len(vol_ids)
    # It should retry for a 1 hour time limit.
    retry_num = int((1 * 3600) / AbstractCloud.SERVER_WAIT_SECONDS) + 1
    # Loop till all volumes are modified or the limit is reached.
    num_vols_failed = 0

    while retry_num > 0:
        num_vols_modified = 0
        response = ec2_client.describe_volumes_modifications(VolumeIds=vol_ids)
        # The response format can be found here:
        # https://boto3.amazonaws.com/v1/documentation/api/latest/reference/services/ec2.html#EC2.Client.describe_volumes_modifications
        for entry in response["VolumesModifications"]:
            if entry["ModificationState"] == "failed":
                logging.error(
                    f"Modification of {entry['VolumeId']} failed: {entry['StatusMessage']}")
                num_vols_failed += 1
            elif entry["ModificationState"] in {"optimizing", "completed"}:
                # Modifying completed.
                num_vols_modified += 1

        # This means all volume modifications have succeeded/failed.
        if num_vols_modified + num_vols_failed == num_vols_to_modify:
            break

        num_vols_failed = 0
        time.sleep(AbstractCloud.SERVER_WAIT_SECONDS)
        retry_num -= 1

    if num_vols_failed:
        raise YBOpsRecoverableError(f"Failed to modify {num_vols_failed} volumes")

    if retry_num <= 0:
        raise YBOpsRuntimeError("wait_for_disk_modifications failed. Retry limit reached.")
