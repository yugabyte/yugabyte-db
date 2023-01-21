#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (c) YugaByte, Inc.

import upgrade_base

# ==================================================================================================
# --- Constants
# ==================================================================================================

POST_RESIZE_NODE_UPGRADE_URL = f"/api/v1/customers/{upgrade_base.CUSTOMER_UUID}" \
                               f"/universes/{upgrade_base.UNIVERSE_UUID}/upgrade/resize_node"


# ==================================================================================================
# --- Api request
# ==================================================================================================

# Instance type changes
# <TO BE SET>
instance_type = "c5.2xlarge"

# Volume size changes in GB, must be greater than current volume
# <TO BE SET>
volume_size = None

# Get Universe Details
universe_details = upgrade_base.get_base_params()


if volume_size:
    print("Instance volume size before: ",
          universe_details["clusters"][0]["userIntent"]["deviceInfo"]["volumeSize"])
    print("Modifying volume size to:", volume_size)
    universe_details["clusters"][0]["userIntent"]["deviceInfo"]["volumeSize"] = volume_size


if instance_type:
    print("Instance type before: ",
          universe_details["clusters"][0]["userIntent"]["instanceType"])
    print("Modifying instance type to:", instance_type)
    universe_details["clusters"][0]["userIntent"]["instanceType"] = instance_type

upgrade_base.run_upgrade(POST_RESIZE_NODE_UPGRADE_URL, universe_details)
