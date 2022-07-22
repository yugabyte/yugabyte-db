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

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.typesafe.config.ConfigFactory;
import com.typesafe.config.ConfigRenderOptions;
import com.typesafe.config.ConfigValue;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.CustomWsClientFactory;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import java.io.IOException;
import lombok.extern.slf4j.Slf4j;
import play.libs.ws.WSClient;

@Singleton
@Slf4j
public class PlatformInstanceClientFactory {

  public static final String YB_HA_WS_KEY = "yb.ha.ws";
  private final CustomWsClientFactory customWsClientFactory;
  private final RuntimeConfigFactory runtimeConfigFactory;
  private WSClient customWsClient = null;

  @Inject
  public PlatformInstanceClientFactory(
      CustomWsClientFactory customWsClientFactory, RuntimeConfigFactory runtimeConfigFactory) {
    this.customWsClientFactory = customWsClientFactory;
    this.runtimeConfigFactory = runtimeConfigFactory;
  }

  public synchronized void refreshWsClient(String haWsConfigPath) {
    ConfigValue haWsOverrides = runtimeConfigFactory.globalRuntimeConf().getValue(haWsConfigPath);
    log.info(
        "Creating ws client with config override: {}",
        haWsOverrides.render(ConfigRenderOptions.concise()));
    Config customWsConfig =
        ConfigFactory.empty()
            .withValue("play.ws", haWsOverrides)
            .withFallback(runtimeConfigFactory.staticApplicationConf())
            .withOnlyPath("play.ws");
    // Enable trace level logging to debug actual config value being resolved:
    log.trace("Creating ws client with config: {}", customWsConfig.root().render());
    closePreviousClient(customWsClient);
    customWsClient = customWsClientFactory.forCustomConfig(customWsConfig);
  }

  private void closePreviousClient(WSClient previousWsClient) {
    if (previousWsClient != null) {
      try {
        previousWsClient.close();
      } catch (IOException e) {
        log.warn("Exception while closing wsClient. Ignored", e);
      }
    }
  }

  public PlatformInstanceClient getClient(String clusterKey, String remoteAddress) {
    if (customWsClient == null) {
      log.info("Creating customWsClient for first time");
      refreshWsClient(YB_HA_WS_KEY);
    }
    return new PlatformInstanceClient(new ApiHelper(customWsClient), clusterKey, remoteAddress);
  }
}
