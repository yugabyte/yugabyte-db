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

import com.yugabyte.yw.common.WSClientRefresher;
import com.yugabyte.yw.common.config.RuntimeConfigChangeListener;
import com.yugabyte.yw.common.inject.StaticInjectorHolder;

public class WSClientKeyListener implements RuntimeConfigChangeListener {

  private final String clientKey;

  public WSClientKeyListener(String clientKey) {
    this.clientKey = clientKey;
  }

  public String getKeyPath() {
    return clientKey;
  }

  public void processGlobal() {
    WSClientRefresher wsClientRefresher =
        StaticInjectorHolder.injector().instanceOf(WSClientRefresher.class);
    wsClientRefresher.refreshWsClient(getKeyPath());
  }
}
