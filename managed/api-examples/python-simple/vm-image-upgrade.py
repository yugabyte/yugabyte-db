#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (c) YugaByte, Inc.

import upgrade_base

# ==================================================================================================
# --- Constants
# ==================================================================================================

VM_IMAGE_UPGRADE_URL = f"/api/v1/customers/{upgrade_base.CUSTOMER_UUID}" \
                       f"/universes/{upgrade_base.UNIVERSE_UUID}/upgrade/vm"

# ==================================================================================================
# --- Api request
# ==================================================================================================

universe_details = upgrade_base.get_base_params()
universe_details["machineImages"] = {"e83d5748-82ee-4e06-af3d-b5dbbaa0e0bb": "ami-b63ae0ce"}
universe_details["forceVMImageUpgrade"] = "true"

upgrade_base.run_upgrade(VM_IMAGE_UPGRADE_URL, universe_details)
