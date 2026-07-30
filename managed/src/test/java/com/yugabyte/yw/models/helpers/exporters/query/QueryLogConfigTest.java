// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.helpers.exporters.query;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import java.util.Collections;
import java.util.UUID;
import org.junit.Test;

public class QueryLogConfigTest {

  private QueryLogConfig configWithExporter() {
    QueryLogConfig config = new QueryLogConfig();
    UniverseQueryLogsExporterConfig exporter = new UniverseQueryLogsExporterConfig();
    exporter.setExporterUuid(UUID.randomUUID());
    config.setUniverseLogsExporterConfig(Collections.singletonList(exporter));
    return config;
  }

  @Test
  public void normalizeClearsExportActiveWhenExporterListEmpty() {
    QueryLogConfig config = new QueryLogConfig();
    config.setExportActive(true);
    config.setUniverseLogsExporterConfig(Collections.emptyList());

    config.normalizeExportActive();

    assertFalse(config.isExportActive());
  }

  @Test
  public void normalizeClearsExportActiveWhenExporterListNull() {
    QueryLogConfig config = new QueryLogConfig();
    config.setExportActive(true);
    config.setUniverseLogsExporterConfig(null);

    config.normalizeExportActive();

    assertFalse(config.isExportActive());
  }

  @Test
  public void normalizeKeepsExportActiveWhenExporterPresent() {
    QueryLogConfig config = configWithExporter();
    config.setExportActive(true);

    config.normalizeExportActive();

    assertTrue(config.isExportActive());
  }

  @Test
  public void normalizeLeavesExportInactiveUnchangedWithExporter() {
    // Exporter configured but export deliberately paused: normalization must not flip it back on.
    QueryLogConfig config = configWithExporter();
    config.setExportActive(false);

    config.normalizeExportActive();

    assertFalse(config.isExportActive());
  }

  @Test
  public void normalizeIsIdempotent() {
    QueryLogConfig config = new QueryLogConfig();
    config.setExportActive(true);
    config.setUniverseLogsExporterConfig(Collections.emptyList());

    config.normalizeExportActive();
    config.normalizeExportActive();

    assertFalse(config.isExportActive());
  }
}
