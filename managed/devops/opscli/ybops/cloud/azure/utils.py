# Copyright 2020 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

from azure.common.credentials import ServicePrincipalCredentials
from azure.mgmt.network import NetworkManagementClient
from azure.mgmt.resource import ResourceManagementClient
from azure.mgmt.compute import ComputeManagementClient
from azure.mgmt.compute.models import DiskCreateOption
from msrestazure.azure_exceptions import CloudError
from ybops.utils import is_valid_ip_address, validated_key_file, format_rsa_key, wait_for_ssh
from ybops.common.exceptions import YBOpsRuntimeError

import logging
import os
import re
from collections import defaultdict

SUBSCRIPTION_ID = os.environ.get("AZURE_SUBSCRIPTION_ID")
RESOURCE_GROUP = os.environ.get("AZURE_RG")
SUBNET_ID_FORMAT_STRING = "/subscriptions/{}/resourceGroups/{}/providers/Microsoft.Network/virtualNetworks/{}/subnets/{}"
NSG_ID_FORMAT_STRING = "/subscriptions/{}/resourceGroups/{}/providers/Microsoft.Network/networkSecurityGroups/{}"
AZURE_SKU_FORMAT = {"premium_lrs": "Premium_LRS",
                    "standardssd_lrs": "StandardSSD_LRS",
                    "ultrassd_lrs": "UltraSSD_LRS"}


def get_credentials():
    credentials = ServicePrincipalCredentials(
        client_id=os.environ.get("AZURE_CLIENT_ID"),
        secret=os.environ.get("AZURE_CLIENT_SECRET"),
        tenant=os.environ.get("AZURE_TENANT_ID")
    )
    return credentials


def create_resource_group(resource_group, region):
    resource_group_client = ResourceManagementClient(get_credentials(), SUBSCRIPTION_ID)
    if resource_group_client.resource_groups.check_existence(resource_group):
        return
    resource_group_params = {'location': region}
    return self.resource_group_client.resource_groups.create_or_update(
        self.resource_group,
        resource_group_params
    )


class AzureBootstrapClient():
    def __init__(self, resource_group, region, regionMeta, network, metadata):
        self.subid = SUBSCRIPTION_ID
        self.resource_group = RESOURCE_GROUP
        self.region = region
        self.credentials = get_credentials()
        self.network_client = network
        self.regionMeta = regionMeta
        self.metadata = metadata

    def create_vnet(self, vNet, subnet):
        try:
            self.network_client.virtual_networks.get(self.resource_group, self.vnet)
            logging.debug("Skipping {} vnet creation".format(vNet))
            self.create_subnet(vNet, subnet)
            return
        except CloudError:
            pass

        self.create_subnet(vNet, subnet)

        vnet_params = {
            'location': self.region,
            'address_space': {
                'address_prefixes': ['10.0.0.0/16']
            },
        }
        creation_result = self.network_client.virtual_networks.create_or_update(
            self.resource_group,
            self.vnet,
            vnet_params
        )
        return creation_result.result()

    def create_subnet(self, vNet, subnet):
        try:
            self.network_client.subnets.get(self.resource_group, vNet, subnet)
            logging.debug("Skipping {} subnet creation".format(subnet))
            return
        except CloudError:
            pass
        subnet_params = {
            'address_prefix': '10.0.0.0/24'
        }
        creation_result = self.network_client.subnets.create_or_update(
            self.resource_group,
            vNet,
            subnet,
            subnet_params
        )

        return creation_result.result()

    def bootstrap(self):
        # TODO: Implement bootstrapping of default vnets/subnets/nsg
        raise YBOpsRuntimeError("Automatic bootstrapping not supported for Azure")
        return {}

    def from_user_json(self):
        regInfo = {}
        regInfo["vpc_id"] = self.regionMeta.get("vpcId")
        sg = self.regionMeta.get("customSecurityGroupId")
        regInfo["security_group"] = [{"id": sg, "name": sg}]
        azSubnets = self.regionMeta.get("azToSubnetIds")
        zonenets = {az: subnetId for az, subnetId in azSubnets.items()}
        regInfo["zones"] = zonenets
        return regInfo


