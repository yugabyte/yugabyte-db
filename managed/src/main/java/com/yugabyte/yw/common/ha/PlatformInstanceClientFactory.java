/*
 * Copyright 2022 YugaByte, Inc. and Contributors
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
import com.google.inject.name.Named;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.WSClientRefresher;
import lombok.extern.slf4j.Slf4j;

@Singleton
@Slf4j
public class PlatformInstanceClientFactory {

  private final WSClientRefresher wsClientRefresher;

  @Inject
  public PlatformInstanceClientFactory(@Named(YB_HA_WS_KEY) WSClientRefresher wsClientRefresher) {
    this.wsClientRefresher = wsClientRefresher;
  }

  public PlatformInstanceClient getClient(String clusterKey, String remoteAddress) {
    return new PlatformInstanceClient(
        new ApiHelper(wsClientRefresher.getClient(YB_HA_WS_KEY)), clusterKey, remoteAddress);
  }
}
