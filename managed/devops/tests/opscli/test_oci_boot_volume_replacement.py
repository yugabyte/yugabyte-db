#!/bin/python3
#
# Copyright 2026 YugabyteDB, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

import json
from types import SimpleNamespace
from unittest import TestCase
from unittest.mock import MagicMock, call, patch

from ybops.cloud.oci.cloud import OciCloud
from ybops.cloud.oci.method import OciReplaceRootVolumeMethod
from ybops.cloud.oci.utils import OciCloudAdmin
from ybops.common.exceptions import YBOpsRuntimeError

INSTANCE_ID = "ocid1.instance.oc1..node"
IMAGE_ID = "ocid1.image.oc1..target"
NODE_TAGS = {"universe-uuid": "universe-1", "node-uuid": "node-1"}


def new_admin():
    admin = object.__new__(OciCloudAdmin)
    admin._compute_client = MagicMock()
    admin._blockstorage_client = MagicMock()
    admin._compartment_id = "ocid1.compartment.oc1..yb"
    admin._list_all = lambda list_func, *args, **kwargs: list_func(*args, **kwargs)
    return admin


class TestReplaceBootVolume(TestCase):
    def setUp(self):
        self.admin = new_admin()
        self.compute = self.admin._compute_client
        self.blockstorage = self.admin._blockstorage_client
        self.compute.get_instance.return_value.data = SimpleNamespace(
            availability_domain="AD-1",
            compartment_id="ocid1.compartment.oc1..yb",
            freeform_tags=dict(NODE_TAGS, Name="yb-n1"))
        self.compute.list_boot_volume_attachments.return_value = [
            SimpleNamespace(lifecycle_state="DETACHED", boot_volume_id="older"),
            SimpleNamespace(lifecycle_state="ATTACHED", boot_volume_id="current"),
        ]
        self.blockstorage.get_boot_volume.return_value.data = SimpleNamespace(
            id="current", freeform_tags={"team": "db"}, size_in_gbs=100,
            kms_key_id="ocid1.key.oc1..cmk", image_id="ocid1.image.oc1..current")

        def update_instance(*_):
            # The old volume must be tagged before OCI detaches it.
            self.assertTrue(self.blockstorage.update_boot_volume.called)
            return SimpleNamespace(headers={"opc-work-request-id": "work-request-1"})

        self.compute.update_instance.side_effect = update_instance
        self.work_requests = MagicMock()
        self.work_requests.list_work_requests.return_value = []
        self.work_requests.get_work_request.return_value.data = SimpleNamespace(
            status="SUCCEEDED")
        self.admin._build_client = MagicMock(return_value=self.work_requests)

    def test_replaces_from_image_keeping_size_and_key(self):
        self.admin.replace_boot_volume(INSTANCE_ID, IMAGE_ID)

        (volume_id, tag_details), _ = self.blockstorage.update_boot_volume.call_args
        self.assertEqual("current", volume_id)
        self.assertEqual(dict(NODE_TAGS, team="db"), tag_details.freeform_tags)
        (instance_id, details), _ = self.compute.update_instance.call_args
        self.assertEqual(INSTANCE_ID, instance_id)
        self.assertEqual(IMAGE_ID, details.source_details.image_id)
        self.assertEqual(100, details.source_details.boot_volume_size_in_gbs)
        self.assertEqual("ocid1.key.oc1..cmk", details.source_details.kms_key_id)
        self.assertTrue(details.source_details.is_preserve_boot_volume_enabled)
        self.work_requests.get_work_request.assert_called_once_with("work-request-1")

    def test_raises_boot_volume_below_minimum_to_minimum(self):
        self.blockstorage.get_boot_volume.return_value.data.size_in_gbs = 47

        self.admin.replace_boot_volume(INSTANCE_ID, IMAGE_ID)

        (_, details), _ = self.compute.update_instance.call_args
        self.assertEqual(50, details.source_details.boot_volume_size_in_gbs)

    def test_leaves_tags_of_a_volume_that_has_the_node_tags(self):
        # A YBA node's boot volume at OCI's limit of 10 freeform tags.
        self.blockstorage.get_boot_volume.return_value.data.freeform_tags = dict(
            NODE_TAGS, **{"tag-{}".format(i): "value" for i in range(8)})
        self.compute.update_instance.side_effect = None
        self.compute.update_instance.return_value = SimpleNamespace(
            headers={"opc-work-request-id": "work-request-1"})

        self.admin.replace_boot_volume(INSTANCE_ID, IMAGE_ID)

        self.blockstorage.update_boot_volume.assert_not_called()
        self.compute.update_instance.assert_called_once()

    def test_failed_work_request_reports_its_errors(self):
        self.work_requests.get_work_request.return_value.data.status = "FAILED"
        self.work_requests.list_work_request_errors.return_value = [
            SimpleNamespace(message="Invalid image")]

        with self.assertRaises(YBOpsRuntimeError) as ctx:
            self.admin.replace_boot_volume(INSTANCE_ID, IMAGE_ID)
        self.assertIn("ended FAILED: Invalid image", str(ctx.exception))

    @patch("ybops.cloud.oci.utils.time.sleep")
    def test_polls_work_request_at_a_fixed_interval(self, sleep):
        self.work_requests.get_work_request.side_effect = [
            SimpleNamespace(data=SimpleNamespace(status=status))
            for status in ("ACCEPTED", "IN_PROGRESS", "SUCCEEDED")]

        self.admin.replace_boot_volume(INSTANCE_ID, IMAGE_ID)

        self.assertEqual([call(10), call(10)], sleep.call_args_list)

    @patch("ybops.cloud.oci.utils.time.sleep")
    @patch("ybops.cloud.oci.utils.time.time", side_effect=[0, 0, 599, 600])
    def test_stuck_work_request_times_out(self, _, sleep):
        self.work_requests.get_work_request.return_value.data.status = "IN_PROGRESS"

        with self.assertRaises(YBOpsRuntimeError) as ctx:
            self.admin._wait_for_work_request(self.work_requests, "work-request-1")
        self.assertIn("Timeout waiting for work request work-request-1", str(ctx.exception))
        self.assertEqual(2, self.work_requests.get_work_request.call_count)

    def test_leaves_boot_volume_already_from_image(self):
        self.blockstorage.get_boot_volume.return_value.data.image_id = IMAGE_ID

        self.admin.replace_boot_volume(INSTANCE_ID, IMAGE_ID)

        self.blockstorage.update_boot_volume.assert_not_called()
        self.compute.update_instance.assert_not_called()

    def test_force_replaces_boot_volume_already_from_image(self):
        self.blockstorage.get_boot_volume.return_value.data.image_id = IMAGE_ID

        self.admin.replace_boot_volume(INSTANCE_ID, IMAGE_ID, force=True)

        self.compute.update_instance.assert_called_once()

    def test_waits_for_unfinished_work_request_before_checking(self):
        def finish_earlier_replacement(work_request_id):
            self.blockstorage.get_boot_volume.return_value.data.image_id = IMAGE_ID
            return SimpleNamespace(data=SimpleNamespace(status="SUCCEEDED"))

        self.work_requests.get_work_request.side_effect = finish_earlier_replacement
        self.work_requests.list_work_requests.return_value = [
            SimpleNamespace(id="earlier", status="IN_PROGRESS", operation_type="UpdateInstance"),
            SimpleNamespace(id="launch", status="SUCCEEDED", operation_type="LaunchInstance"),
        ]

        self.admin.replace_boot_volume(INSTANCE_ID, IMAGE_ID)

        self.work_requests.list_work_requests.assert_called_once_with(
            "ocid1.compartment.oc1..yb", resource_id=INSTANCE_ID)
        self.work_requests.get_work_request.assert_called_once_with("earlier")
        self.compute.update_instance.assert_not_called()

    def test_missing_boot_volume_fails_before_update(self):
        self.compute.list_boot_volume_attachments.return_value = []

        with self.assertRaises(YBOpsRuntimeError):
            self.admin.replace_boot_volume(INSTANCE_ID, IMAGE_ID)
        self.compute.update_instance.assert_not_called()


