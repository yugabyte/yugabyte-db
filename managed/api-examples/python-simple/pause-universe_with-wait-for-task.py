#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (c) YugaByte, Inc.

import http.client
import json
import time
import upgrade_base

# ==================================================================================================
# --- Constants
# ==================================================================================================

UNIVERSE_TASK_URL = f"/api/v1/customers/{upgrade_base.CUSTOMER_UUID}" \
                    f"/universes/{upgrade_base.UNIVERSE_UUID}/pause"

debug_mode = False  # <Set to True for verbose logs>

# ==================================================================================================
# --- Task Api request
# ==================================================================================================

universe_details_params = None  # <TO BE SET based on task. Example in comment below.>
# universe_details_params = upgrade_base.get_base_params()

response_json = upgrade_base.run_upgrade(UNIVERSE_TASK_URL, universe_details_params)

# ==================================================================================================
# --- Wait for Task Api request
# ==================================================================================================

if response_json is not None:
    taskUUID = response_json["taskUUID"]

WAIT_FOR_TASK_URL = f"/api/v1/customers/{upgrade_base.CUSTOMER_UUID}" \
                    f"/tasks/{taskUUID}"

waitTime = 4  # <SET to a higher value for longer running tasks>

conn = http.client.HTTPConnection(upgrade_base.PLATFORM_ADDRESS)
while True:
    conn.request(
        "GET",
        WAIT_FOR_TASK_URL,
        headers=upgrade_base.DEFAULT_HEADERS)
    statusResp = conn.getresponse()
    taskStatusJson = json.loads(statusResp.read())
    if taskStatusJson["status"] == "Running":
        print("\nTask is still pending- waiting for " + str(waitTime) +
              " seconds before polling again.\n")
        if debug_mode:
            print("\nCurrent task status is:\n", str(taskStatusJson))
        time.sleep(waitTime)
    else:
        print("\n\n====\nTask finished. Result: " + taskStatusJson["status"] + ".\n====\n")
        break

print("\nFinished polling long running task.")
