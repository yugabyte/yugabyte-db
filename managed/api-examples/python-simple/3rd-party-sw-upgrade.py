#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (c) YugaByte, Inc.

import upgrade_base

# ==================================================================================================
# --- Constants
# ==================================================================================================

THIRD_PARTY_SW_UPGRADE_URL = f"/api/v1/customers/{upgrade_base.CUSTOMER_UUID}" \
                             f"/universes/{upgrade_base.UNIVERSE_UUID}/upgrade/thirdparty_software"

# ==================================================================================================
# --- Api request
# ==================================================================================================

universe_details = upgrade_base.get_base_params()

upgrade_base.run_upgrade(THIRD_PARTY_SW_UPGRADE_URL, universe_details)
