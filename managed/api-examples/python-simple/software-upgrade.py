#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (c) YugaByte, Inc.

import upgrade_base

# ==================================================================================================
# --- Constants
# ==================================================================================================

SW_UPGRADE_URL = f"/api/v1/customers/{upgrade_base.CUSTOMER_UUID}" \
                 f"/universes/{upgrade_base.UNIVERSE_UUID}/upgrade/software"

# ==================================================================================================
# --- Api request
# ==================================================================================================

universe_details = upgrade_base.get_base_params()
universe_details["ybSoftwareVersion"] = "2.17.1.0-b377"
universe_details["forceAll"] = "true"

upgrade_base.run_upgrade(SW_UPGRADE_URL, universe_details)
