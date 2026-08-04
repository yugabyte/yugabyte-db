#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (c) YugabyteDB, Inc.

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
universe_details["clusters"][0]["userIntent"]["masterGFlags"] = {
        "ysql_yb_enable_ash": "true",
        "ysql_yb_ash_enable_infra": "true",
        "allowed_preview_flags_csv": "ysql_yb_enable_ash,ysql_yb_ash_enable_infra"
        }
universe_details["clusters"][0]["userIntent"]["tserverGFlags"] = {
        "ysql_yb_enable_ash": "true",
        "ysql_yb_ash_enable_infra": "true",
        "allowed_preview_flags_csv": "ysql_yb_enable_ash,ysql_yb_ash_enable_infra"
        }
upgrade_base.run_upgrade(GFLAGS_UPGRADE_URL, universe_details)
