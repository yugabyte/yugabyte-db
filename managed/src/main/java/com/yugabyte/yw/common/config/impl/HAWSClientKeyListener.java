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

import static com.yugabyte.yw.common.ha.PlatformInstanceClientFactory.YB_HA_WS_KEY;

import com.yugabyte.yw.common.config.RuntimeConfigChangeListener;
import com.yugabyte.yw.common.ha.PlatformInstanceClientFactory;
import javax.inject.Inject;
import javax.inject.Singleton;

@Singleton
public class HAWSClientKeyListener implements RuntimeConfigChangeListener {
  private final PlatformInstanceClientFactory platformInstanceClientFactory;

  @Inject
  public HAWSClientKeyListener(PlatformInstanceClientFactory platformInstanceClientFactory) {
    this.platformInstanceClientFactory = platformInstanceClientFactory;
  }

  public String getKeyPath() {
    return YB_HA_WS_KEY;
  }

  public void processGlobal() {
    platformInstanceClientFactory.refreshWsClient(getKeyPath());
  }
}
