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

import azure.mgmt.compute.models as compute_models
from msrest.serialization import Serializer

import ybops.cloud.azure.utils as azure_utils
from ybops.cloud.azure.utils import AzureCloudAdmin


def to_arm(parameters, model):
    """Serialize a request body the way the SDK does, to check what actually reaches ARM."""
    classes = {k: v for k, v in compute_models.__dict__.items() if isinstance(v, type)}
    return Serializer(classes).body(parameters, model)


class TestAzureAppendDisk(TestCase):

    def setUp(self):
        patcher = patch.multiple(
            azure_utils,
            get_credentials=MagicMock(),
            ComputeManagementClient=MagicMock(),
            NetworkManagementClient=MagicMock())
        patcher.start()
        self.addCleanup(patcher.stop)

        self.admin = AzureCloudAdmin({})
        self.compute = MagicMock()
        self.admin.compute_client = self.compute

    def created_disk_params(self):
        self.assertEqual(1, self.compute.disks.begin_create_or_update.call_count)
        return self.compute.disks.begin_create_or_update.call_args.args[2]

    def test_iops_and_throughput_serialize_to_arm_disk_properties(self):
        # The SDK drops dict keys that do not match a Disk attribute without any error, so a
        # mistyped throughput key leaves Azure to apply its default (125 MB/s on Premium SSD v2).
        for vol_type in ("premiumv2_lrs", "ultrassd_lrs"):
            with self.subTest(vol_type=vol_type):
                self.compute.disks.begin_create_or_update.reset_mock()
                vm = MagicMock()
                vm.storage_profile.data_disks = []
                self.admin.append_disk(
                    vm, "yb-test-n1", "yb-test-n1-Disk-1", 100, 0, "1", vol_type,
                    "westeurope", {}, 5000, 375, {})

                properties = to_arm(self.created_disk_params(), "Disk")["properties"]
                self.assertEqual(5000, properties["diskIOPSReadWrite"])
                self.assertEqual(375, properties["diskMBpsReadWrite"])
