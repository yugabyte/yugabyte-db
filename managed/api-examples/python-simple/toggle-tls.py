#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (c) YugaByte, Inc.

import upgrade_base

# ==================================================================================================
# --- Constants
# ==================================================================================================

TLS_UPGRADE_URL = f"/api/v1/customers/{upgrade_base.CUSTOMER_UUID}" \
                  f"/universes/{upgrade_base.UNIVERSE_UUID}/upgrade/tls"

# ==================================================================================================
# --- Api request
# ==================================================================================================

universe_details = upgrade_base.get_base_params()
universe_details["enableNodeToNodeEncrypt"] = "false"
universe_details["enableClientToNodeEncrypt"] = "false"
universe_details["rootCA"] = None
universe_details["clientRootCA"] = None
universe_details["rootAndClientRootCASame"] = "true"

upgrade_base.run_upgrade(TLS_UPGRADE_URL, universe_details)
