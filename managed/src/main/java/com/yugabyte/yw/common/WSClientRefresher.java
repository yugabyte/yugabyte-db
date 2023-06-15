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

import com.cronutils.utils.StringUtils;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.typesafe.config.ConfigFactory;
import com.typesafe.config.ConfigRenderOptions;
import com.typesafe.config.ConfigValue;
import com.typesafe.config.ConfigValueFactory;
import com.yugabyte.yw.common.certmgmt.castore.CustomCAStoreManager;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.security.KeyStore;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import play.libs.ws.WSClient;

@Slf4j
@Singleton
public class WSClientRefresher implements CustomTrustStoreListener {

  private static final String YB_JAVA_HOME_PATHS = "yb.wellKnownCA.trustStore.javaHomePaths";

  private final CustomWsClientFactory customWsClientFactory;
  private final RuntimeConfigFactory runtimeConfigFactory;
  private final Map<String, WSClient> customWsClients = new ConcurrentHashMap<>();
  private final CustomCAStoreManager customCAStoreManager;
  private final RuntimeConfGetter runtimeConfGetter;

  @Inject
  public WSClientRefresher(
      CustomWsClientFactory customWsClientFactory,
      RuntimeConfigFactory runtimeConfigFactory,
      CustomCAStoreManager customCAStoreManager,
      RuntimeConfGetter runtimeConfGetter) {
    this.customWsClientFactory = customWsClientFactory;
    this.runtimeConfigFactory = runtimeConfigFactory;
    this.customCAStoreManager = customCAStoreManager;
    this.runtimeConfGetter = runtimeConfGetter;
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
    if (!ybaStoreConfig.isEmpty()) {
      // Add JRE default cert paths as well in this case.
      ybaStoreConfig.add(getJavaDefaultConfig());

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

  private Map<String, String> getJavaDefaultConfig() {
    // Java looks for trust-store in these files by default in this order.
    // NOTE: If adding any custom path, we must add the ordered default path as well, if they exist.
    Map<String, String> javaSSLConfigMap = new HashMap<>();

    String javaxNetSslTrustStore =
        runtimeConfGetter.getGlobalConf(GlobalConfKeys.javaxNetSslTrustStore);
    String javaxNetSslTrustStoreType =
        runtimeConfGetter.getGlobalConf(GlobalConfKeys.javaxNetSslTrustStoreType);
    String javaxNetSslTrustStorePassword =
        runtimeConfGetter.getGlobalConf(GlobalConfKeys.javaxNetSslTrustStorePassword);
    log.debug(
        "Javax truststore is: {}, type is: {}", javaxNetSslTrustStore, javaxNetSslTrustStoreType);
    if (!StringUtils.isEmpty(javaxNetSslTrustStore)
        && Files.exists(Paths.get(javaxNetSslTrustStore))) {
      javaSSLConfigMap.put("path", javaxNetSslTrustStore);
      if (!StringUtils.isEmpty(javaxNetSslTrustStoreType)) {
        javaSSLConfigMap.put("type", javaxNetSslTrustStoreType);
      }
      if (!StringUtils.isEmpty(javaxNetSslTrustStorePassword)) {
        javaSSLConfigMap.put("password", javaxNetSslTrustStorePassword);
      }
      return javaSSLConfigMap;
    }

    List<String> javaHomePaths = config.getStringList(YB_JAVA_HOME_PATHS);
    log.debug("Java home cert paths are {}", javaHomePaths);
    for (String javaPath : javaHomePaths) {
      if (Files.exists(Paths.get(javaPath))) {
        javaSSLConfigMap.put("path", javaPath);
        javaSSLConfigMap.put("type", KeyStore.getDefaultType()); // pkcs12
      }
    }
    log.info("Java SSL config is:{}", javaSSLConfigMap);
    return javaSSLConfigMap;
  }
}