class TestDeleteDetachedBootVolumes(TestCase):
    def setUp(self):
        self.admin = new_admin()
        self.admin.resolve_availability_domain = MagicMock(return_value="AD-1")

    def test_deletes_only_detached_volumes_of_the_node(self):
        def boot_volume(volume_id, tags, state="AVAILABLE"):
            return SimpleNamespace(
                id=volume_id, display_name=volume_id, freeform_tags=tags, lifecycle_state=state)

        self.admin._blockstorage_client.list_boot_volumes.return_value = [
            boot_volume("detached", dict(NODE_TAGS, Name="yb-n1")),
            boot_volume("attached", NODE_TAGS),
            boot_volume("other-node", dict(NODE_TAGS, **{"node-uuid": "node-2"})),
            boot_volume("untagged", None),
            boot_volume("terminating", NODE_TAGS, state="TERMINATING"),
        ]
        self.admin._compute_client.list_boot_volume_attachments.side_effect = (
            lambda availability_domain, compartment_id, boot_volume_id: [
                SimpleNamespace(
                    lifecycle_state="ATTACHED" if boot_volume_id == "attached" else "DETACHED")
            ])

        self.assertEqual(
            ["detached"], self.admin.delete_detached_boot_volumes("ad-1", NODE_TAGS))
        self.admin._blockstorage_client.delete_boot_volume.assert_called_once_with("detached")

    def test_requires_tags(self):
        with self.assertRaises(YBOpsRuntimeError):
            self.admin.delete_detached_boot_volumes("ad-1", {})
        self.admin._blockstorage_client.list_boot_volumes.assert_not_called()


