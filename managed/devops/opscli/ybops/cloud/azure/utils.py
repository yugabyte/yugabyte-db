from azure.common.credentials import ServicePrincipalCredentials
from azure.mgmt.network import NetworkManagementClient
from azure.mgmt.resource import ResourceManagementClient
from azure.mgmt.compute import ComputeManagementClient
from azure.mgmt.compute.models import DiskCreateOption
from msrestazure.azure_exceptions import CloudError
from ybops.utils import is_valid_ip_address, validated_key_file, format_rsa_key, wait_for_ssh

import logging
import os

SUBSCRIPTION_ID = os.environ.get("AZURE_SUBSCRIPTION_ID")
RESOURCE_GROUP = os.environ.get("AZURE_RG")


def get_credentials():
        credentials = ServicePrincipalCredentials(
            client_id=os.environ.get("AZURE_CLIENT_ID"),
            secret=os.environ.get("AZURE_CLIENT_SECRET"),
            tenant=os.environ.get("AZURE_TENANT_ID")
        )
        return credentials


class AzureBootstrapClient():
    def __init__(self, resource_group, region, vNet, subnet, nic, ip):
        self.subid = SUBSCRIPTION_ID
        self.rg_name = RESOURCE_GROUP
        self.location = region
        self.credentials = get_credentials()
        self.network_client = NetworkManagementClient(self.credentials, self.subid)
        self.resource_group_client = ResourceManagementClient(self.credentials, self.subid)
        self.ip_address_name = ip
        self.vnet = vNet
        self.subnet = subnet
        self.nic = nic

    def create_resource_group(self):
        try:
            return self.resource_group_client.resource_groups.get(self.rg_name)
        except CloudError:
            pass
        resource_group_params = {'location': self.location}
        return self.resource_group_client.resource_groups.create_or_update(
            self.rg_name,
            resource_group_params
        ).result()

    def create_vnet(self, vNet, subnet):
        try:
            self.network_client.virtual_networks.get(self.rg_name, self.vnet)
            logging.debug("Skipping {} vnet creation".format(vNet))
            return
        except CloudError:
            pass

        self.create_subnet(vNet, subnet)

        vnet_params = {
            'location': self.location,
            'address_space': {
                'address_prefixes': ['10.0.0.0/16']
            },
            "subnets": [
                    {
                        "name": subnet,
                        "properties": {
                            "addressPrefix": "10.0.0.0/24"
                        }
                    }
                ],
        }
        creation_result = self.network_client.virtual_networks.create_or_update(
            self.rg_name,
            self.vnet,
            vnet_params
        )
        return creation_result.result()

    def create_subnet(self, vNet, subnet):
        try:
            self.network_client.subnets.get(self.rg_name, vNet, subnet)
            logging.debug("Skipping {} subnet creation".format(subnet))
            return
        except CloudError:
            pass
        subnet_params = {
            'address_prefix': '10.0.0.0/24'
        }
        creation_result = self.network_client.subnets.create_or_update(
            self.rg_name,
            vNet,
            subnet,
            subnet_params
        )

        return creation_result.result()

    def bootstrap(self):
        self.create_resource_group()
        print(self.create_vnet(self.vnet, self.subnet))


