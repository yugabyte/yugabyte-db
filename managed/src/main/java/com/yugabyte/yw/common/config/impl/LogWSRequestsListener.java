/*
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.config.impl;

import com.yugabyte.yw.common.WSRequestLoggingFilter;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfigChangeListener;
import javax.inject.Inject;
import javax.inject.Singleton;

@Singleton
public class LogWSRequestsListener implements RuntimeConfigChangeListener {

  private final WSRequestLoggingFilter wsRequestLoggingFilter;
  private final RuntimeConfGetter confGetter;

  @Inject
  public LogWSRequestsListener(
      WSRequestLoggingFilter wsRequestLoggingFilter, RuntimeConfGetter confGetter) {
    this.wsRequestLoggingFilter = wsRequestLoggingFilter;
    this.confGetter = confGetter;
  }

  @Override
  public String getKeyPath() {
    return GlobalConfKeys.logWSRequests.getKey();
  }

  @Override
  public void processGlobal() {
    boolean enabled = Boolean.TRUE.equals(confGetter.getGlobalConf(GlobalConfKeys.logWSRequests));
    wsRequestLoggingFilter.setLogRequests(enabled);
  }
}
