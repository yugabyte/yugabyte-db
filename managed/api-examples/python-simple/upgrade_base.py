#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (c) YugaByte, Inc.

import json
import http.client
from pprint import pprint

PLATFORM_ADDRESS = "localhost:9000"

# To identify the customer UUID for this API call, see the "Get Session Info" section at
# https://github.com/yugabyte/yugabyte-db/blob/master/managed/api-examples/python-simple/
#   create-universe.ipynb
CUSTOMER_UUID = "f33e3c9b-75ab-4c30-80ad-cba85646ea39"

# To identify the universe uuid for a given universe, use the the example at
# https://github.com/yugabyte/yugabyte-db/blob/master/managed/api-examples/python-simple/
#   list-universes.ipynb
# to list all universes and filter by name
UNIVERSE_UUID = "4d419e8d-51d0-4c1c-9446-40849d3cec9c"

# Platform api key to be set
X_AUTH_YW_API_TOKEN = "5e8d9e2e-894d-405d-8731-fdc2111e8d57"

DEFAULT_HEADERS = {"Content-Type": "application/json", "X-AUTH-YW-API-TOKEN": X_AUTH_YW_API_TOKEN}

GET_UNIVERSE_URL = f"/api/v1/customers/{CUSTOMER_UUID}/universes/{UNIVERSE_UUID}"


def get_base_params():
    conn = http.client.HTTPConnection(PLATFORM_ADDRESS)
    conn.request(
        "GET",
        GET_UNIVERSE_URL,
        headers=DEFAULT_HEADERS
    )
    response_json = json.load(conn.getresponse())
    base_params = response_json["universeDetails"]
    pprint(base_params)
    print("-----\n\n\n")

    base_params["sleepAfterMasterRestartMillis"] = 0   # Default is 18000 ms, <TO BE SET>
    base_params["sleepAfterTServerRestartMillis"] = 0
    return base_params


def run_upgrade(url, params):
    pprint(params)
    print("-----\n\n\n")

    conn = http.client.HTTPConnection(PLATFORM_ADDRESS)
    conn.request(
        "POST",
        url,
        body=json.dumps(params),
        headers=DEFAULT_HEADERS
    )
    response_json = json.load(conn.getresponse())
    print(response_json)
    return response_json
