#!/bin/env python3
#
# Copyright 2023 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
import argparse
import io
import logging
import mimetypes
import re
import shutil
import urllib.request as urllib_request
import uuid

# Constants
ATTACH_ACTION = "attach_universe"
DETACH_ACTION = "detach_universe"
VALID_ACTIONS = [ATTACH_ACTION, DETACH_ACTION]
UUID4_REGEX = re.compile("[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}")


class YBAttachDetach:
    """
    Allows user to detach universe from a specific platform and attach the universe to
    a separate platform as needed.

    Sample detach example:
        python3 ./yb_attach_detach.py detach_universe bb4991fd-2cb9-4052-9821-639351454e73 -f
        /Users/cwang/Downloads/output.tar.gz -p http://localhost:9000 -c
        f33e3c9b-75ab-4c30-80ad-cba85646ea39 -t ce5dd0f1-3e3f-4334-aa64-28f1c87cb3f2

    Sample attach example:
        python3 ./yb_attach_detach.py attach_universe bb4991fd-2cb9-4052-9821-639351454e73 -f
        /Users/cwang/Downloads/output.tar.gz -p http://localhost:9000 -c
        f33e3c9b-75ab-4c30-80ad-cba85646ea39 -t ce5dd0f1-3e3f-4334-aa64-28f1c87cb3f2
    """
    def __init__(self, action, universe_uuid, file, customer_uuid, api_token, platform_host):
        self.action = action
        self.universe_uuid = universe_uuid
        self.file = file
        self.customer_uuid = customer_uuid
        self.api_token = api_token
        self.platform_host = platform_host
        self.set_url_request_variables()

    def run(self):
        """Performs the required action and throw error if failed."""
        self.validate_arguments()
        logging.info("Running %s action", self.action)
        if self.action == ATTACH_ACTION:
            logging.info("Attaching universe...")
            file_name = self.file.split("/")[-1]
            form = MultiPartForm()
            form.add_file(
                "spec", file_name, file_handle=open(self.file, "rb"), mimetype="application/gzip")
            data = bytes(form)
            req = urllib_request.Request(
                self.attach_url, method="POST", headers=self.default_headers, data=data)
            req.add_header('Content-type', form.get_content_type())
            urllib_request.urlopen(req)

            logging.info("Completed attaching universe.")
        elif self.action == DETACH_ACTION:
            logging.info("Detaching universe...")
            req = urllib_request.Request(
                url=self.detach_url, method="POST", headers=self.default_headers)
            with urllib_request.urlopen(req) as response, open(self.file, "wb") as file:
                shutil.copyfileobj(response, file)
            logging.info("Completed detaching universe.")

    def validate_arguments(self):
        """Simple validation of the arguments passed in."""
        if not is_valid_uuid(self.universe_uuid):
            raise ValueError("Invalid universe uuid passed in.")

        if not is_valid_uuid(self.customer_uuid):
            raise ValueError("Invalid customer uuid passed in.")

        if not is_valid_uuid(self.api_token):
            raise ValueError("Invalid api token passed in.")

        if self.action not in VALID_ACTIONS:
            raise ValueError(
                f"Invalid action passed in. Got {self.action}. "
                f"Expected one of: {VALID_ACTIONS}")

    def set_url_request_variables(self):
        """
        Use arguments passed in to generate required urls/headers
        needed to perform requests later on.
        """
        self.base_url = f"{self.platform_host}/api/v1"
        self.attach_url = (f"{self.base_url}/customers/"
                           f"{self.customer_uuid}/universes/{self.universe_uuid}/import")
        self.detach_url = (f"{self.base_url}/customers/"
                           f"{self.customer_uuid}/universes/{self.universe_uuid}/export")
        self.default_headers = {"X-AUTH-YW-API-TOKEN": self.api_token}
        logging.debug("Base url: %s", self.base_url)
        logging.debug("Detach url: %s", self.detach_url)
        logging.debug("Attach url: %s", self.attach_url)
        logging.debug("Default headers: %s", self.default_headers)


