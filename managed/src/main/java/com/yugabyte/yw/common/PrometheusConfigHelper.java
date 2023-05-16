/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.metrics.MetricUrlProvider;
import java.io.File;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

// Contains all the helper methods related to managing Prometheus
// configuration file.
@Singleton
@Slf4j
public class PrometheusConfigHelper {
  static final String PROMETHEUS_HOST_CONFIG_KEY = "yb.metrics.host";
  static final String PROMETHEUS_PORT_CONFIG_KEY = "yb.metrics.port";
  private static final String PROMETHEUS_FEDERATED_CONFIG_DIR_KEY = "yb.ha.prometheus_config_dir";
  private static final String PROMETHEUS_CONFIG_FILENAME = "prometheus.yml";

  private final RuntimeConfGetter confGetter;

  private final MetricUrlProvider metricUrlProvider;

  private final ApiHelper apiHelper;

  @Inject
  public PrometheusConfigHelper(
      RuntimeConfGetter confGetter, MetricUrlProvider metricUrlProvider, ApiHelper apiHelper) {
    this.confGetter = confGetter;
    this.metricUrlProvider = metricUrlProvider;
    this.apiHelper = apiHelper;
  }

  public String getPrometheusHost() {
    return confGetter.getStaticConf().getString(PROMETHEUS_HOST_CONFIG_KEY);
  }

  public int getPrometheusPort() {
    return confGetter.getStaticConf().getInt(PROMETHEUS_PORT_CONFIG_KEY);
  }

  private File getPrometheusConfigDir() {
    String outputDirString =
        confGetter.getStaticConf().getString(PROMETHEUS_FEDERATED_CONFIG_DIR_KEY);

    return new File(outputDirString);
  }

  public File getPrometheusConfigFile() {
    File configDir = getPrometheusConfigDir();

    return new File(configDir, PROMETHEUS_CONFIG_FILENAME);
  }

  public void reloadPrometheusConfig() {
    try {
      String baseUrl = metricUrlProvider.getMetricsInternalUrl();
      String reloadUrl = baseUrl + "/-/reload";

      // Send the reload request.
      this.apiHelper.postRequest(reloadUrl, Json.newObject());
    } catch (Exception e) {
      log.error("Error reloading prometheus config", e);
    }
  }
}
