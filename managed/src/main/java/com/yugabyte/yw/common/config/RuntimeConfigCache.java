/*
 * Copyright 2023 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.config;

import com.google.inject.Inject;
import com.google.inject.Singleton;

/**
 * Reads global runtime config flags on paths that run for every API call. The values come from the
 * runtime config factory's cache, so a change is visible once its transaction commits.
 */
@Singleton
public class RuntimeConfigCache {

  private final RuntimeConfigFactory runtimeConfigFactory;

  @Inject
  public RuntimeConfigCache(RuntimeConfigFactory runtimeConfigFactory) {
    this.runtimeConfigFactory = runtimeConfigFactory;
  }

  public boolean getBoolean(String key) {
    return runtimeConfigFactory.globalRuntimeConf().getBoolean(key);
  }
}
