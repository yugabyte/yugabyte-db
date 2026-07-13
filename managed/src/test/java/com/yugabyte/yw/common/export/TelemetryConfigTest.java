// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.export;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.models.helpers.exporters.audit.AuditLogConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.MetricsExportConfig;
import com.yugabyte.yw.models.helpers.exporters.query.QueryLogConfig;
import com.yugabyte.yw.models.helpers.exporters.server.MasterLogConfig;
import com.yugabyte.yw.models.helpers.exporters.server.TServerLogConfig;
import com.yugabyte.yw.models.helpers.telemetry.ExportType;
import java.util.Collections;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Set;
import org.junit.Test;

/**
 * Guards the single per-type mapping in {@link TelemetryConfig#section}. If a new {@link
 * ExportType} is added without wiring its field, these tests fail loudly naming the type instead of
 * the gap surfacing as a silent no-op in diff/hasAnyConfig.
 */
public class TelemetryConfigTest {

  /** A config with every section populated with a distinct instance. */
  private static TelemetryConfig fullyPopulated() {
    TelemetryConfig cfg = new TelemetryConfig();
    cfg.setAuditLogConfig(new AuditLogConfig());
    cfg.setQueryLogConfig(new QueryLogConfig());
    cfg.setMetricsExportConfig(new MetricsExportConfig());
    cfg.setMasterLogConfig(new MasterLogConfig());
    cfg.setTserverLogConfig(new TServerLogConfig());
    return cfg;
  }

  @Test
  public void sectionWiresEveryExportTypeToADistinctField() {
    TelemetryConfig cfg = fullyPopulated();
    Set<Object> seen = Collections.newSetFromMap(new IdentityHashMap<>());
    for (ExportType type : ExportType.values()) {
      Object section = cfg.section(type);
      assertNotNull("No config field wired in section() for export type " + type, section);
      assertTrue(
          "Two export types resolve to the same field; check section() for " + type,
          seen.add(section));
    }
    assertEquals(ExportType.values().length, seen.size());
  }

  @Test
  public void hasAnyConfigDerivesFromSection() {
    assertFalse(new TelemetryConfig().hasAnyConfig());
    for (ExportType type : ExportType.values()) {
      assertTrue("hasAnyConfig() missed export type " + type, isDetectedWhenOnlySet(type));
    }
  }

  @Test
  public void diffReportsEachChangedTypeAndNothingWhenEqual() {
    assertTrue(TelemetryConfig.diff(fullyPopulated(), fullyPopulated()).isEmpty());
    // Clearing exactly one section from a fully-populated config must surface exactly that type.
    for (ExportType type : ExportType.values()) {
      TelemetryConfig changed = fullyPopulated();
      clearSection(changed, type);
      List<ExportType> modified = TelemetryConfig.diff(fullyPopulated(), changed);
      assertEquals("diff() should report exactly [" + type + "]", List.of(type), modified);
    }
  }

  /** True when a config with only {@code type}'s section set is reported by hasAnyConfig(). */
  private static boolean isDetectedWhenOnlySet(ExportType type) {
    TelemetryConfig cfg = fullyPopulated();
    for (ExportType other : ExportType.values()) {
      if (other != type) {
        clearSection(cfg, other);
      }
    }
    return cfg.hasAnyConfig();
  }

  private static void clearSection(TelemetryConfig cfg, ExportType type) {
    switch (type) {
      case AUDIT_LOGS:
        cfg.setAuditLogConfig(null);
        break;
      case QUERY_LOGS:
        cfg.setQueryLogConfig(null);
        break;
      case METRICS:
        cfg.setMetricsExportConfig(null);
        break;
      case MASTER_LOGS:
        cfg.setMasterLogConfig(null);
        break;
      case TSERVER_LOGS:
        cfg.setTserverLogConfig(null);
        break;
      default:
        throw new IllegalArgumentException("Unhandled export type: " + type);
    }
  }
}
