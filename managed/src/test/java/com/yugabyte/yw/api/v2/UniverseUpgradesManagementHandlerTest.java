// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.api.v2;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.when;

import api.v2.handlers.UniverseUpgradesManagementHandler;
import api.v2.models.ConfigureMetricsExportSpec;
import api.v2.models.MetricsExportConfig;
import api.v2.models.UniverseMetricsExporterConfig;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.SoftwareUpgradeHelper;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.controllers.RequestContext;
import com.yugabyte.yw.controllers.TokenAuthenticator;
import com.yugabyte.yw.controllers.handlers.UpgradeUniverseHandler;
import com.yugabyte.yw.forms.ExportTelemetryConfigParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.TelemetryProvider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.TelemetryProviderService;
import com.yugabyte.yw.models.helpers.exporters.audit.AuditLogConfig;
import com.yugabyte.yw.models.helpers.exporters.audit.UniverseLogsExporterConfig;
import com.yugabyte.yw.models.helpers.exporters.audit.YSQLAuditConfig;
import com.yugabyte.yw.models.helpers.exporters.query.QueryLogConfig;
import com.yugabyte.yw.models.helpers.exporters.query.UniverseQueryLogsExporterConfig;
import com.yugabyte.yw.models.helpers.telemetry.DataDogConfig;
import java.util.Collections;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.mockito.ArgumentCaptor;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.MockitoAnnotations;
import play.mvc.Http.Request;
import play.test.Helpers;

public class UniverseUpgradesManagementHandlerTest extends FakeDBApplication {

  @InjectMocks private UniverseUpgradesManagementHandler handler;

  @Mock private UpgradeUniverseHandler v1Handler;
  @Mock private Commissioner commissioner;
  @Mock private RuntimeConfGetter confGetter;
  @Mock private TelemetryProviderService telemetryProviderService;
  @Mock private SoftwareUpgradeHelper softwareUpgradeHelper;

  private Customer customer;
  private Universe universe;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    Users user = ModelFactory.testUser(customer);
    universe = ModelFactory.createUniverse(customer.getId());
    MockitoAnnotations.openMocks(this);
    // The v1 params mappers stamp creatingUser from the request context, so it must be populated.
    TestUtils.setFakeHttpContext(user);
  }

  @After
  public void tearDown() {
    RequestContext.clean(Set.of(TokenAuthenticator.USER));
  }

  private Request createRequest() {
    return Helpers.fakeRequest(
            "POST",
            "/customers/"
                + customer.getUuid()
                + "/universes/"
                + universe.getUniverseUUID()
                + "/configure-metrics-export")
        .build();
  }

  /**
   * Regression test: enabling metrics export used to wipe out the universe's existing audit and
   * query log export configs. The handler built its params via a mapper that does not populate
   * telemetryConfig, then read the (null) audit/query configs back and persisted them - clobbering
   * the real configs. configureMetricsExport must instead preserve the current audit/query configs,
   * sourcing them from the current telemetry config.
   */
  @Test
  public void testConfigureMetricsExportPreservesAuditAndQueryLogConfigs() throws Exception {
    // Existing audit + query log export configs currently on the universe (source of truth).
    AuditLogConfig existingAudit = new AuditLogConfig();
    YSQLAuditConfig ysqlAudit = new YSQLAuditConfig();
    ysqlAudit.setEnabled(true);
    existingAudit.setYsqlAuditConfig(ysqlAudit);
    existingAudit.setExportActive(true);
    UniverseLogsExporterConfig auditExporter = new UniverseLogsExporterConfig();
    auditExporter.setExporterUuid(UUID.randomUUID());
    existingAudit.setUniverseLogsExporterConfig(Collections.singletonList(auditExporter));

    QueryLogConfig existingQuery = new QueryLogConfig();
    existingQuery.setExportActive(true);
    UniverseQueryLogsExporterConfig queryExporter = new UniverseQueryLogsExporterConfig();
    queryExporter.setExporterUuid(UUID.randomUUID());
    existingQuery.setUniverseLogsExporterConfig(Collections.singletonList(queryExporter));

    // Persist the existing audit + query configs on the universe so the real
    // OtelCollectorUtil.getCurrentTelemetryConfig (a static source-of-truth read) returns them.
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams details = u.getUniverseDetails();
              UniverseDefinitionTaskParams.UserIntent userIntent =
                  details.getPrimaryCluster().userIntent;
              userIntent.auditLogConfig = existingAudit;
              userIntent.queryLogConfig = existingQuery;
              u.setUniverseDetails(details);
            });

    // Telemetry provider backing the new metrics exporter (DATA_DOG is allowed for metrics).
    UUID metricsExporterUuid = UUID.randomUUID();
    when(telemetryProviderService.checkIfExists(any(), any())).thenReturn(true);
    TelemetryProvider tp = new TelemetryProvider();
    DataDogConfig ddConfig = new DataDogConfig();
    ddConfig.setApiKey("api-key");
    ddConfig.setSite("us3.datadoghq.com");
    tp.setConfig(ddConfig);
    when(telemetryProviderService.get(any())).thenReturn(tp);
    when(telemetryProviderService.areTPsCredentialsConsistentOnUniverse(any(), any(), any(), any()))
        .thenReturn(true);

    UUID taskUuid = UUID.randomUUID();
    ArgumentCaptor<ExportTelemetryConfigParams> captor =
        ArgumentCaptor.forClass(ExportTelemetryConfigParams.class);
    when(v1Handler.submitExportTelemetryConfigs(captor.capture(), any(), any(Universe.class)))
        .thenReturn(taskUuid);

    // Request: enable metrics export to metricsExporterUuid, installing the collector.
    ConfigureMetricsExportSpec spec = new ConfigureMetricsExportSpec();
    spec.setInstallOtelCollector(true);
    MetricsExportConfig metricsConfig = new MetricsExportConfig();
    UniverseMetricsExporterConfig metricsExporter = new UniverseMetricsExporterConfig();
    metricsExporter.setExporterUuid(metricsExporterUuid);
    metricsConfig.setUniverseMetricsExporterConfig(Collections.singletonList(metricsExporter));
    spec.setMetricsExportConfig(metricsConfig);

    handler.configureMetricsExport(
        createRequest(), customer.getUuid(), universe.getUniverseUUID(), spec);

    ExportTelemetryConfigParams submitted = captor.getValue();
    // Audit + query log configs are preserved (the regression persisted these as null).
    assertNotNull("Audit log config must be preserved", submitted.getAuditLogConfig());
    assertEquals(existingAudit, submitted.getAuditLogConfig());
    assertNotNull("Query log config must be preserved", submitted.getQueryLogConfig());
    assertEquals(existingQuery, submitted.getQueryLogConfig());
    // Metrics export config reflects the request.
    assertNotNull(submitted.getMetricsExportConfig());
    List<com.yugabyte.yw.models.helpers.exporters.metrics.UniverseMetricsExporterConfig>
        submittedExporters = submitted.getMetricsExportConfig().getUniverseMetricsExporterConfig();
    assertEquals(1, submittedExporters.size());
    assertEquals(metricsExporterUuid, submittedExporters.get(0).getExporterUuid());
  }
}
