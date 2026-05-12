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

import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfigChangeListener;
import com.yugabyte.yw.filters.AccessLogFilter;
import javax.inject.Inject;
import javax.inject.Singleton;

@Singleton
public class AccessLogExcludeListener implements RuntimeConfigChangeListener {

  private final AccessLogFilter accessLogFilter;

  @Inject
  public AccessLogExcludeListener(AccessLogFilter accessLogFilter) {
    this.accessLogFilter = accessLogFilter;
  }

  public String getKeyPath() {
    return GlobalConfKeys.accessLogExcludeRegex.getKey();
  }

  @Override
  public void processGlobal() {
    accessLogFilter.refreshPatterns();
  }
}
