#!/bin/python3
#
# Copyright 2026 YugabyteDB, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

from unittest import TestCase
from unittest.mock import MagicMock, patch

from azure.core.exceptions import HttpResponseError
from azure.mgmt.compute.models import DiskCreateOption
import azure.mgmt.compute.models as compute_models
from msrest.serialization import Serializer

import ybops.cloud.azure.utils as azure_utils
from ybops.cloud.azure.utils import AzureCloudAdmin
from ybops.common.exceptions import YBOpsRuntimeError


def to_arm(parameters, model="VirtualMachine"):
    """Serialize a request body the way the SDK does, to check what actually reaches ARM."""
    classes = {k: v for k, v in compute_models.__dict__.items() if isinstance(v, type)}
    return Serializer(classes).body(parameters, model)


def as_vm(data_disks):
    """Stands in for the VM that virtual_machines.get() returns after creation."""
    vm = MagicMock()
    disks = []
    for data_disk in data_disks:
        disk = MagicMock()
        disk.lun = data_disk["lun"]
        # name is a reserved MagicMock constructor argument, so it has to be set afterwards.
        disk.name = data_disk["name"]
        disks.append(disk)
    vm.storage_profile.data_disks = disks
    return vm


def azure_error(code, message):
    error = HttpResponseError(message="({}) {}".format(code, message))
    error.error = MagicMock()
    error.error.code = code
    return error


LOCKED = azure_error(
    "OperationNotAllowed", "Disk is being attached to VM, therefore operation is not allowed.")


