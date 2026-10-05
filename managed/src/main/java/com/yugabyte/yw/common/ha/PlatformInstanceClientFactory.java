/*
 * Copyright 2022 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.ha;

import static com.yugabyte.yw.common.ha.PlatformInstanceClient.YB_HA_WS_KEY;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.typesafe.config.ConfigValue;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.WSClientRefresher;
import com.yugabyte.yw.models.helpers.CommonUtils;
import java.util.Map;
import lombok.extern.slf4j.Slf4j;

@Singleton
@Slf4j
public class PlatformInstanceClientFactory {

  private final WSClientRefresher wsClientRefresher;

  private final ConfigHelper configHelper;

  private final boolean fipsEnabled;

  @Inject
  public PlatformInstanceClientFactory(
      WSClientRefresher wsClientRefresher, ConfigHelper configHelper, Config config) {
    this.wsClientRefresher = wsClientRefresher;
    this.configHelper = configHelper;
    this.fipsEnabled = config.getBoolean(CommonUtils.FIPS_ENABLED);
  }

  public PlatformInstanceClient getClient(
      String clusterKey, String remoteAddress, Map<String, ConfigValue> wsOverrides) {
    return new PlatformInstanceClient(
        new ApiHelper(
            wsClientRefresher.getClient(YB_HA_WS_KEY, wsOverrides),
            wsClientRefresher.getMaterializer()),
        clusterKey,
        remoteAddress,
        configHelper,
        fipsEnabled);
  }
}
