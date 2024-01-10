// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.config.impl;

import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfigChangeListener;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;

@Singleton
@Slf4j
public class BootstrapRequiredRpcPoolMaxThreadListener implements RuntimeConfigChangeListener {

  private final XClusterUniverseService xClusterUniverseService;
  private final RuntimeConfGetter confGetter;

  @Inject
  public BootstrapRequiredRpcPoolMaxThreadListener(
      XClusterUniverseService xClusterUniverseService, RuntimeConfGetter confGetter) {
    this.xClusterUniverseService = xClusterUniverseService;
    this.confGetter = confGetter;
  }

  public String getKeyPath() {
    return GlobalConfKeys.xclusterBootstrapRequiredRpcMaxThreads.getKey();
  }

  @Override
  public void processGlobal() {
    xClusterUniverseService.isBootstrapRequiredExecutor.setMaximumPoolSize(
        confGetter.getGlobalConf(GlobalConfKeys.xclusterBootstrapRequiredRpcMaxThreads));
    log.debug(
        "isBootstrapRequiredExecutor max pool size set to: {}",
        xClusterUniverseService.isBootstrapRequiredExecutor.getMaximumPoolSize());
  }
}
