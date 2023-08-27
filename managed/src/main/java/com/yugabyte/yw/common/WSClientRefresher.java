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
import com.typesafe.config.Config;
import com.typesafe.config.ConfigFactory;
import com.typesafe.config.ConfigRenderOptions;
import com.typesafe.config.ConfigValue;
import com.typesafe.config.ConfigValueFactory;
import com.yugabyte.yw.common.certmgmt.castore.CustomCAStoreManager;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import java.io.IOException;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import play.libs.ws.WSClient;

@Slf4j
@Singleton
public class WSClientRefresher implements CustomTrustStoreListener {

  private final CustomWsClientFactory customWsClientFactory;
  private final RuntimeConfigFactory runtimeConfigFactory;
  private final Map<String, WSClient> customWsClients = new ConcurrentHashMap<>();
  private final CustomCAStoreManager customCAStoreManager;

  @Inject
  public WSClientRefresher(
      CustomWsClientFactory customWsClientFactory,
      RuntimeConfigFactory runtimeConfigFactory,
      CustomCAStoreManager customCAStoreManager) {
    this.customWsClientFactory = customWsClientFactory;
    this.runtimeConfigFactory = runtimeConfigFactory;
    this.customCAStoreManager = customCAStoreManager;
    customCAStoreManager.addListener(this);
  }

  @Inject Config config;

  public void refreshWsClient(String ybWsConfigPath) {
    log.debug("Refreshing ws-client for {}", ybWsConfigPath);
    WSClient previousWsClient = customWsClients.put(ybWsConfigPath, newClient(ybWsConfigPath));
    closePreviousClient(previousWsClient);
  }

  public WSClient getClient(String ybWsConfigPath) {
    return customWsClients.computeIfAbsent(ybWsConfigPath, this::newClient);
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

  private WSClient newClient(String ybWsConfigPath) {
    ConfigValue ybWsOverrides = runtimeConfigFactory.globalRuntimeConf().getValue(ybWsConfigPath);

    // Add the custom CA truststore config if applicable.
    List<Map<String, String>> ybaStoreConfig = customCAStoreManager.getPemStoreConfig();
    if (!ybaStoreConfig.isEmpty() && !customCAStoreManager.isEnabled()) {
      log.warn("Skipping to add YBA's custom trust-store config as the feature is disabled");
    }
    if (!ybaStoreConfig.isEmpty() && customCAStoreManager.isEnabled()) {
      // Add JRE default cert paths as well in this case.
      ybaStoreConfig.add(customCAStoreManager.getJavaDefaultConfig());

      Config customWsConfig =
          ConfigFactory.empty()
              .withValue("play.ws", ybWsOverrides)
              .withValue(
                  "play.ws.ssl.trustManager.stores",
                  ConfigValueFactory.fromIterable(ybaStoreConfig));
      ybWsOverrides = customWsConfig.getValue("play.ws");
    }

    log.info(
        "Creating ws client with config override: {}",
        ybWsOverrides.render(ConfigRenderOptions.concise()));

    return customWsClientFactory.forCustomConfig(ybWsOverrides);
  }

  public void truststoreUpdated() {
    // Update all ws Client listeners like yb.alert.webhook.ws
    List<String> refreshableClientKeys = AppConfigHelper.getRefreshableClients();
    refreshableClientKeys.forEach(
        clientKey -> {
          if (customWsClients.containsKey(clientKey)) {
            refreshWsClient(clientKey);
          }
        });
  }
}
