// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.helpers.exporters.audit;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import java.util.Collections;
import java.util.UUID;
import org.junit.Test;

public class AuditLogConfigTest {

  private AuditLogConfig configWithExporter() {
    AuditLogConfig config = new AuditLogConfig();
    UniverseLogsExporterConfig exporter = new UniverseLogsExporterConfig();
    exporter.setExporterUuid(UUID.randomUUID());
    config.setUniverseLogsExporterConfig(Collections.singletonList(exporter));
    return config;
  }

  @Test
  public void normalizeClearsExportActiveWhenExporterListEmpty() {
    AuditLogConfig config = new AuditLogConfig();
    config.setExportActive(true);
    config.setUniverseLogsExporterConfig(Collections.emptyList());

    config.normalizeExportActive();

    assertFalse(config.isExportActive());
  }

  @Test
  public void normalizeClearsExportActiveWhenExporterListNull() {
    AuditLogConfig config = new AuditLogConfig();
    config.setExportActive(true);
    config.setUniverseLogsExporterConfig(null);

    config.normalizeExportActive();

    assertFalse(config.isExportActive());
  }

  @Test
  public void normalizeKeepsExportActiveWhenExporterPresent() {
    AuditLogConfig config = configWithExporter();
    config.setExportActive(true);

    config.normalizeExportActive();

    assertTrue(config.isExportActive());
  }

  @Test
  public void normalizeLeavesExportInactiveUnchangedWithExporter() {
    // Exporter configured but export deliberately paused: normalization must not flip it back on.
    AuditLogConfig config = configWithExporter();
    config.setExportActive(false);

    config.normalizeExportActive();

    assertFalse(config.isExportActive());
  }

  @Test
  public void normalizeIsIdempotent() {
    AuditLogConfig config = new AuditLogConfig();
    config.setExportActive(true);
    config.setUniverseLogsExporterConfig(Collections.emptyList());

    config.normalizeExportActive();
    config.normalizeExportActive();

    assertFalse(config.isExportActive());
  }
}
