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
import com.yugabyte.yw.metrics.MetricQueryHelper;
import java.io.File;
import lombok.extern.slf4j.Slf4j;

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

  private final MetricQueryHelper metricQueryHelper;

  @Inject
  public PrometheusConfigHelper(RuntimeConfGetter confGetter, MetricQueryHelper metricQueryHelper) {
    this.confGetter = confGetter;
    this.metricQueryHelper = metricQueryHelper;
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
      metricQueryHelper.postManagementCommand(MetricQueryHelper.MANAGEMENT_COMMAND_RELOAD);
    } catch (Exception e) {
      log.error("Error reloading prometheus config", e);
    }
  }
}
