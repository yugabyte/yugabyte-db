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
import json
import logging
import mimetypes
import os.path
import re
import shutil
import urllib.request as urllib_request
import urllib.error
import uuid

# Constants
RUN_ACTION = "run"
ATTACH_ACTION = "attach"
DETACH_ACTION = "detach"
DELETE = "delete"
UUID4_REGEX = re.compile("[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}")


def is_valid_uuid(uuid_to_test):
    """Regular rexpression to check if UUID is valid."""
    match = UUID4_REGEX.match(uuid_to_test)
    return bool(match)


def parse_arguments():
    """
    Parses arguments passed in and prints to console output.
    """
    parser = argparse.ArgumentParser(
        prog="Attach/detach universe script",
        description="Command line script for attaching and detaching universe."
    )

    parser.add_argument(
        "-u",
        "--univ_uuid", required=True,
        help="Universe's uuid for universe to be attached/detached/deleted.")

    parser.add_argument(
        "-v", "--verbose", action="store_true",
        required=False, default=False, help="Enable verbose logging.")

    file_parent_parser = argparse.ArgumentParser(add_help=False)
    file_parent_parser.add_argument(
        "-f", "--file", required=True,
        help="For detach, file location to save tar gz file to. "
        "For attach, file location of required tar gz used.")

    detach_parent_parser = argparse.ArgumentParser(add_help=False)
    detach_parent_parser.add_argument(
        "-ts", "--api_token_src", required=True,
        help="Api token required to connect to the source YBA platform.")
    detach_parent_parser.add_argument(
        "-ps", "--platform_host_src", required=True,
        help="Base endpoint for source platform requests are sent to.")

    attach_parent_parser = argparse.ArgumentParser(add_help=False)
    attach_parent_parser.add_argument(
        "-td", "--api_token_dest", required=True,
        help="Api token required to connect to the destination YBA platform.")
    attach_parent_parser.add_argument(
        "-pd", "--platform_host_dest", required=True,
        help="Base endpoint for destination platform requests are sent to.")

    releases_parent_parser = argparse.ArgumentParser(add_help=False)
    releases_parent_parser.add_argument(
        "-s", "--skip_releases", action="store_true",
        required=False, default=False,
        help="Whether or not do include software and ybc releases.")

    subparsers = parser.add_subparsers(help="sub-command --help", dest="name")
    run_parser = subparsers.add_parser(
        RUN_ACTION,
        parents=[detach_parent_parser, attach_parent_parser,
                 file_parent_parser, releases_parent_parser],
        help="Perform detach/attach/deletion in one-stop.")
    detach_parser = subparsers.add_parser(
        DETACH_ACTION, parents=[detach_parent_parser, file_parent_parser, releases_parent_parser],
        help="Generate tar gz containing all universe metadata.")
    attach_parser = subparsers.add_parser(
        ATTACH_ACTION, parents=[attach_parent_parser, file_parent_parser],
        help="Populate target platform with universe metadata using tar gz.")
    delete_parser = subparsers.add_parser(
        DELETE, parents=[detach_parent_parser],
        help="Perform a delete of the universe on the source platform.")

    args = vars(parser.parse_args())

    logging.info("\n")
    logging.info("-----------------Arguments----------------------------")
    for key in args:
        logging.info("%s: %s", key, redact_sensitive_info(key, args[key]))
    logging.info("------------------------------------------------------")
    logging.info("\n")

    return args


def redact_sensitive_info(key, val):
    if "api_token" in key and len(val) >= 5:
        return "".join((val[0:2], len(val[2:-2]) * "*", val[-2:]))
    return val


