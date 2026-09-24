#!/bin/python3

from types import SimpleNamespace
from unittest import TestCase
from unittest.mock import patch

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
