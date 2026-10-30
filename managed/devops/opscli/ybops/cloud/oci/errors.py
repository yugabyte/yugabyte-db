#!/usr/bin/env python
#
# Copyright 2026 YugabyteDB, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

"""User-facing messages for OCI SDK errors, in the format of Java's OCIErrorFormatter.

str() of an OCI SDK exception includes SDK boilerplate (timestamp, client version, logging and
troubleshooting links). The formatted message keeps what the service returned (message, detail,
status, error code) and drops the boilerplate. Callers must leave the original exception in the
traceback or log so the full error is still recorded.
"""

import logging
from urllib.parse import urlsplit

from oci.exceptions import ConnectTimeout, RequestException, ServiceError

_ENDPOINT_ARG_PREFIX = "Request Endpoint: "


def format_oci_error(e):
    """Returns the user-facing message for an OCI SDK exception, None for any other exception."""
    try:
        if isinstance(e, ServiceError):
            return _format_service_error(e)
        if isinstance(e, (RequestException, ConnectTimeout)):
            return _format_request_exception(e)
    except Exception:
        # Callers are error handlers, including sys.excepthook. Returning None makes them show the
        # original error instead of replacing it with this one.
        logging.exception("Failed to format OCI error")
    return None


def _format_service_error(e):
    # ServiceError converts the response body's keys to snake_case, so OCI's originalMessage is
    # original_message here.
    body = e.args[0] if e.args and isinstance(e.args[0], dict) else {}
    # Some services return "detail" instead of, or in addition to, "message".
    detail = _non_blank(body.get("detail"))
    text = next((t for t in (_non_blank(e.message), _non_blank(body.get("original_message")),
                             detail) if t), None)
    target = " ".join(name for name in (e.target_service, e.operation_name) if name)
    msg = "OCI {} failed: ".format(target or "request")
    if text is None:
        msg += "no error message in the response"
    else:
        msg += text
        if detail and detail not in text:
            msg += "{}Detail: {}".format(" " if text.endswith(".") else ". ", detail)

    msg += " ({}".format(e.status)
    if e.code:
        msg += " {}".format(e.code)
    # OCI answers both "does not exist" and "not authorized" with 404; the region in the endpoint
    # host is often the explanation.
    host = _endpoint_host(e.request_endpoint) if e.status == 404 else None
    if host:
        msg += ", endpoint: {}".format(host)
    msg += ")"
    if (e.status < 400 or e.status >= 500) and e.request_id:
        msg += ". OCI request ID: {}".format(e.request_id)
    return msg


def _format_request_exception(e):
    # The SDK wraps the requests exception and appends an argument with the endpoint and a
    # troubleshooting link. The requests message already names the host and the cause.
    inner = e.args[0] if e.args and isinstance(e.args[0], BaseException) else e
    args = [str(a) for a in inner.args]
    text = "; ".join(a for a in args if a.strip() and not a.startswith(_ENDPOINT_ARG_PREFIX))
    if text:
        return "OCI request failed: {}".format(text)
    endpoint = next((a for a in args if a.startswith(_ENDPOINT_ARG_PREFIX)), None)
    host = _endpoint_host(endpoint)
    return "OCI request failed: no response from {}".format(host or "OCI")


def _non_blank(value):
    return str(value) if value is not None and str(value).strip() else None


def _endpoint_host(endpoint):
    # Endpoints look like "GET https://host/path?query".
    url = next((token for token in (endpoint or "").split() if "://" in token), None)
    return urlsplit(url).hostname if url else None
