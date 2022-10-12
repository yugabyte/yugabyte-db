/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.config.impl;

import static com.yugabyte.yw.common.ha.PlatformInstanceClient.YB_HA_WS_KEY;

import com.google.inject.name.Named;
import com.yugabyte.yw.common.WSClientRefresher;
import com.yugabyte.yw.common.config.RuntimeConfigChangeListener;
import javax.inject.Inject;
import javax.inject.Singleton;

@Singleton
public class HAWSClientKeyListener implements RuntimeConfigChangeListener {
  private final WSClientRefresher wsClientRefresher;

  @Inject
  public HAWSClientKeyListener(@Named(YB_HA_WS_KEY) WSClientRefresher wsClientRefresher) {
    this.wsClientRefresher = wsClientRefresher;
  }

  public String getKeyPath() {
    return YB_HA_WS_KEY;
  }

  public void processGlobal() {
    wsClientRefresher.refreshWsClient(getKeyPath());
  }
}
