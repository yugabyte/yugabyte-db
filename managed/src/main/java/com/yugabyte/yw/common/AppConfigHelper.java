// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;

/* Helper class for Files */
@Singleton
public class AppConfigHelper {

  public static final String YB_STORAGE_PATH = "yb.storage.path";

  @Inject private static Config config;

  public static String getStoragePath() {
    return config.getString(YB_STORAGE_PATH);
  }
}
