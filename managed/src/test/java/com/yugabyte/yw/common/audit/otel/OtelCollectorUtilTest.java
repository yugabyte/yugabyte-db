// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.audit.otel;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.models.helpers.exporters.metrics.MetricsExportConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.ScrapeConfigTargetType;
import com.yugabyte.yw.models.helpers.exporters.metrics.UniverseMetricsExporterConfig;
import java.util.Collections;
import java.util.EnumSet;
import java.util.UUID;
import org.junit.Test;

public class OtelCollectorUtilTest {

  private MetricsExportConfig configWithTargets(ScrapeConfigTargetType... targets) {
    MetricsExportConfig config = new MetricsExportConfig();
    UniverseMetricsExporterConfig exporter = new UniverseMetricsExporterConfig();
    exporter.setExporterUuid(UUID.randomUUID());
    config.setUniverseMetricsExporterConfig(ImmutableList.of(exporter));
    config.setScrapeConfigTargets(
        targets.length == 0 ? Collections.emptySet() : ImmutableSet.copyOf(targets));
    return config;
  }

  @Test
  public void unsupportedK8sTargetsNullConfig() {
    assertTrue(OtelCollectorUtil.getUnsupportedK8sScrapeTargets(null).isEmpty());
  }

  @Test
  public void unsupportedK8sTargetsSupportedOnly() {
    assertTrue(
        OtelCollectorUtil.getUnsupportedK8sScrapeTargets(
                configWithTargets(
                    ScrapeConfigTargetType.MASTER_EXPORT,
                    ScrapeConfigTargetType.TSERVER_EXPORT,
                    ScrapeConfigTargetType.YSQL_EXPORT,
                    ScrapeConfigTargetType.CQL_EXPORT,
                    ScrapeConfigTargetType.OTEL_EXPORT))
            .isEmpty());
  }

  @Test
  public void unsupportedK8sTargetsReportsNodeTargets() {
    assertEquals(
        EnumSet.of(ScrapeConfigTargetType.NODE_EXPORT),
        OtelCollectorUtil.getUnsupportedK8sScrapeTargets(
            configWithTargets(
                ScrapeConfigTargetType.TSERVER_EXPORT, ScrapeConfigTargetType.NODE_EXPORT)));
    assertEquals(
        EnumSet.of(ScrapeConfigTargetType.NODE_EXPORT, ScrapeConfigTargetType.NODE_AGENT_EXPORT),
        OtelCollectorUtil.getUnsupportedK8sScrapeTargets(
            configWithTargets(
                ScrapeConfigTargetType.NODE_EXPORT, ScrapeConfigTargetType.NODE_AGENT_EXPORT)));
  }

  @Test
  public void unsupportedK8sTargetsEmptyMeansAllTargets() {
    // Empty/omitted target set resolves to all targets (config generator semantics), which
    // includes the node-based targets that K8s pods cannot serve.
    assertEquals(
        EnumSet.of(ScrapeConfigTargetType.NODE_EXPORT, ScrapeConfigTargetType.NODE_AGENT_EXPORT),
        OtelCollectorUtil.getUnsupportedK8sScrapeTargets(configWithTargets()));
  }
}