class AzureCloudAdmin():
    def __init__(self, metadata):
        self.metadata = metadata
        self.resource_group = RESOURCE_GROUP
        self.credentials = get_credentials()
        self.compute_client = ComputeManagementClient(self.credentials, SUBSCRIPTION_ID)
        self.network_client = NetworkManagementClient(self.credentials, SUBSCRIPTION_ID)

    def network(self, region, perRegionMeta={}):
        return AzureBootstrapClient(self.resource_group, region, perRegionMeta, self.network_client,
                                    self.metadata)

    def appendDisk(self, vm, vmName, diskName, size, lun, zone, volType, region):
        data_disk = self.compute_client.disks.create_or_update(
            self.resource_group,
            diskName,
            {
                "location": region,
                "disk_size_gb": size,
                "creation_data": {
                    "create_option": DiskCreateOption.empty
                },
                "sku": {
                    "name": AZURE_SKU_FORMAT[volType]
                },
                "zones": [
                    zone
                ]
            }
        ).result()

        vm.storage_profile.data_disks.append({
            "lun": lun,
            "name": diskName,
            "create_option": DiskCreateOption.attach,
            "managed_disk": {
                "storageAccountType": AZURE_SKU_FORMAT[volType],
                "id": data_disk.id
            }
        })

        async_disk_attach = self.compute_client.virtual_machines.create_or_update(
            self.resource_group,
            vmName,
            vm
        )

        async_disk_attach.wait()
        return async_disk_attach.result()

    def get_public_ip_name(self, vmName):
        return vmName + '-IP'

    def get_nic_name(self, vmName):
        return vmName + '-NIC'

    def create_public_ip_address(self, vmName, zone, region):
        public_ip_addess_params = {
            "location": region,
            "sku": {
                "name": "Standard"  # Only standard SKU supports zone
            },
            "public_ip_allocation_method": "Static",
            "zones": [
                zone
            ]
        }
        creation_result = self.network_client.public_ip_addresses.create_or_update(
            self.resource_group,
            self.get_public_ip_name(vmName),
            public_ip_addess_params
        )
        return creation_result.result()

    def create_nic(self, vmName, subnetId, zone, nsgId, region, public_ip):
        nic_params = {
            "location": region,
            "ip_configurations": [{
                "name": vmName + "-IPConfig",
                "subnet": {
                    "id": subnetId
                },
            }],
        }
        if public_ip:
            publicIPAddress = self.create_public_ip_address(vmName, zone, region)
            nic_params["ip_configurations"][0]["public_ip_address"] = publicIPAddress
        if nsgId:
            nic_params['networkSecurityGroup'] = {'id': nsgId}
        creation_result = self.network_client.network_interfaces.create_or_update(
            self.resource_group,
            self.get_nic_name(vmName),
            nic_params
        )

        return creation_result.result()

    def destroy_instance(self, vmName, hostInfo):
        if not hostInfo:
            try:
                logging.info("Could not find VM {}. Deleting network interface and public IP."
                             .format(vmName))
                nic_name = self.get_nic_name(vmName)
                ip_name = self.get_public_ip_name(vmName)
                nicdel = self.network_client.network_interfaces.delete(RESOURCE_GROUP, nic_name)
                nicdel.wait()
                ipdel = self.network_client.public_ip_addresses.delete(RESOURCE_GROUP, ip_name)
                ipdel.wait()
                logging.info("Sucessfully deleted related resources from {}".format(vmName))
                return
            except CloudError:
                logging.error("Error deleting Network Interface or Public IP when {} not found"
                              .format(vmName))

        # Since we have hostInfo, we know the virtual machine exists
        vm = self.compute_client.virtual_machines.get(self.resource_group, vmName)

        nic_name = hostInfo.get("nic", None)
        public_ip_name = hostInfo.get("public_ip_name", None)
        os_disk_name = vm.storage_profile.os_disk.name
        data_disks = vm.storage_profile.data_disks

        logging.debug("About to delete vm {}".format(vmName))
        delete_op1 = self.compute_client.virtual_machines.delete(self.resource_group, vmName)
        delete_op1.wait()

        diskdels = []
        for disk in data_disks:
            logging.debug("About to delete disk {}".format(disk.name))
            disk_delop = self.compute_client.disks.delete(self.resource_group, disk.name)
            diskdels.append(disk_delop)

        logging.debug("About to delete os disk {}".format(os_disk_name))
        disk_delop = self.compute_client.disks.delete(self.resource_group, os_disk_name)
        diskdels.append(disk_delop)

        logging.debug("About to delete network interface {}".format(nic_name))
        delete_op2 = self.network_client.network_interfaces.delete(self.resource_group, nic_name)
        delete_op2.wait()

        logging.debug("About to delete public ip {}".format(public_ip_name))
        if (public_ip_name):
            delete_op3 = self.network_client.public_ip_addresses.delete(self.resource_group,
                                                                        public_ip_name)
            delete_op3.wait()

        for diskdel in diskdels:
            diskdel.wait()

        logging.info("Sucessfully deleted {} and all related resources".format(vmName))
        return

    def get_subnet_id(self, vnet, subnet):
        return SUBNET_ID_FORMAT_STRING.format(
            SUBSCRIPTION_ID, self.resource_group, vnet, subnet
        )

    def get_nsg_id(self, nsg):
        if nsg:
            return NSG_ID_FORMAT_STRING.format(
                SUBSCRIPTION_ID, self.resource_group, nsg
            )
        else:
            return

    def add_tag_resource(self, params, key, value):
        result = params.get("tags", {})
        result[key] = value
        params["tags"] = result
        return params

    def create_vm(self, vmName, zone, numVolumes, subnet, private_key_file, volume_size,
                  instanceType, sshUser, image, nsg, pub, offer, sku, vnet, volType, serverType,
                  region, public_ip):
        try:
            return self.compute_client.virtual_machines.get(self.resource_group, vmName)
        except CloudError:
            pass

        nic = self.create_nic(vmName, self.get_subnet_id(vnet, subnet),
                              zone, self.get_nsg_id(nsg), region, public_ip)

        diskNames = [vmName + "-Disk-" + str(i) for i in range(1, numVolumes + 1)]
        privateKey = validated_key_file(private_key_file)
        vm_parameters = {
            "location": region,
            "os_profile": {
                "computer_name": vmName,
                "admin_username": sshUser,
                "linux_configuration": {
                    "disable_password_authentication": True,
                    "ssh": {
                        "public_keys": [{
                            "path": "/home/{}/.ssh/authorized_keys".format(sshUser),
                            "key_data": format_rsa_key(privateKey, public_key=True)
                        }]
                    }
                }
            },
            "hardware_profile": {
                "vm_size": instanceType
            },
            "storage_profile": {
                "osDisk": {
                    "createOption": "fromImage",
                    "managedDisk": {
                        "storageAccountType": "Standard_LRS"
                    }
                },
                "image_reference": {
                        "publisher": pub,
                        "offer": offer,
                        "sku": sku,
                        "version": image
                }
            },
            "network_profile": {
                "network_interfaces": [{
                    "id": nic.id
                }]
            },
            "zones": [
                zone
            ]
        }

        if (volType == "ultrassd_lrs"):
            vm_parameters["additionalCapabilities"] = {"ultraSSDEnabled": True}

        self.add_tag_resource(vm_parameters, "yb-server-type", serverType)

        creation_result = self.compute_client.virtual_machines.create_or_update(
            self.resource_group,
            vmName,
            vm_parameters
        )

        vm_result = creation_result.result()
        vm = self.compute_client.virtual_machines.get(self.resource_group, vmName)

        for idx, diskName in enumerate(diskNames):
            self.appendDisk(vm, vmName, diskName, volume_size, idx, zone, volType, region)

        return

    def query_vpc(self):
        """
        TODO: Implement this once we get custom Azure region support
        """
        return {}

    def get_zones(self, region):
        return ["{}-{}".format(region, zone)
                for zone in self.metadata["regions"].get(region, {}).get("zones", [])]

    def get_zone_to_subnets(self, vnet, region):
        zones = self.get_zones(region)
        subnet = self.get_default_subnet(vnet)
        return {zone: subnet for zone in zones}

    def parse_vm_info(self, vm):
        vmInfo = {}
        vmInfo["numCores"] = vm.get("numberOfCores")
        vmInfo["memSizeGb"] = float(vm.get("memoryInMB")) / 1000.0
        vmInfo["maxDiskCount"] = vm.get("maxDataDiskCount")
        return vmInfo

    def get_instance_types(self, regions):
        # This regex probably should be refined? It returns a LOT of VMs right now.
        premium_regex_format = 'Standard_.*s'
        regex = re.compile(premium_regex_format, re.IGNORECASE)

        allVms = {}
        regionsPresent = defaultdict(set)

        for region in regions:
            vmList = [vm.serialize() for vm in
                      self.compute_client.virtual_machine_sizes.list(location=region)]
            for vm in vmList:
                vmSize = vm.get("name")
                # We only care about VMs that support Premium storage. Promo is pricing special.
                if (not regex.match(vmSize) or vmSize.endswith("Promo")):
                    continue
                allVms[vmSize] = self.parse_vm_info(vm)
                regionsPresent[vmSize].add(region)

        numRegions = len(regions)
        # Only return VMs that are present in all regions
        return {vm: info for vm, info in allVms.items() if len(regionsPresent[vm]) == numRegions}

    def get_default_vnet(self, region):
        vnets = [resource.serialize() for resource in
                 self.network_client.virtual_networks.list(self.resource_group)]
        for vnetJson in vnets:
            # parse vnet from ID
            vnetName = str(vnetJson.get("id").split('/')[-1])
            if (vnetJson.get("location") == region and vnetName.startswith("yugabyte-vnet")):
                return vnetName
        raise YBOpsRuntimeError(
            "Could not find default yugabyte-vnet in region {}".format(region))

    def get_default_subnet(self, vnet):
        subnets = [resource.serialize() for resource in
                   self.network_client.subnets.list(self.resource_group, vnet)]
        for subnet in subnets:
            # Maybe change to tags rather than filtering on name prefix
            if(subnet.get("name").startswith("default")):
                return subnet.get("name")

    def get_host_info(self, vmName, get_all=False):
        try:
            vm = self.compute_client.virtual_machines.get(self.resource_group, vmName)
        except Exception as e:
            return None
        nicName = vm.network_profile.network_interfaces[0].id.split('/')[-1]
        nic = self.network_client.network_interfaces.get(self.resource_group, nicName)
        region = vm.location
        zone = vm.zones[0] if vm.zones else None
        private_ip = nic.ip_configurations[0].private_ip_address
        public_ip = None
        if (nic.ip_configurations[0].public_ip_address):
            public_ip_name = nic.ip_configurations[0].public_ip_address.id.split('/')[-1]
            public_ip = (self.network_client.public_ip_addresses
                         .get(self.resource_group, public_ip_name).ip_address)
        subnet = nic.ip_configurations[0].subnet.id
        serverType = vm.tags.get("yb-server-type", None) if vm.tags else None
        return {"private_ip": private_ip, "public_ip": public_ip, "region": region,
                "zone": "{}-{}".format(region, zone), "name": vm.name,
                "instance_type": vm.hardware_profile.vm_size, "server_type": serverType,
                "subnet": subnet, "nic": nicName, "id": vm.name}
