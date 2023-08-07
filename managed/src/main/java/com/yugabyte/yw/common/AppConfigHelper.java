// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.config.RuntimeConfService;
import java.util.List;
import java.util.stream.Collectors;

/* Helper class for application config */
@Singleton
public class AppConfigHelper {

  public static final String YB_STORAGE_PATH = "yb.storage.path";
  public static final String YB_DEVOPS_PATH = "yb.devops.home";

  private static final String WS_RUNTIME_CONFIG_SUFFIX = ".ws";

  @Inject private static Config config;

  public static String getStoragePath() {
    return config.getString(YB_STORAGE_PATH);
  }

  public static String getDevopsHomePath() {
    return config.getString(YB_DEVOPS_PATH);
  }

  public static List<String> getRefreshableClients() {
    List<String> refreshableClients =
        config.getStringList(RuntimeConfService.INCLUDED_OBJECTS_KEY).stream()
            .filter(object -> object.endsWith(WS_RUNTIME_CONFIG_SUFFIX))
            .collect(Collectors.toList());
    return refreshableClients;
  }
}