class TestAzureCreateVm(TestCase):
    """Covers the invariant that a VM is never created without its data disks."""

    VM_NAME = "yb-test-n1"
    NUM_VOLUMES = 4
    VOLUME_SIZE = 250
    REGION = "westeurope"
    TAGS = {"universe-uuid": "universe-1", "node-uuid": "node-1"}
    DISK_NAMES = ["yb-test-n1-Disk-{}".format(i) for i in range(1, NUM_VOLUMES + 1)]

    def setUp(self):
        patcher = patch.multiple(
            azure_utils,
            get_credentials=MagicMock(),
            ComputeManagementClient=MagicMock(),
            NetworkManagementClient=MagicMock(),
            validated_key_file=MagicMock(return_value="private-key"),
            format_rsa_key=MagicMock(return_value="ssh-rsa AAAA"))
        patcher.start()
        self.addCleanup(patcher.stop)

        self.admin = AzureCloudAdmin({})
        self.compute = MagicMock()
        self.admin.compute_client = self.compute
        self.compute.disks.begin_create_or_update.side_effect = self.create_disk

        self.vm_payloads = []
        self.compute.virtual_machines.begin_create_or_update.side_effect = self.create_vm
        self.compute.virtual_machines.get.side_effect = self.get_vm

    def create_disk(self, resource_group, disk_name, disk_params):
        created = MagicMock()
        created.id = "/disks/{}".format(disk_name)
        poller = MagicMock()
        poller.result.return_value = created
        return poller

    def create_vm(self, resource_group, vm_name, vm_parameters):
        self.vm_payloads.append(vm_parameters)
        return MagicMock()

    def get_vm(self, resource_group, vm_name, *args):
        return as_vm(self.vm_payloads[-1]["storage_profile"].get("dataDisks", []))

    def create_or_update_vm(self, **overrides):
        kwargs = dict(
            vm_name=self.VM_NAME, zone="1", num_vols=self.NUM_VOLUMES,
            private_key_file="/dev/null", volume_size=self.VOLUME_SIZE,
            instance_type="Standard_D4s_v5", ssh_user="yugabyte",
            image="OpenLogic:CentOS:7_8:7.8.2020051900", vol_type="premium_lrs",
            server_type="cluster-server", region=self.REGION, nic_id="/nics/nic1",
            tags=dict(self.TAGS), disk_iops=None, disk_throughput=None, spot_price=None,
            use_spot_instance=False, vm_custom={}, disk_custom={}, use_plan=False)
        kwargs.update(overrides)
        return self.admin.create_or_update_vm(**kwargs)

    def created_disks(self):
        return {call.args[1]: call.args[2] for call
                in self.compute.disks.begin_create_or_update.call_args_list}

    def data_disks(self):
        self.assertEqual(1, self.compute.virtual_machines.begin_create_or_update.call_count)
        return self.vm_payloads[0]["storage_profile"]["dataDisks"]

    def test_disks_are_attached_by_the_single_vm_call(self):
        # The whole point of the change: one call to Azure creates the VM with every data disk
        # already attached, so a VM can never exist without them.
        result = self.create_or_update_vm()

        data_disks = self.data_disks()
        self.assertEqual([0, 1, 2, 3], [d["lun"] for d in data_disks])
        self.assertEqual(self.DISK_NAMES, [d["name"] for d in data_disks])
        for lun, data_disk in enumerate(data_disks):
            self.assertEqual(DiskCreateOption.attach, data_disk["createOption"])
            self.assertEqual(
                "/disks/{}".format(self.DISK_NAMES[lun]), data_disk["managedDisk"]["id"])
        self.assertEqual({"lun_indexes": [0, 1, 2, 3]}, result)

    def test_disks_are_tagged_when_they_are_created(self):
        # Tagging in a follow up call would leave a window where a crash orphans an untagged
        # volume that no sweep can match.
        self.create_or_update_vm()

        created = self.created_disks()
        self.assertEqual(set(self.DISK_NAMES), set(created.keys()))
        self.assertEqual(0, self.compute.disks.begin_update.call_count)
        for disk_params in created.values():
            self.assertEqual(self.TAGS, disk_params["tags"])
            self.assertEqual(self.VOLUME_SIZE, disk_params["disk_size_gb"])
            self.assertEqual("Premium_LRS", disk_params["sku"]["name"])
            self.assertEqual(["1"], disk_params["zones"])

    def test_ultra_disks_carry_iops_and_throughput(self):
        # diskIOPSReadWrite/diskMBpsReadWrite are honoured only on the disk resource, never on the
        # VM's inline disk, so they have to be set when the disk is created.
        self.create_or_update_vm(vol_type="ultrassd_lrs", disk_iops=5000, disk_throughput=200)

        for disk_params in self.created_disks().values():
            self.assertEqual(5000, disk_params["disk_iops_read_write"])
            self.assertEqual(200, disk_params["disk_m_bps_read_write"])
            self.assertEqual("UltraSSD_LRS", disk_params["sku"]["name"])
        self.assertEqual(
            {"ultraSSDEnabled": True}, self.vm_payloads[0]["additionalCapabilities"])

    def test_iops_and_throughput_serialize_to_arm_disk_properties(self):
        # The SDK drops dict keys that do not match a Disk attribute without any error, so a
        # mistyped throughput key leaves Azure to apply its default (125 MB/s on Premium SSD v2).
        for vol_type in ("premiumv2_lrs", "ultrassd_lrs"):
            with self.subTest(vol_type=vol_type):
                self.compute.disks.begin_create_or_update.reset_mock()
                self.create_or_update_vm(vol_type=vol_type, disk_iops=5000, disk_throughput=375)

                for disk_params in self.created_disks().values():
                    properties = to_arm(disk_params, "Disk")["properties"]
                    self.assertEqual(5000, properties["diskIOPSReadWrite"])
                    self.assertEqual(375, properties["diskMBpsReadWrite"])

    def test_custom_disk_params_reach_the_disk_resource(self):
        # yb.azure.custom_params.disk targets the managed disk resource body.
        self.create_or_update_vm(disk_custom={"tier": "P30"})

        created = self.created_disks()
        self.assertEqual(self.NUM_VOLUMES, len(created))
        for disk_params in created.values():
            self.assertEqual("P30", disk_params["tier"])

    def test_failed_vm_creation_leaves_no_disks_behind(self):
        self.compute.virtual_machines.begin_create_or_update.side_effect = \
            YBOpsRuntimeError("allocation failed")

        with self.assertRaises(YBOpsRuntimeError):
            self.create_or_update_vm()

        deleted = [call.args[1] for call in self.compute.disks.begin_delete.call_args_list]
        self.assertEqual(self.DISK_NAMES, deleted)

    def test_failed_disk_creation_leaves_no_disks_behind(self):
        calls = []

        def fail_on_third(resource_group, disk_name, disk_params):
            calls.append(disk_name)
            if len(calls) == 3:
                raise YBOpsRuntimeError("disk quota exceeded")
            return self.create_disk(resource_group, disk_name, disk_params)

        self.compute.disks.begin_create_or_update.side_effect = fail_on_third

        with self.assertRaises(YBOpsRuntimeError):
            self.create_or_update_vm()

        deleted = [call.args[1] for call in self.compute.disks.begin_delete.call_args_list]
        self.assertEqual(self.DISK_NAMES[:2], deleted)
        self.assertEqual(0, self.compute.virtual_machines.begin_create_or_update.call_count)

    def test_missing_data_disk_after_creation_fails(self):
        self.compute.virtual_machines.get.side_effect = \
            lambda *args: as_vm([{"lun": 0, "name": self.DISK_NAMES[0]}])

        with self.assertRaises(YBOpsRuntimeError):
            self.create_or_update_vm()

    def test_vm_payload_serializes_to_arm_data_disks(self):
        # The dicts are handed straight to the SDK, so a mistyped key would be dropped silently and
        # bring back the diskless VM this change exists to prevent.
        self.create_or_update_vm()

        storage_profile = to_arm(self.vm_payloads[0])["properties"]["storageProfile"]
        self.assertEqual(self.NUM_VOLUMES, len(storage_profile["dataDisks"]))
        for lun, data_disk in enumerate(storage_profile["dataDisks"]):
            self.assertEqual(lun, data_disk["lun"])
            self.assertEqual(self.DISK_NAMES[lun], data_disk["name"])
            self.assertEqual("Attach", data_disk["createOption"])
            self.assertEqual(
                "/disks/{}".format(self.DISK_NAMES[lun]), data_disk["managedDisk"]["id"])


