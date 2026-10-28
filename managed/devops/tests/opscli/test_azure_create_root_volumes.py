#!/bin/python3
#
# Copyright 2026 YugabyteDB, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

from types import SimpleNamespace
from unittest import TestCase
from unittest.mock import MagicMock

from ybops.cloud.azure.method import AzureCreateRootVolumesMethod
from ybops.common.exceptions import YBOpsRuntimeError


class TestAzureCreateMasterVolume(TestCase):
    """Covers the temporary VM that create_root_volumes builds to get a boot disk."""

    VM_NAME = "abcdef-yb-test-n1"
    HOST_INFO = {"node_uuid": "node-1", "root_volume": "/disks/yb-test-n1_OS"}

    def setUp(self):
        base_command = MagicMock()
        self.cloud = base_command.cloud
        self.method = AzureCreateRootVolumesMethod(base_command)
        self.method.create_method = MagicMock()
        self.admin = self.cloud.get_admin.return_value
        self.args = SimpleNamespace(search_pattern=self.VM_NAME, node_uuid="node-1",
                                    assign_public_ip=True)

    def test_returns_root_volume_and_keeps_os_disk(self):
        self.cloud.get_host_info.return_value = dict(self.HOST_INFO)

        self.assertEqual("/disks/yb-test-n1_OS", self.method.create_master_volume(self.args))

        self.assertFalse(self.args.assign_public_ip)
        self.admin.destroy_instance.assert_called_once_with(self.VM_NAME, "node-1", True)

    def test_creation_error_is_raised_after_cleanup(self):
        # The VM can exist after a failed creation, e.g. in a failed provisioning state.
        creation_error = Exception("(AllocationFailed) Allocation failed.")
        self.method.create_method.run_create_instance.side_effect = creation_error
        self.cloud.get_host_info.return_value = dict(self.HOST_INFO)

        with self.assertRaises(Exception) as raised:
            self.method.create_master_volume(self.args)

        self.assertIs(creation_error, raised.exception)
        # The OS disk is deleted as well since it is not going to be used.
        self.admin.destroy_instance.assert_called_once_with(self.VM_NAME, "node-1", False)

    def test_orphaned_resources_are_cleaned_up_when_vm_does_not_exist(self):
        creation_error = Exception("(QuotaExceeded) Operation could not be completed.")
        self.method.create_method.run_create_instance.side_effect = creation_error
        self.cloud.get_host_info.return_value = None

        with self.assertRaises(Exception) as raised:
            self.method.create_master_volume(self.args)

        self.assertIs(creation_error, raised.exception)
        self.admin.destroy_instance.assert_not_called()
        # The NIC is created before the VM, so it is left behind when the VM is not.
        self.admin.destroy_orphaned_resources.assert_called_once_with(self.VM_NAME, "node-1")

    def test_creation_error_is_not_replaced_by_orphan_cleanup_error(self):
        creation_error = Exception("(QuotaExceeded) Operation could not be completed.")
        self.method.create_method.run_create_instance.side_effect = creation_error
        self.cloud.get_host_info.return_value = None
        self.admin.destroy_orphaned_resources.side_effect = Exception("(NicReservedForAnotherVm)")

        with self.assertRaises(Exception) as raised:
            self.method.create_master_volume(self.args)

        self.assertIs(creation_error, raised.exception)

    def test_creation_error_is_not_replaced_by_cleanup_error(self):
        creation_error = Exception("(AllocationFailed) Allocation failed.")
        self.method.create_method.run_create_instance.side_effect = creation_error
        self.cloud.get_host_info.return_value = dict(self.HOST_INFO)
        self.admin.destroy_instance.side_effect = Exception("(OperationNotAllowed) Disk attached.")

        with self.assertRaises(Exception) as raised:
            self.method.create_master_volume(self.args)

        self.assertIs(creation_error, raised.exception)

    def test_missing_vm_after_creation_raises_a_clear_error(self):
        self.cloud.get_host_info.return_value = None

        with self.assertRaisesRegex(YBOpsRuntimeError, self.VM_NAME):
            self.method.create_master_volume(self.args)

        self.admin.destroy_instance.assert_not_called()
