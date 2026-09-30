#!/bin/python3

from types import SimpleNamespace
from unittest import TestCase
from unittest.mock import MagicMock, patch

from ybops.cloud.azure.cloud import AzureCloud
from ybops.cloud.azure.utils import AzureCloudAdmin


class TestAzureCloudAdmin(TestCase):
    def test_get_yb_data_disk_lun_indexes(self):
        vm = SimpleNamespace(
            name="yb-universe-n1",
            storage_profile=SimpleNamespace(
                data_disks=[
                    SimpleNamespace(name="yb-universe-n1-Disk-10", lun=12),
                    SimpleNamespace(name="unrelated-disk", lun=0),
                    SimpleNamespace(name="yb-universe-n1-Disk-2", lun=5),
                    SimpleNamespace(name=None, lun=7),
                    SimpleNamespace(name="yb-universe-n1-Disk-1", lun=3),
                ]
            ),
        )

        admin = object.__new__(AzureCloudAdmin)
        self.assertEqual(admin._get_yb_data_disk_lun_indexes(vm), [3, 5, 12])

    @patch("ybops.cloud.azure.utils.RESOURCE_GROUP", "rg")
    @patch("ybops.cloud.azure.utils.SUBSCRIPTION_ID", "sub")
    def test_change_instance_type_with_reservation_updates_size_then_group(self):
        admin = object.__new__(AzureCloudAdmin)
        admin.compute_client = MagicMock()

        admin.change_instance_type("vm1", "Standard_D4as_v5", "crg1")

        updates = [call.args for call in
                   admin.compute_client.virtual_machines.begin_update.call_args_list]
        self.assertEqual(updates, [
            ("rg", "vm1", {"hardware_profile": {"vm_size": "Standard_D4as_v5"}}),
            ("rg", "vm1", {"properties": {"capacityReservation": {"capacityReservationGroup": {
                "id": "/subscriptions/sub/resourceGroups/rg/providers"
                      "/Microsoft.Compute/capacityReservationGroups/crg1"}}}}),
        ])

    @patch("ybops.cloud.azure.utils.RESOURCE_GROUP", "rg")
    def test_change_instance_type_without_reservation_updates_size_only(self):
        admin = object.__new__(AzureCloudAdmin)
        admin.compute_client = MagicMock()

        admin.change_instance_type("vm1", "Standard_D4as_v5", None)

        admin.compute_client.virtual_machines.begin_update.assert_called_once_with(
            "rg", "vm1", {"hardware_profile": {"vm_size": "Standard_D4as_v5"}})

    @patch("ybops.cloud.azure.cloud.RemoteShell")
    def test_expand_file_system_supports_nvme(self, remote_shell_class):
        remote_shell = remote_shell_class.return_value
        remote_shell.check_exec_command.side_effect = [
            "/dev/disk/by-uuid/data-disk\n",
            "/dev/nvme0n2\n",
            "",
            "",
        ]
        cloud = object.__new__(AzureCloud)
        args = SimpleNamespace(mount_points="/mnt/d0")

        cloud.expand_file_system(args, {"ssh_host": "10.0.0.1"})

        commands = [call.args[0] for call in remote_shell.check_exec_command.call_args_list]
        self.assertIn("findmnt -rn -M /mnt/d0 -o SOURCE", commands[0])
        self.assertIn("/sys/class/block/nvme0n2/device/rescan", commands[2])
        self.assertEqual(commands[3], "sudo xfs_growfs /mnt/d0")
