// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.config.impl;

import com.yugabyte.yw.common.config.RuntimeConfigChangeListener;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.pa.PerfAdvisorService;
import com.yugabyte.yw.models.Universe;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;

/**
 * Sends a universe's {@code yb.pa.active_table_metrics.enabled} to its PA Collector when the flag
 * changes, so the collector starts or stops pushing the universe's active table list.
 */
@Slf4j
@Singleton
public class PaActiveTableMetricsListener implements RuntimeConfigChangeListener {

  private final PerfAdvisorService perfAdvisorService;

  @Inject
  public PaActiveTableMetricsListener(PerfAdvisorService perfAdvisorService) {
    this.perfAdvisorService = perfAdvisorService;
  }

  @Override
  public String getKeyPath() {
    return UniverseConfKeys.paActiveTableMetricsEnabled.getKey();
  }

  @Override
  public void processUniverse(Universe universe) {
    try {
      perfAdvisorService.syncActiveTableMetrics(universe);
    } catch (RuntimeException e) {
      // The flag is saved either way; the next registration of the universe sends it.
      log.warn(
          "Failed to send active table metrics setting of universe {} to its PA Collector",
          universe.getUniverseUUID(),
          e);
    }
  }
}