def is_valid_uuid(uuid_to_test):
    """Regular rexpression to check if UUID is valid"""
    match = UUID4_REGEX.match(uuid_to_test)
    return bool(match)


def parse_arguments():
    """
    Parses arguments passed in and prints to console output.
    """
    parser = argparse.ArgumentParser(
        prog="Attach/detach universe script",
        description="Command line script for attaching and detaching universe"
    )

    parser.add_argument(
        "action",
        help=f"Accepts one of the following: {VALID_ACTIONS}")
    parser.add_argument(
        "univ_uuid",
        help="Universe uuid to be passed to attach/detach")
    parser.add_argument(
        "-f", "--file", required=True,
        help="For detach, file location to save tar gz file to. "
        "For attach, file location of required tar gz used")
    parser.add_argument(
        "-c", "--customer", required=True,
        help="Customer uuid for the universe")
    parser.add_argument(
        "-t", "--api_token", required=True,
        help="Api token required to connect to YBA platform")
    parser.add_argument(
        "-p", "--platform_host", required=True,
        help="Base endpoint platform requests are sent to")
    args = parser.parse_args()

    logging.info("\n")
    logging.info("-----------------Arguments----------------------------")
    logging.info("Action: %s", args.action)
    logging.info("Universe uuid: %s", args.univ_uuid)
    logging.info("File path: %s", args.file)
    logging.info("Customer: %s", args.customer)
    logging.info("Api token: %s", args.api_token)
    logging.info("Platform host: %s", args.platform_host)
    logging.info("------------------------------------------------------")
    logging.info("\n")

    return args


class MultiPartForm:
    """
    Accumulate the data to be used when posting a form.
    Source: https://pymotw.com/3/urllib.request/#uploading-files
    """

    def __init__(self):
        self.form_fields = []
        self.files = []
        # Use a large random byte string to separate
        # parts of the MIME data.
        self.boundary = uuid.uuid4().hex.encode("utf-8")

    def get_content_type(self):
        """Returns multipart form data and specifying a specific boundary."""
        return f"multipart/form-data; boundary={self.boundary.decode('utf-8')}"

    def add_field(self, name, value):
        """Add a simple field to the form data."""
        self.form_fields.append((name, value))

    def add_file(self, field_name, file_name, file_handle,
                 mimetype=None):
        """Add a file to be uploaded."""
        body = file_handle.read()
        if mimetype is None:
            mimetype = (
                mimetypes.guess_type(file_name)[0] or
                "application/octet-stream"
            )
        self.files.append((field_name, file_name, mimetype, body))

    @staticmethod
    def _form_data(name):
        return (f"Content-Disposition: form-data; name=\"{name}\"\r\n").encode("utf-8")

    @staticmethod
    def _attached_file(name, file_name):
        return ("Content-Disposition: file; "
                f"name=\"{name}\"; filename=\"{file_name}\"\r\n").encode("utf-8")

    @staticmethod
    def _content_type(content_type):
        return f"Content-Type: {content_type}\r\n".encode("utf-8")

    def __bytes__(self):
        """Return a byte-string representing the form data,
        including attached files.
        """
        buffer = io.BytesIO()
        boundary = b"--" + self.boundary + b"\r\n"

        # Add the form fields
        for name, value in self.form_fields:
            buffer.write(boundary)
            buffer.write(self._form_data(name))
            buffer.write(b"\r\n")
            buffer.write(value.encode("utf-8"))
            buffer.write(b"\r\n")

        # Add the files to upload
        for f_name, file_name, f_content_type, body in self.files:
            buffer.write(boundary)
            buffer.write(self._attached_file(f_name, file_name))
            buffer.write(self._content_type(f_content_type))
            buffer.write(b"\r\n")
            buffer.write(body)
            buffer.write(b"\r\n")

        buffer.write(b"--" + self.boundary + b"--\r\n")
        return buffer.getvalue()


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s: %(message)s")
    cmd_args = parse_arguments()
    yb_attach_detach = YBAttachDetach(
        cmd_args.action, cmd_args.univ_uuid, cmd_args.file,
        cmd_args.customer, cmd_args.api_token, cmd_args.platform_host)
    yb_attach_detach.run()
