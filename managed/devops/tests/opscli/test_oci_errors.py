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

from oci._vendor.requests.exceptions import ConnectionError
from oci._vendor.requests.exceptions import ConnectTimeout as RequestsConnectTimeout
from oci.exceptions import ConnectTimeout, RequestException, ServiceError

from ybops.cloud.oci.cloud import OciCloud
from ybops.cloud.oci.errors import format_oci_error
from ybops.cloud.oci.method import OciChangeInstanceTypeMethod
from ybops.common.exceptions import YBOpsRuntimeError

REQUEST_ID = "test-opc-request-id"
ENDPOINT = "GET https://iaas.us-ashburn-1.oraclecloud.com/20160918/instances/ocid1.instance.oc1..x"
ENDPOINT_ARG = ("Request Endpoint: " + ENDPOINT + " See https://docs.oracle.com/en-us/iaas/"
                "Content/API/Concepts/sdk_troubleshooting.htm for help troubleshooting this "
                "error, or contact support and provide this full error message.")


def service_error(status, code, message, **body):
    # Mirrors BaseClient.raise_service_error: extra response body keys arrive as deserialized_data.
    data = dict(body, code=code)
    if message is not None:
        data["message"] = message
    return ServiceError(status, code, {"opc-request-id": REQUEST_ID}, message,
                        operation_name="get_instance", target_service="compute",
                        request_endpoint=ENDPOINT, client_version="Oracle-PythonSDK/2.184.2",
                        timestamp="2026-10-20T10:00:00Z", deserialized_data=data,
                        api_reference_link="https://docs.oracle.com/iaas/api/#/en/iaas/x")


class TestFormatOciError(TestCase):

    def assertFormatted(self, expected, e):
        message = format_oci_error(e)
        self.assertEqual(expected, message)
        for boilerplate in ("Oracle-PythonSDK", "2026-10-20", "docs.oracle.com", "contact"):
            self.assertNotIn(boilerplate, message)

    def test_service_error_keeps_message_and_code(self):
        self.assertFormatted(
            "OCI compute get_instance failed: Invalid shape. (400 InvalidParameter)",
            service_error(400, "InvalidParameter", "Invalid shape."))

    def test_detail_used_when_message_missing(self):
        self.assertFormatted(
            "OCI compute get_instance failed: Shape is not available (400 InvalidParameter)",
            service_error(400, "InvalidParameter", None, detail="Shape is not available"))

    def test_detail_appended_when_different_from_message(self):
        self.assertFormatted(
            "OCI compute get_instance failed: Bad request. Detail: Too long "
            "(400 InvalidParameter)",
            service_error(400, "InvalidParameter", "Bad request.", detail="Too long"))

    def test_original_message_used_when_message_missing(self):
        self.assertFormatted(
            "OCI compute get_instance failed: Out of capacity. (500 InternalError). "
            "OCI request ID: " + REQUEST_ID,
            service_error(500, "InternalError", None, originalMessage="Out of capacity."))

    def test_missing_message_still_shows_status_and_code(self):
        self.assertFormatted(
            "OCI compute get_instance failed: no error message in the response "
            "(409 Conflict)",
            service_error(409, "Conflict", None))

    def test_not_found_includes_endpoint_host(self):
        self.assertFormatted(
            "OCI compute get_instance failed: Authorization failed or requested resource not "
            "found. (404 NotAuthorizedOrNotFound, endpoint: iaas.us-ashburn-1.oraclecloud.com)",
            service_error(404, "NotAuthorizedOrNotFound",
                          "Authorization failed or requested resource not found."))

    def test_connection_error_keeps_requests_message(self):
        cause = ("OCIConnectionPool(host='iaas.nowhere-1.invalid', port=443): Max retries "
                 "exceeded with url: /20160918/instances/x (Caused by NameResolutionError("
                 "\"Failed to resolve 'iaas.nowhere-1.invalid'\"))")
        self.assertFormatted("OCI request failed: " + cause,
                             RequestException(ConnectionError(cause, ENDPOINT_ARG)))

    def test_connect_timeout_without_message_shows_endpoint_host(self):
        self.assertFormatted(
            "OCI request failed: no response from iaas.us-ashburn-1.oraclecloud.com",
            ConnectTimeout(RequestsConnectTimeout("", ENDPOINT_ARG)))

    def test_other_exceptions_are_not_formatted(self):
        self.assertIsNone(format_oci_error(ValueError("boom")))

    def test_unexpected_error_shape_falls_back_to_original(self):
        e = service_error(404, "NotAuthorizedOrNotFound", "Not found.")
        e.request_endpoint = "GET https://[bad/path"
        with self.assertLogs(level="ERROR"):
            self.assertIsNone(format_oci_error(e))


class TestOciChangeInstanceTypeError(TestCase):

    def test_sdk_error_formatted_by_shared_resize_path(self):
        base_command = MagicMock()
        base_command.cloud = OciCloud()
        base_command.cloud.stop_instance = MagicMock(
            side_effect=service_error(400, "InvalidParameter", "Invalid shape."))
        method = OciChangeInstanceTypeMethod(base_command)
        method._start_instance = MagicMock()
        args = SimpleNamespace(instance_type="VM.Standard.E5.Flex", search_pattern="n1",
                               force=False, boot_script=None)

        with self.assertRaises(YBOpsRuntimeError) as ctx:
            method._resize_instance(args, {"instance_type": "VM.Standard.E4.Flex"})
        self.assertEqual(
            "Runtime error: error executing \"instance.modify_attribute\": OCI compute "
            "get_instance failed: Invalid shape. (400 InvalidParameter)",
            str(ctx.exception))
