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
 *
 * <p>Sleep-after-restart fields are nullable. When null, the consuming task should resolve the
 * default from runtime config (e.g. {@code UniverseConfKeys.sleepAfterMasterRestartMs} / {@code
 * UniverseConfKeys.sleepAfterTServerRestartMs}) see {@code
 * UniverseTaskBase.getSleepTimeForProcess()}.
 */
@Getter
@Setter
@NoArgsConstructor
@AllArgsConstructor
public class ExportTelemetryUpgradeOptions {

  /** API request uses this (boolean); when null, use upgradeOption (e.g. from DB). */
  private Boolean rollingUpgrade;

  private UpgradeTaskParams.UpgradeOption upgradeOption =
      UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE;
  private Integer delayBetweenMasterServers = 0;
  private Integer delayBetweenTserverServers = 0;
  private Integer sleepAfterTserverRestartMillis;
  private Integer sleepAfterMasterRestartMillis;

  /** Default options (rolling, zero delay, null sleep resolved from runtime config at use). */
  public static ExportTelemetryUpgradeOptions defaults() {
    return new ExportTelemetryUpgradeOptions(
        null, UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, 0, 0, null, null);
  }

  /** Build from task params when persisting. Null sleep values defer to runtime config. */
  public static ExportTelemetryUpgradeOptions fromParams(ExportTelemetryConfigParams params) {
    ExportTelemetryUpgradeOptions o = new ExportTelemetryUpgradeOptions();
    o.setUpgradeOption(params.upgradeOption);
    o.setDelayBetweenMasterServers(
        params.delayBetweenMasterServers != null ? params.delayBetweenMasterServers : 0);
    o.setDelayBetweenTserverServers(
        params.delayBetweenTserverServers != null ? params.delayBetweenTserverServers : 0);
    o.setSleepAfterTserverRestartMillis(params.sleepAfterTServerRestartMillis);
    o.setSleepAfterMasterRestartMillis(params.sleepAfterMasterRestartMillis);
    return o;
  }
}
