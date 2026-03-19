// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.export;

import com.yugabyte.yw.forms.ExportTelemetryConfigParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import lombok.AllArgsConstructor;
import lombok.Getter;
import lombok.NoArgsConstructor;
import lombok.Setter;

/**
 * Typed upgrade options for export-telemetry config (rolling vs non-rolling, delays). Stored in
 * {@link com.yugabyte.yw.models.ExportTelemetryConfig}; reuses {@link
 * UpgradeTaskParams.UpgradeOption}. JSON uses snake_case to match OpenAPI/DB.
 */
@Getter
@Setter
@NoArgsConstructor
@AllArgsConstructor
public class ExportTelemetryUpgradeOptions {

  public static final int DEFAULT_SLEEP_AFTER_RESTART_MS = 180000;

  /** API request uses this (boolean); when null, use upgradeOption (e.g. from DB). */
  private Boolean rollingUpgrade;

  private UpgradeTaskParams.UpgradeOption upgradeOption =
      UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE;
  private Integer delayBetweenMasterServers = 0;
  private Integer delayBetweenTserverServers = 0;
  private Integer sleepAfterTserverRestartMillis = DEFAULT_SLEEP_AFTER_RESTART_MS;
  private Integer sleepAfterMasterRestartMillis = DEFAULT_SLEEP_AFTER_RESTART_MS;

  /** Default options (rolling, zero delay, default sleep). */
  public static ExportTelemetryUpgradeOptions defaults() {
    return new ExportTelemetryUpgradeOptions(
        null,
        UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE,
        0,
        0,
        DEFAULT_SLEEP_AFTER_RESTART_MS,
        DEFAULT_SLEEP_AFTER_RESTART_MS);
  }

  /** Build from task params when persisting. */
  public static ExportTelemetryUpgradeOptions fromParams(ExportTelemetryConfigParams params) {
    ExportTelemetryUpgradeOptions o = new ExportTelemetryUpgradeOptions();
    o.setUpgradeOption(params.upgradeOption);
    o.setDelayBetweenMasterServers(
        params.delayBetweenMasterServers != null ? params.delayBetweenMasterServers : 0);
    o.setDelayBetweenTserverServers(
        params.delayBetweenTserverServers != null ? params.delayBetweenTserverServers : 0);
    o.setSleepAfterTserverRestartMillis(
        params.sleepAfterTServerRestartMillis != null
            ? params.sleepAfterTServerRestartMillis
            : DEFAULT_SLEEP_AFTER_RESTART_MS);
    o.setSleepAfterMasterRestartMillis(
        params.sleepAfterMasterRestartMillis != null
            ? params.sleepAfterMasterRestartMillis
            : DEFAULT_SLEEP_AFTER_RESTART_MS);
    return o;
  }
}
