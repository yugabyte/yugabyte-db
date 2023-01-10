#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (c) YugaByte, Inc.

import upgrade_base

# ==================================================================================================
# --- Constants
# ==================================================================================================

GFLAGS_UPGRADE_URL = f"/api/v1/customers/{upgrade_base.CUSTOMER_UUID}" \
                     f"/universes/{upgrade_base.UNIVERSE_UUID}/upgrade/gflags"

# ==================================================================================================
# --- Api request
# ==================================================================================================

universe_details = upgrade_base.get_base_params()
universe_details["masterGFlags"] = {"vmodule": "secure1*"}
universe_details["tserverGFlags"] = {"vmodule": "secure2*"}

upgrade_base.run_upgrade(GFLAGS_UPGRADE_URL, universe_details)