class AzureCloudAdmin():
    def __init__(self, metadata, region):
        self.region = region
        self.credentials = get_credentials()
        self.compute_client = ComputeManagementClient(self.credentials, SUBSCRIPTION_ID)
        self.network_client = NetworkManagementClient(self.credentials, SUBSCRIPTION_ID)

    def network(self, vNet, subnet, nic, ip):
        return AzureBootstrapClient(RESOURCE_GROUP, self.region, vNet, subnet, nic, ip)

    def appendDisk(self, vm, vmName, diskName, size, lun, zone):
        data_disk = self.compute_client.disks.create_or_update(
            RESOURCE_GROUP,
            diskName,
            {
                'location': self.region,
                'disk_size_gb': size,
                'creation_data': {
                    'create_option': DiskCreateOption.empty
                },
                'zones': [
                    zone
                ]
            }
        ).result()

        vm.storage_profile.data_disks.append({
            'lun': lun,
            'name': diskName,
            'create_option': DiskCreateOption.attach,
            'managed_disk': {
                'storageAccountType': 'Premium_LRS',
                'id': data_disk.id
            }
        })

        async_disk_attach = self.compute_client.virtual_machines.create_or_update(
            RESOURCE_GROUP,
            vmName,
            vm
        )

        async_disk_attach.wait()
        return async_disk_attach.result()

    def create_public_ip_address(self, vmName, zone):
        public_ip_addess_params = {
            'location': self.region,
            'public_ip_allocation_method': 'Dynamic',
            'zones': [
                zone
            ]
        }
        creation_result = self.network_client.public_ip_addresses.create_or_update(
            RESOURCE_GROUP,
            vmName + '-IP',
            public_ip_addess_params
        )
        return creation_result.result()

    def create_nic(self, vmName, subnetId, zone):
        publicIPAddress = self.create_public_ip_address(vmName, zone)
        nic_params = {
            'location': self.region,
            'ip_configurations': [{
                'name': vmName + '-IPConfig',
                'public_ip_address': publicIPAddress,
                'subnet': {
                    'id': subnetId
                }
            }]
        }
        creation_result = self.network_client.network_interfaces.create_or_update(
            RESOURCE_GROUP,
            vmName + '-NIC',
            nic_params
        )

        return creation_result.result()

    def destroy_instance(self, vmName, hostInfo):
        try:
            vm = self.compute_client.virtual_machines.get(RESOURCE_GROUP, vmName)
        except CloudError:
            print("Could not find VM with name {}".format(vmName))

        nic_name = hostInfo["nic"]
        public_ip_name = hostInfo["public_ip_name"]
        os_disk_name = vm.storage_profile.os_disk.name
        data_disks = vm.storage_profile.data_disks

        logging.debug("About to delete vm {}".format(vmName))
        delete_op1 = self.compute_client.virtual_machines.delete(RESOURCE_GROUP, vmName)
        delete_op1.wait()

        diskdels = []
        for disk in data_disks:
            logging.debug("About to delete disk {}".format(disk.name))
            disk_delop = self.compute_client.disks.delete(RESOURCE_GROUP, disk.name)
            diskdels.append(disk_delop)

        logging.debug("About to delete os disk {}".format(os_disk_name))
        disk_delop = self.compute_client.disks.delete(RESOURCE_GROUP, os_disk_name)
        diskdels.append(disk_delop)

        logging.debug("About to delete network interface {}".format(nic_name))
        delete_op2 = self.network_client.network_interfaces.delete(RESOURCE_GROUP, nic_name)
        delete_op2.wait()

        logging.debug("About to delete public ip {}".format(public_ip_name))
        delete_op3 = self.network_client.public_ip_addresses.delete(RESOURCE_GROUP, public_ip_name)
        delete_op3.wait()

        for diskdel in diskdels:
            diskdel.wait()

        print ("Sucessfully deleted {} and all related resources".format(vmName))
        return

    def create_vm(self, vmName, zone, numVolumes, subnetId, private_key_file, volume_size,
                  instanceType, sshUser, image):
        try:
            return self.compute_client.virtual_machines.get(RESOURCE_GROUP, vmName)
        except CloudError:
            pass

        nic = self.create_nic(vmName, subnetId, zone)

        diskNames = [vmName + "-Disk-" + str(i) for i in range(1, numVolumes + 1)]
        privateKey = validated_key_file(private_key_file)
        vm_parameters = {
            'location': self.region,
            'os_profile': {
                'computer_name': vmName,
                'admin_username': sshUser,
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
            'hardware_profile': {
                'vm_size': instanceType
            },
            'storage_profile': {
                'osDisk': {
                    'createOption': 'fromImage',
                    'managedDisk': {
                        'storageAccountType': 'Premium_LRS'
                    }
                },
                'image_reference': {
                        "publisher": "OpenLogic",
                        "offer": "CentOS",
                        "sku": "7_8",
                        "version": image
                }
            },
            'network_profile': {
                'network_interfaces': [{
                    'id': nic.id
                }]
            },
            'zones': [
                zone
            ]
        }

        creation_result = self.compute_client.virtual_machines.create_or_update(
            RESOURCE_GROUP,
            vmName,
            vm_parameters
        )

        vm_result = creation_result.result()
        vm = self.compute_client.virtual_machines.get(RESOURCE_GROUP, vmName)

        for idx, diskName in enumerate(diskNames):
            self.appendDisk(vm, vmName, diskName, volume_size, idx, zone)

        return

    def get_instance_types(self):
        vmSizeList = [vm.serialize() for vm in
                      self.compute_client.virtual_machine_sizes.list(location=self.region)]
        yugabyteVMS = {}
        for vm in vmSizeList:
            # DS VMs support SSDs and Promo is a pricing thing so ignored for now
            if (vm.get('name').startswith("Standard_DS") and not vm.get('name').endswith("Promo")):
                vmInfo = {}
                vmInfo["numCores"] = vm.get('numberOfCores')
                vmInfo["memSizeGb"] = float(vm.get('memoryInMB')) / 1000.0
                vmInfo["maxDiskCount"] = vm.get('maxDataDiskCount')
                yugabyteVMS[vm.get('name')] = vmInfo
        return yugabyteVMS

    def get_host_info(self, vmName, get_all=False):
        try:
            vm = self.compute_client.virtual_machines.get(RESOURCE_GROUP, vmName)
        except Exception as e:
            return None
        nicName = vm.network_profile.network_interfaces[0].id.split('/')[-1]
        nic = self.network_client.network_interfaces.get(RESOURCE_GROUP, nicName)
        region = vm.location
        if (vm.zones):
            zone = vm.zones[0]
        private_ip = nic.ip_configurations[0].private_ip_address
        public_ip_name = nic.ip_configurations[0].public_ip_address.id.split('/')[-1]
        public_ip = (self.network_client.public_ip_addresses
                     .get(RESOURCE_GROUP, public_ip_name).ip_address)
        subnet = nic.ip_configurations[0].subnet.id
        return {"private_ip": private_ip, "public_ip": public_ip, "region": region,
                "zone": zone, "name": vm.name, "instance_type": vm.hardware_profile.vm_size,
                "server_type": "cluster-server", "subnet": subnet, "nic": nicName,
                "public_ip_name": public_ip_name, "id": vm.name}
