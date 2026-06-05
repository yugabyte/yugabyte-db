// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.api.v2;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yba.v2.client.ApiClient;
import com.yugabyte.yba.v2.client.ApiException;
import com.yugabyte.yba.v2.client.Configuration;
import com.yugabyte.yba.v2.client.api.UniverseApi;
import com.yugabyte.yba.v2.client.models.TelemetryConfig;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.controllers.UniverseControllerTestBase;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.ExportTelemetryConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.exporters.audit.AuditLogConfig;
import com.yugabyte.yw.models.helpers.exporters.audit.UniverseLogsExporterConfig;
import com.yugabyte.yw.models.helpers.exporters.query.QueryLogConfig;
import com.yugabyte.yw.models.helpers.exporters.query.UniverseQueryLogsExporterConfig;
import java.util.Collections;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;
import play.mvc.Result;

public class UniverseApiControllerExportTelemetryConfigTest extends UniverseControllerTestBase {

  private Customer customer;
  private Universe universe;
  private String authToken;
  private UniverseApi apiClient;

  @Before
  @Override
  public void setUp() {
    this.customer = ModelFactory.testCustomer();
    Users user = ModelFactory.testUser(customer);
    this.authToken = user.createAuthToken();
    this.universe = ModelFactory.createUniverse(customer.getId());

    ApiClient v2Client = Configuration.getDefaultApiClient();
    String basePath = String.format("http://localhost:%d/api/v2", port);
    v2Client = v2Client.setBasePath(basePath).addDefaultHeader("X-AUTH-TOKEN", authToken);
    Configuration.setDefaultApiClient(v2Client);
    apiClient = new UniverseApi();
  }

  @Test
  public void getReturnsEmptyTelemetryConfigWhenNoRow() throws ApiException {
    TelemetryConfig resp =
        apiClient.getExportTelemetryConfig(customer.getUuid(), universe.getUniverseUUID());
    assertNotNull(resp);
    assertNull(resp.getAuditLogs());
    assertNull(resp.getQueryLogs());
    assertNull(resp.getMetrics());
  }

  @Test
  public void getReturnsAuditConfigAndOmitsIrrelevantFieldsOnWire() {
    UUID auditExporterUuid = UUID.randomUUID();
    UniverseLogsExporterConfig auditExporter = new UniverseLogsExporterConfig();
    auditExporter.setExporterUuid(auditExporterUuid);
    auditExporter.setAdditionalTags(Collections.singletonMap("env", "test"));
    AuditLogConfig auditConfig = new AuditLogConfig();
    auditConfig.setUniverseLogsExporterConfig(List.of(auditExporter));
    persistTelemetryConfig(
        universe.getUniverseUUID(),
        com.yugabyte.yw.common.export.TelemetryConfig.of(auditConfig, null, null));

    JsonNode body = rawGet();
    JsonNode entry = body.get("audit_logs").get("exporters").get(0);
    assertEquals(auditExporterUuid.toString(), entry.get("exporter_uuid").asText());
    assertEquals("test", entry.get("additional_tags").get("env").asText());
    // Fields not relevant to audit must NOT be present in the JSON output.
    assertFalse("send_batch_max_size leaked into audit response", entry.has("send_batch_max_size"));
    assertFalse("send_batch_size leaked into audit response", entry.has("send_batch_size"));
    assertFalse(
        "send_batch_timeout_seconds leaked into audit response",
        entry.has("send_batch_timeout_seconds"));
    assertFalse("memory_limit_mib leaked into audit response", entry.has("memory_limit_mib"));
    assertFalse(
        "memory_limit_check_interval_seconds leaked into audit response",
        entry.has("memory_limit_check_interval_seconds"));
    assertFalse("metrics_prefix leaked into audit response", entry.has("metrics_prefix"));

    assertTrue(
        "query_logs should be absent",
        body.get("query_logs") == null || body.get("query_logs").isNull());
    assertTrue(
        "metrics should be absent", body.get("metrics") == null || body.get("metrics").isNull());
  }

  @Test
  public void getReturnsQueryConfigWithBatchKeptAndMetricsPrefixOmitted() {
    UniverseQueryLogsExporterConfig queryExporter = new UniverseQueryLogsExporterConfig();
    queryExporter.setExporterUuid(UUID.randomUUID());
    queryExporter.setSendBatchMaxSize(2000);
    queryExporter.setMemoryLimitMib(4096);
    QueryLogConfig queryConfig = new QueryLogConfig();
    queryConfig.setUniverseLogsExporterConfig(List.of(queryExporter));
    persistTelemetryConfig(
        universe.getUniverseUUID(),
        com.yugabyte.yw.common.export.TelemetryConfig.of(null, queryConfig, null));

    JsonNode body = rawGet();
    JsonNode entry = body.get("query_logs").get("exporters").get(0);
    assertEquals(2000, entry.get("send_batch_max_size").asInt());
    assertEquals(4096, entry.get("memory_limit_mib").asInt());
    assertFalse("metrics_prefix leaked into query response", entry.has("metrics_prefix"));
  }

  private JsonNode rawGet() {
    String path =
        String.format(
            "/api/v2/customers/%s/universes/%s/export-telemetry-configs",
            customer.getUuid(), universe.getUniverseUUID());
    Result result = doRequestWithAuthToken("GET", path, authToken);
    assertEquals(200, result.status());
    return Json.parse(contentAsString(result));
  }

  private static void persistTelemetryConfig(
      UUID universeUuid, com.yugabyte.yw.common.export.TelemetryConfig config) {
    ExportTelemetryConfig row = new ExportTelemetryConfig();
    row.setUniverseUuid(universeUuid);
    row.setTelemetryConfig(config);
    row.save();
  }
}
