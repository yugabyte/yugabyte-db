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
from unittest.mock import MagicMock

import oci
from oci.core import ComputeClient

from ybops.cloud.oci.utils import OciCloudAdmin
from ybops.common.exceptions import YBOpsRuntimeError

IMAGE_ID = "ocid1.image.oc1..marketplace"


class TestCreateInstanceLaunchErrors(TestCase):
    def setUp(self):
        self.compute = MagicMock()
        self.admin = OciCloudAdmin()
        self.admin._config = {}
        self.admin._compartment_id = "ocid1.compartment.oc1..yb"
        self.admin._build_client = (
            lambda client_class: self.compute if client_class is ComputeClient else MagicMock())
        self.admin.resolve_availability_domain = lambda ad, compartment_id=None: ad

    def create(self):
        self.admin.create_instance(
            "us-sanjose-1", "AD-1", "ocid1.subnet.oc1..yb", "yb-n1", "VM.Standard.E4.Flex",
            "cluster-server", IMAGE_ID, 0, 100)

    def test_not_found_names_image_and_subscription(self):
        self.compute.launch_instance.side_effect = oci.exceptions.ServiceError(
            404, "NotAuthorizedOrNotFound", {},
            "Authorization failed or requested resource not found.")

        with self.assertRaises(YBOpsRuntimeError) as ctx:
            self.create()
        message = str(ctx.exception)
        self.assertIn(IMAGE_ID, message)
        self.assertIn("Authorization failed or requested resource not found.", message)
        self.assertIn("subscribe the compartment to that image version", message)

    def test_other_launch_errors_are_unchanged(self):
        error = oci.exceptions.ServiceError(400, "LimitExceeded", {}, "Out of host capacity.")
        self.compute.launch_instance.side_effect = error

        with self.assertRaises(oci.exceptions.ServiceError) as ctx:
            self.create()
        self.assertIs(error, ctx.exception)
