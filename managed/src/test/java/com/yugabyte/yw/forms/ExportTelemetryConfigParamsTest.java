// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.export.TelemetryConfig;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.TelemetryProvider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.exporters.audit.AuditLogConfig;
import com.yugabyte.yw.models.helpers.exporters.audit.UniverseLogsExporterConfig;
import com.yugabyte.yw.models.helpers.exporters.audit.YSQLAuditConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.MetricsExportConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.UniverseMetricsExporterConfig;
import com.yugabyte.yw.models.helpers.exporters.server.MasterLogConfig;
import com.yugabyte.yw.models.helpers.exporters.server.UniverseServerLogsExporterConfig;
import com.yugabyte.yw.models.helpers.telemetry.AWSCloudWatchConfig;
import com.yugabyte.yw.models.helpers.telemetry.DataDogConfig;
import com.yugabyte.yw.models.helpers.telemetry.DynatraceConfig;
import com.yugabyte.yw.models.helpers.telemetry.TelemetryProviderConfig;
import java.util.Collections;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import play.mvc.Http.Status;

/**
 * Validation tests for the unified export-telemetry-configs params, focused on the provider
 * capability matrix (PLAT-21973).
 */
public class ExportTelemetryConfigParamsTest extends FakeDBApplication {

  private Customer customer;
  private Universe universe;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse(customer.getId());
  }

  @Test
  public void testMetricsExportRejectsLogsOnlyProvider() {
    // CloudWatch is a logs-only sink.
    UUID exporterUuid = mockProvider("cloudwatch-sink", new AWSCloudWatchConfig());
    ExportTelemetryConfigParams params =
        paramsFor(
            TelemetryConfig.builder().metricsExportConfig(metricsConfig(exporterUuid)).build());

    PlatformServiceException exception =
        assertThrows(PlatformServiceException.class, () -> params.verifyParams(universe, true));
    assertEquals(Status.BAD_REQUEST, exception.getHttpStatus());
    assertTrue(
        exception.getMessage(),
        exception
            .getMessage()
            .contains(
                "Exporter 'cloudwatch-sink' of provider type 'AWS_CLOUDWATCH' is not allowed for"
                    + " metrics export"));
  }

  @Test
  public void testAuditLogExportRejectsMetricsOnlyProvider() {
    // Dynatrace is a metrics-only sink.
    UUID exporterUuid = mockProvider("dynatrace-sink", new DynatraceConfig());
    ExportTelemetryConfigParams params =
        paramsFor(TelemetryConfig.builder().auditLogConfig(auditConfig(exporterUuid)).build());

    PlatformServiceException exception =
        assertThrows(PlatformServiceException.class, () -> params.verifyParams(universe, true));
    assertEquals(Status.BAD_REQUEST, exception.getHttpStatus());
    assertTrue(
        exception.getMessage(),
        exception
            .getMessage()
            .contains(
                "Exporter 'dynatrace-sink' of provider type 'DYNATRACE' is not allowed for logs"
                    + " export"));
  }

  @Test
  public void testMasterLogExportRejectsMetricsOnlyProvider() {
    UUID exporterUuid = mockProvider("dynatrace-sink", new DynatraceConfig());
    UniverseServerLogsExporterConfig exporter = new UniverseServerLogsExporterConfig();
    exporter.setExporterUuid(exporterUuid);
    MasterLogConfig masterLogConfig = new MasterLogConfig();
    masterLogConfig.setUniverseLogsExporterConfig(Collections.singletonList(exporter));
    ExportTelemetryConfigParams params =
        paramsFor(TelemetryConfig.builder().masterLogConfig(masterLogConfig).build());

    PlatformServiceException exception =
        assertThrows(PlatformServiceException.class, () -> params.verifyParams(universe, true));
    assertEquals(Status.BAD_REQUEST, exception.getHttpStatus());
    assertTrue(
        exception.getMessage(), exception.getMessage().contains("is not allowed for logs export"));
  }

  @Test
  public void testDualSignalProviderAllowedForBothLogsAndMetrics() {
    // Datadog speaks both signals.
    UUID exporterUuid = mockProvider("datadog-sink", new DataDogConfig());
    ExportTelemetryConfigParams params =
        paramsFor(
            TelemetryConfig.builder()
                .auditLogConfig(auditConfig(exporterUuid))
                .metricsExportConfig(metricsConfig(exporterUuid))
                .build());

    params.verifyParams(universe, true);
  }

  private ExportTelemetryConfigParams paramsFor(TelemetryConfig telemetryConfig) {
    ExportTelemetryConfigParams params = new ExportTelemetryConfigParams();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.setTelemetryConfig(telemetryConfig);
    return params;
  }

  private UUID mockProvider(String name, TelemetryProviderConfig config) {
    UUID uuid = UUID.randomUUID();
    TelemetryProvider provider = new TelemetryProvider();
    provider.setUuid(uuid);
    provider.setCustomerUUID(customer.getUuid());
    provider.setName(name);
    provider.setConfig(config);
    when(mockTelemetryProviderService.getOrBadRequest(eq(uuid))).thenReturn(provider);
    return uuid;
  }

  private static AuditLogConfig auditConfig(UUID exporterUuid) {
    UniverseLogsExporterConfig exporter = new UniverseLogsExporterConfig();
    exporter.setExporterUuid(exporterUuid);
    YSQLAuditConfig ysqlAuditConfig = new YSQLAuditConfig();
    ysqlAuditConfig.setEnabled(true);
    AuditLogConfig auditLogConfig = new AuditLogConfig();
    auditLogConfig.setYsqlAuditConfig(ysqlAuditConfig);
    auditLogConfig.setExportActive(true);
    auditLogConfig.setUniverseLogsExporterConfig(Collections.singletonList(exporter));
    return auditLogConfig;
  }

  private static MetricsExportConfig metricsConfig(UUID exporterUuid) {
    UniverseMetricsExporterConfig exporter = new UniverseMetricsExporterConfig();
    exporter.setExporterUuid(exporterUuid);
    MetricsExportConfig metricsExportConfig = new MetricsExportConfig();
    metricsExportConfig.setUniverseMetricsExporterConfig(Collections.singletonList(exporter));
    return metricsExportConfig;
  }
}
