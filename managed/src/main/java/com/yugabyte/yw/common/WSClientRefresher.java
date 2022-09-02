/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common;

import com.google.inject.Inject;
import com.typesafe.config.ConfigRenderOptions;
import com.typesafe.config.ConfigValue;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import java.io.IOException;
import lombok.extern.slf4j.Slf4j;
import play.libs.ws.WSClient;

@Slf4j
public class WSClientRefresher {

  private final CustomWsClientFactory customWsClientFactory;
  private final RuntimeConfigFactory runtimeConfigFactory;
  private WSClient customWsClient = null;

  @Inject
  public WSClientRefresher(
      CustomWsClientFactory customWsClientFactory, RuntimeConfigFactory runtimeConfigFactory) {
    this.customWsClientFactory = customWsClientFactory;
    this.runtimeConfigFactory = runtimeConfigFactory;
  }

  public synchronized void refreshWsClient(String ybWsConfigPath) {
    ConfigValue ybWsOverrides = runtimeConfigFactory.globalRuntimeConf().getValue(ybWsConfigPath);
    log.info(
        "Creating ws client with config override: {}",
        ybWsOverrides.render(ConfigRenderOptions.concise()));
    closePreviousClient(customWsClient);
    customWsClient = customWsClientFactory.forCustomConfig(ybWsOverrides);
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

  public synchronized WSClient getClient(String ybWsConfigPath) {
    if (customWsClient == null) {
      log.info("Creating customWsClient for first time");
      refreshWsClient(ybWsConfigPath);
    }
    return customWsClient;
  }
}