class TestAzureDestroyInstance(TestCase):
    """Covers the disks of a failed VM, which Azure keeps locked for a while after the VM delete."""

    VM_NAME = "yb-test-n1"
    DISK_NAMES = ["yb-test-n1-Disk-1", "yb-test-n1-Disk-2"]
    OS_DISK = "yb-test-n1-os"

    def setUp(self):
        patcher = patch.multiple(
            azure_utils,
            get_credentials=MagicMock(),
            ComputeManagementClient=MagicMock(),
            NetworkManagementClient=MagicMock())
        patcher.start()
        self.addCleanup(patcher.stop)
        sleep = patch.object(azure_utils.time, "sleep")
        self.sleep = sleep.start()
        self.addCleanup(sleep.stop)

        self.admin = AzureCloudAdmin({})
        self.compute = MagicMock()
        self.network = MagicMock()
        self.admin.compute_client = self.compute
        self.admin.network_client = self.network
        self.admin.is_public_ip_assigned = MagicMock(return_value=False)

        vm = as_vm([{"lun": i, "name": name} for i, name in enumerate(self.DISK_NAMES)])
        vm.storage_profile.os_disk.name = self.OS_DISK
        self.compute.virtual_machines.get.return_value = vm

        self.events = []
        self.compute.virtual_machines.begin_delete.side_effect = \
            lambda *args: self.events.append("vm") or MagicMock()
        self.network.network_interfaces.begin_delete.side_effect = \
            lambda *args: self.events.append("nic") or MagicMock()

    def fail_disk_deletes(self, errors_per_disk, on_wait=False):
        """Each disk delete raises the listed errors in turn, then succeeds."""
        attempts = {}

        def begin_delete(resource_group, disk_name):
            self.events.append(disk_name)
            attempt = attempts.get(disk_name, 0)
            attempts[disk_name] = attempt + 1
            errors = errors_per_disk.get(disk_name, [])
            error = errors[attempt] if attempt < len(errors) else None
            poller = MagicMock()
            if error is not None and not on_wait:
                raise error
            if error is not None:
                poller.wait.side_effect = error
            return poller

        self.compute.disks.begin_delete.side_effect = begin_delete
        return attempts

    def destroy(self):
        self.admin.destroy_instance(self.VM_NAME, "node-1")

    def test_disks_locked_after_vm_delete_are_retried(self):
        attempts = self.fail_disk_deletes({self.DISK_NAMES[0]: [LOCKED, LOCKED]})

        self.destroy()

        self.assertEqual("vm", self.events[0])
        self.assertEqual(3, attempts[self.DISK_NAMES[0]])
        self.assertEqual(1, attempts[self.DISK_NAMES[1]])
        self.assertEqual(1, attempts[self.OS_DISK])
        self.assertEqual(2, self.sleep.call_count)
        # The NIC goes last, once the disks are released.
        self.assertEqual("nic", self.events[-1])

    def test_lock_reported_by_wait_is_retried(self):
        attempts = self.fail_disk_deletes({self.DISK_NAMES[1]: [LOCKED]}, on_wait=True)

        self.destroy()

        self.assertEqual(2, attempts[self.DISK_NAMES[1]])
        self.assertEqual(1, self.sleep.call_count)

    def test_disk_not_found_counts_as_deleted(self):
        attempts = self.fail_disk_deletes(
            {self.DISK_NAMES[0]: [azure_error("ResourceNotFound", "Disk not found.")]})

        self.destroy()

        self.assertEqual(1, attempts[self.DISK_NAMES[0]])
        self.assertEqual(0, self.sleep.call_count)

    def test_other_disk_errors_are_raised_without_retry(self):
        error = azure_error("AuthorizationFailed", "Not allowed.")
        attempts = self.fail_disk_deletes({self.DISK_NAMES[0]: [error]})

        with self.assertRaises(HttpResponseError):
            self.destroy()

        self.assertEqual(1, attempts[self.DISK_NAMES[0]])
        self.assertEqual(0, self.sleep.call_count)

    def test_disk_still_locked_after_all_attempts_is_raised(self):
        attempts = self.fail_disk_deletes({self.DISK_NAMES[0]: [LOCKED] * 20})

        with self.assertRaises(HttpResponseError):
            self.destroy()

        self.assertEqual(10, attempts[self.DISK_NAMES[0]])
        self.assertEqual(9, self.sleep.call_count)
        self.assertNotIn("nic", self.events)

    def test_vm_without_os_disk_is_destroyed(self):
        # A VM whose allocation failed (e.g. ZonalAllocationFailed) never got an OS disk.
        self.compute.virtual_machines.get.return_value.storage_profile.os_disk.name = None
        attempts = self.fail_disk_deletes({self.DISK_NAMES[0]: [LOCKED]})

        self.destroy()

        self.assertEqual({self.DISK_NAMES[0]: 2, self.DISK_NAMES[1]: 1}, attempts)
        self.assertEqual("nic", self.events[-1])