class TestDeleteVolumes(TestCase):
    def setUp(self):
        self.cloud = object.__new__(OciCloud)
        self.admin = MagicMock()
        self.admin.delete_detached_boot_volumes.return_value = []
        self.admin.list_volumes_by_tags.return_value = [
            SimpleNamespace(id="detached", display_name="detached", lifecycle_state="AVAILABLE"),
            SimpleNamespace(id="creating", display_name="creating", lifecycle_state="PROVISIONING"),
        ]
        self.cloud.get_admin = MagicMock(return_value=self.admin)
        self.args = SimpleNamespace(
            instance_tags=json.dumps(NODE_TAGS), region="us-ashburn-1", zone="ad-1",
            volume_id=None, search_pattern="yb-n1")

    def test_keeps_data_volumes_while_instance_exists(self):
        self.cloud.get_host_info = MagicMock(return_value={"id": INSTANCE_ID})

        self.cloud.delete_volumes(self.args)

        self.admin.delete_detached_boot_volumes.assert_called_once_with(
            "ad-1", NODE_TAGS, volume_ids=None)
        self.admin.list_volumes_by_tags.assert_not_called()
        self.admin.delete_volume.assert_not_called()

    def test_sweeps_detached_data_volumes_once_instance_is_gone(self):
        self.cloud.get_host_info = MagicMock(return_value=None)

        self.cloud.delete_volumes(self.args)

        self.admin.delete_detached_boot_volumes.assert_called_once_with(
            "ad-1", NODE_TAGS, volume_ids=None)
        self.admin.delete_volume.assert_called_once_with("detached")


class TestOciReplaceRootVolumeMethod(TestCase):
    def test_replaces_boot_volume_between_stop_and_start(self):
        method = object.__new__(OciReplaceRootVolumeMethod)
        method.cloud = MagicMock()
        method.cloud.name = "oci"
        method.get_server_ports_to_check = MagicMock(return_value=[22])
        host_info = {"id": INSTANCE_ID, "name": "yb-n1", "private_ip": "10.0.0.1"}
        args = SimpleNamespace(
            search_pattern="yb-n1", replacement_disk=IMAGE_ID, boot_script=None,
            capacity_reservation=None, force_replacement=True)

        method._replace_root_volume(
            args, *method._host_info_with_current_root_volume(args, host_info))

        self.assertEqual(
            [
                call.stop_instance(host_info),
                call.replace_boot_volume(host_info, IMAGE_ID, force=True),
                call.start_instance(host_info, [22], None),
            ],
            method.cloud.mock_calls)