class YBAttachDetach:
    """
    Allows user to detach universe from a specific platform and attach the universe to
    a separate platform as needed.

    Sample detach example:
        python3 ./yb_attach_detach.py -u edf981dc-a11a-46d1-9518-c022b1f0bc80 detach
        -ts ad09f10f-c377-4cdf-985c-898d13eae783 -ps http://167.123.191.88:9000
        -f /tmp/universe-export-spec.tar.gz -s

    Sample attach example:
        python3 ./yb_attach_detach.py -u edf981dc-a11a-46d1-9518-c022b1f0bc80 attach
        -td 4abb151c-3020-43fb-bd04-f6a63934e4e5 -pd http://10.150.7.155:9000
        -f /tmp/universe-export-spec.tar.gz

    Sample one stop detach/attach/delete example:
        python3 ./yb_attach_detach.py -u edf981dc-a11a-46d1-9518-c022b1f0bc80 run
        -ts ad09f10f-c377-4cdf-985c-898d13eae783 -ps http://167.123.191.88:9000
        -f /tmp/universe-export-spec.tar.gz -s -td 4abb151c-3020-43fb-bd04-f6a63934e4e5
        -pd http://10.150.7.155:9000


    Can enable verbose logging for debugging purposes by passing -v before the action.
    """

    def __init__(self, args):
        self.action_map = {
            DETACH_ACTION: self.run_detach,
            ATTACH_ACTION: self.run_attach,
            DELETE: self.run_delete_metadata
        }
        self.name = args.get("name")
        if self.name == RUN_ACTION:
            self.actions = [DETACH_ACTION, ATTACH_ACTION, DELETE]
        else:
            self.actions = [args.get("name")]
        self.universe_uuid = args.get("univ_uuid")
        self.file = args.get("file")

        self.api_token_src = args.get("api_token_src")
        self.platform_host_src = args.get("platform_host_src")

        self.api_token_dest = args.get("api_token_dest")
        self.platform_host_dest = args.get("platform_host_dest")

        self.skip_releases = args.get("skip_releases")

    def run_detach(self):
        """Generate tar gz file for a specific universe from the source platform"""
        logging.info("Detaching universe...")
        data = {
            "skipReleases": self.skip_releases
        }
        url = self._get_detach_url()
        headers = {
            "X-AUTH-YW-API-TOKEN": self.api_token_src,
            "Content-Type": "application/json"
        }
        logging.debug("Url: %s", url)
        logging.debug("Request body: %s", data)
        req = urllib_request.Request(
            url=url, method="POST", headers=headers,
            data=json.dumps(data).encode())
        with open_url(req) as response, open(self.file, "wb") as file:
            shutil.copyfileobj(response, file)
        logging.info("Completed detaching universe.")

    def run_attach(self):
        """Attach universe using generated tar gz destination platform"""
        logging.info("Attaching universe...")
        file_name = self.file.split("/")[-1]
        form = MultiPartForm()
        form.add_file(
            "spec", file_name, file_handle=open(self.file, "rb"), mimetype="application/gzip")
        url = self._get_attach_url()
        headers = {
            "X-AUTH-YW-API-TOKEN": self.api_token_dest,
            "Content-Type": "application/json"
        }
        data = bytes(form)
        logging.debug("Url: %s", url)
        logging.debug("Request body: %s", form)
        req = urllib_request.Request(
            url=url, method="POST", headers=headers, data=data)
        req.add_header('Content-type', form.get_content_type())

        try:
            open_url(req)
        except (urllib.error.HTTPError, urllib.error.URLError) as err:
            logging.info("Failed attaching universe.")
            if self.name == RUN_ACTION:
                logging.info("Unlocking universe from source platform.")
                unlock_url = self._get_unlock_universe_url()
                unlock_headers = {
                    "X-AUTH-YW-API-TOKEN": self.api_token_src,
                    "Content-Type": "application/json"
                }
                unlock_req = urllib_request.Request(
                    url=unlock_url, method="POST", headers=unlock_headers)
                logging.debug("Url: %s", unlock_url)
                open_url(unlock_req)
                logging.info("Finished unlocking universe from source platform.")
            raise err

        logging.info("Completed attaching universe.")

    def run_delete_metadata(self):
        """Delete universe metadata from source platform"""
        logging.info("Removing universe metadata from source platform...")
        url = self._get_delete_metadata_url()
        headers = {
            "X-AUTH-YW-API-TOKEN": self.api_token_src,
            "Content-Type": "application/json"
        }
        logging.debug("url: %s", url)
        req = urllib_request.Request(url=url, method="POST", headers=headers)
        open_url(req)
        logging.info("Completed removing universe metadata from source platform.")

    def run(self):
        """Performs the required action(s) and throw error if failed."""
        self.validate_arguments()
        for action in self.actions:
            logging.info("Running %s action", action)
            self.action_map[action]()

    def _get_detach_url(self):
        customer_uuid = self._get_customer_uuid(self.api_token_src, self.platform_host_src)
        base_url = f"{self.platform_host_src}/api/v1"
        detach_url = (
            f"{base_url}/customers/"
            f"{customer_uuid}/universes/{self.universe_uuid}/export")
        return detach_url

    def _get_attach_url(self):
        customer_uuid = self._get_customer_uuid(self.api_token_dest, self.platform_host_dest)
        base_url = f"{self.platform_host_dest}/api/v1"
        attach_url = (
            f"{base_url}/customers/"
            f"{customer_uuid}/universes/{self.universe_uuid}/import")
        return attach_url

    def _get_delete_metadata_url(self):
        customer_uuid = self._get_customer_uuid(self.api_token_src, self.platform_host_src)
        base_url = f"{self.platform_host_src}/api/v1"
        delete_url = (
            f"{base_url}/customers/"
            f"{customer_uuid}/universes/{self.universe_uuid}/delete_metadata")
        return delete_url

    def _get_unlock_universe_url(self):
        customer_uuid = self._get_customer_uuid(self.api_token_src, self.platform_host_src)
        base_url = f"{self.platform_host_src}/api/v1"
        unlock_universe_url = (
            f"{base_url}/customers/"
            f"{customer_uuid}/universes/{self.universe_uuid}/unlock")
        return unlock_universe_url

    def validate_arguments(self):
        """Simple validation of the arguments passed in."""
        if not is_valid_uuid(self.universe_uuid):
            raise ValueError("Invalid universe uuid passed in.")

        if self.name == DETACH_ACTION or self.name == RUN_ACTION or self.name == DELETE:
            if not is_valid_uuid(self.api_token_src):
                raise ValueError("Invalid api_token_src passed in.")

            # Ping source platform to make sure we are able to connect before running commands
            self._get_customer_uuid(self.api_token_src, self.platform_host_src)

        if self.name == ATTACH_ACTION or self.name == RUN_ACTION:
            if not is_valid_uuid(self.api_token_dest):
                raise ValueError("Invalid api_token_dest passed in.")

            # Ping dest platform to make sure we are able to connect before running commands
            self._get_customer_uuid(self.api_token_dest, self.platform_host_dest)

        if (self.name == DETACH_ACTION or self.name == RUN_ACTION) and os.path.isfile(self.file):
            raise ValueError(f"File {self.file} already exists")

    def _get_customer_uuid(self, api_token, platform_host):
        headers = {
            "X-AUTH-YW-API-TOKEN": api_token,
            "Content-Type": "application/json"
        }
        url = self._get_session_info_url(platform_host)
        logging.debug("Url: %s", url)
        req = urllib_request.Request(
            url=url, method="GET", headers=headers)
        with open_url(req) as response:
            data = response.read()
            session_info = json.loads(data.decode('utf-8'))
            logging.debug("Response: %s", session_info)
            return session_info["customerUUID"]

    def _get_session_info_url(self, platform_host):
        base_url = f"{platform_host}/api/v1"
        session_info_url = f"{base_url}/session_info"
        return session_info_url


def open_url(request):
    """Make request to desired url and print verbose logs if needed"""
    try:
        resp = urllib_request.urlopen(request)
        logging.debug("Success Status %s", resp.code)
        return resp
    except urllib.error.HTTPError as err:
        logging.debug("Error Status %s", err.code)
        logging.debug("Error Reason %s", err.reason)
        logging.debug("Error Response %s", err.read().decode())
        raise err
    except urllib.error.URLError as err:
        logging.debug("Error Reason %s", err.reason)
        raise err


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
    if cmd_args.get("verbose"):
        logging.getLogger().setLevel(logging.DEBUG)

    yb_attach_detach = YBAttachDetach(cmd_args)
    yb_attach_detach.run()
