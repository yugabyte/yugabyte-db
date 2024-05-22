/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.ApiUtils.getDefaultUserIntent;
import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertNotFound;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.AssertHelper.assertUnauthorizedNoException;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsBytes;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.google.common.net.HostAndPort;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.rbac.Permission;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.metrics.MetricQueryResponse;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.rbac.ResourceGroup;
import com.yugabyte.yw.models.rbac.ResourceGroup.ResourceDefinition;
import com.yugabyte.yw.models.rbac.Role;
import com.yugabyte.yw.models.rbac.Role.RoleType;
import com.yugabyte.yw.models.rbac.RoleBinding;
import com.yugabyte.yw.models.rbac.RoleBinding.RoleBindingType;
import com.yugabyte.yw.queries.QueryHelper;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Duration;
import java.time.OffsetDateTime;
import java.time.ZoneOffset;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.FileUtils;
import org.apache.commons.lang3.tuple.ImmutablePair;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import play.libs.Json;
import play.libs.ws.WSClient;
import play.mvc.Result;

@Slf4j
@RunWith(JUnitParamsRunner.class)
public class UniverseInfoControllerTest extends UniverseControllerTestBase {
  @Mock PlatformExecutorFactory mockPlatformExecutorFactory;
  @Mock WSClient mockWsClient;

  Config config;
  Role role;
  ResourceDefinition rd1;

  Permission permission1 = new Permission(ResourceType.UNIVERSE, Action.CREATE);
  Permission permission2 = new Permission(ResourceType.UNIVERSE, Action.READ);
  Permission permission3 = new Permission(ResourceType.UNIVERSE, Action.UPDATE);
  Permission permission4 = new Permission(ResourceType.UNIVERSE, Action.DELETE);

  @Before
  public void setUpTest() {
    config = app.injector().instanceOf(Config.class);
  }

  @Test
  public void testGetMasterLeaderWithValidParams() {
    Universe universe = createUniverse(customer.getId());
    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + universe.getUniverseUUID()
            + "/leader";
    String host = "1.2.3.4";
    HostAndPort hostAndPort = HostAndPort.fromParts(host, 9000);
    when(mockClient.getLeaderMasterHostAndPort()).thenReturn(hostAndPort);
    when(mockService.getClient(any(), any())).thenReturn(mockClient);
    Result result = doRequestWithAuthToken("GET", url, authToken);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "privateIP", host);
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  @Parameters({
    "true, true, false",
    "false, false, false",
    "true, false, false",
    "false, true, false",
    "true, true, true" // prometheus error
  })
  public void testUniverseStatus(boolean masterAlive, boolean nodeExpAlive, boolean promError) {

    Universe u = createUniverse("TestUniverse", customer.getId());
    u = Universe.saveDetails(u.getUniverseUUID(), ApiUtils.mockUniverseUpdater("TestUniverse"));

    List<Integer> ports = new ArrayList<>();
    ports.add(9000);
    if (nodeExpAlive) {
      ports.add(9300);
    }
    if (masterAlive) {
      ports.add(7000);
    }

    // prep the fake prometheus response
    ArrayList<MetricQueryResponse.Entry> upMetricValues = new ArrayList<>();
    for (NodeDetails node : u.getNodes()) {
      for (int port : ports) {
        MetricQueryResponse.Entry entry = new MetricQueryResponse.Entry();
        entry.labels = new HashMap<>();
        entry.values = new ArrayList<ImmutablePair<Double, Double>>();
        entry.labels.put("instance", node.cloudInfo.private_ip + ":" + Integer.toString(port));
        entry.labels.put("node_prefix", "TestUniverse");
        entry.values.add(
            new ImmutablePair<Double, Double>(System.currentTimeMillis() / 1000.0, 1.0));
        upMetricValues.add(entry);
      }
    }
    if (!promError) {
      when(mockMetricQueryHelper.queryDirect(anyString())).thenReturn(upMetricValues);
    } else {
      when(mockMetricQueryHelper.queryDirect(anyString())).thenThrow(new RuntimeException());
    }
    String url =
        "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID() + "/status";
    Result result = doRequestWithAuthToken("GET", url, authToken);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "universe_uuid", u.getUniverseUUID().toString());
    assertEquals(ImmutableList.copyOf(json.fieldNames()).size(), 4);

    for (NodeDetails node : u.getNodes()) {
      JsonNode nodeJson = json.get(node.nodeName);
      assertEquals(nodeJson.get("tserver_alive").asText(), Boolean.toString(!promError));
      assertEquals(
          nodeJson.get("master_alive").asText(), Boolean.toString(masterAlive && !promError));
      assertEquals(
          nodeJson.get("node_status").asText(),
          (!promError ? (nodeExpAlive ? "Live" : "Unreachable") : "MetricsUnavailable"));
    }
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testDownloadNodeLogs_NodeNotFound() {
    when(mockShellProcessHandler.run(anyList(), any(ShellProcessContext.class)))
        .thenReturn(new ShellResponse());

    Universe u = createUniverse(customer.getId());
    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + u.getUniverseUUID()
            + "/dummy_node/download_logs";
    Result result = assertPlatformException(() -> doRequestWithAuthToken("GET", url, authToken));
    assertNotFound(result, "dummy_node");
    assertAuditEntry(0, customer.getUuid());
  }

  private byte[] createFakeLog(Path path) throws IOException {
    Random rng = new Random();
    File file = path.toFile();
    file.getParentFile().mkdirs();
    try (FileWriter out = new FileWriter(file)) {
      int sz = 1024 * 1024;
      byte[] arr = new byte[sz];
      rng.nextBytes(arr);
      out.write(new String(arr, StandardCharsets.UTF_8));
    }
    return FileUtils.readFileToByteArray(file);
  }

  @Test
  public void testDownloadNodeLogs() throws IOException {
    Path logPath = Paths.get(config.getString("yb.storage.path") + "/" + "10.0.0.1-logs.tar.gz");
    byte[] fakeLog = createFakeLog(logPath);
    when(mockShellProcessHandler.run(anyList(), any(ShellProcessContext.class)))
        .thenReturn(new ShellResponse());

    UniverseDefinitionTaskParams.UserIntent ui = getDefaultUserIntent(customer);
    String keyCode = "dummy_code";
    ui.accessKeyCode = keyCode;
    UUID uUUID = createUniverse(customer.getId()).getUniverseUUID();
    Universe.saveDetails(uUUID, ApiUtils.mockUniverseUpdater(ui));

    Provider provider = Provider.get(customer.getUuid(), Common.CloudType.aws).get(0);
    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    keyInfo.sshPort = 1223;
    AccessKey.create(provider.getUuid(), keyCode, keyInfo);

    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + uUUID
            + "/host-n1/"
            + "download_logs";
    Result result = doRequestWithAuthToken("GET", url, authToken);
    assertEquals(OK, result.status());
    byte[] actualContent = contentAsBytes(result, mat).toArray();
    assertArrayEquals(fakeLog, actualContent);
    assertAuditEntry(0, customer.getUuid());
    assertFalse(logPath.toFile().exists());
  }

  @Test
  public void testGetSlowQueries() {
    String jsonMsg =
        "{\"ysql\":{\"errorCount\":0,\"queries\":[]},"
            + "\"ycql\":{\"errorCount\":0,\"queries\":[]}}";
    when(mockQueryHelper.slowQueries(any())).thenReturn(Json.parse(jsonMsg));
    Universe u = createUniverse(customer.getId());
    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + u.getUniverseUUID()
            + "/slow_queries";
    Map<String, String> fakeRequestHeaders = new HashMap<>();
    fakeRequestHeaders.put("X-AUTH-TOKEN", authToken);

    Result result = doRequestWithCustomHeaders("GET", url, fakeRequestHeaders);
    assertOk(result);
    assertEquals(jsonMsg, contentAsString(result));
  }

  @Test
  public void testGetSlowQueriesUsingNewRbacAuthzWithPermissions() {
    String jsonMsg =
        "{\"ysql\":{\"errorCount\":0,\"queries\":[]},"
            + "\"ycql\":{\"errorCount\":0,\"queries\":[]}}";
    when(mockQueryHelper.slowQueries(any())).thenReturn(Json.parse(jsonMsg));
    Universe u = createUniverse(customer.getId());
    when(mockRuntimeConfig.getBoolean("yb.rbac.use_new_authz")).thenReturn(true);
    role =
        Role.create(
            customer.getUuid(),
            "FakeRole1",
            "testDescription",
            RoleType.Custom,
            new HashSet<>(Arrays.asList(permission1, permission2, permission3, permission4)));
    rd1 =
        ResourceDefinition.builder()
            .resourceType(ResourceType.UNIVERSE)
            .resourceUUIDSet(new HashSet<>(Arrays.asList(u.getUniverseUUID())))
            .build();
    ResourceGroup rG = new ResourceGroup(new HashSet<>(Arrays.asList(rd1)));
    RoleBinding.create(user, RoleBindingType.Custom, role, rG);
    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + u.getUniverseUUID()
            + "/slow_queries";
    Map<String, String> fakeRequestHeaders = new HashMap<>();
    fakeRequestHeaders.put("X-AUTH-TOKEN", authToken);

    Result result = doRequestWithCustomHeaders("GET", url, fakeRequestHeaders);
    assertOk(result);
    assertEquals(jsonMsg, contentAsString(result));
  }

  @Test
  public void testGetSlowQueriesUsingNewRbacAuthzWithAllUniversePermissions() {
    String jsonMsg =
        "{\"ysql\":{\"errorCount\":0,\"queries\":[]},"
            + "\"ycql\":{\"errorCount\":0,\"queries\":[]}}";
    when(mockQueryHelper.slowQueries(any())).thenReturn(Json.parse(jsonMsg));
    Universe u = createUniverse(customer.getId());
    when(mockRuntimeConfig.getBoolean("yb.rbac.use_new_authz")).thenReturn(true);
    role =
        Role.create(
            customer.getUuid(),
            "FakeRole1",
            "testDescription",
            RoleType.Custom,
            new HashSet<>(Arrays.asList(permission1, permission2, permission3, permission4)));
    rd1 = ResourceDefinition.builder().resourceType(ResourceType.UNIVERSE).allowAll(true).build();
    ResourceGroup rG = new ResourceGroup(new HashSet<>(Arrays.asList(rd1)));
    RoleBinding.create(user, RoleBindingType.Custom, role, rG);
    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + u.getUniverseUUID()
            + "/slow_queries";
    Map<String, String> fakeRequestHeaders = new HashMap<>();
    fakeRequestHeaders.put("X-AUTH-TOKEN", authToken);

    Result result = doRequestWithCustomHeaders("GET", url, fakeRequestHeaders);
    assertOk(result);
    assertEquals(jsonMsg, contentAsString(result));
  }

  @Test
  public void testGetSlowQueriesUsingNewRbacAuthzWithNoPermissions() {
    String jsonMsg =
        "{\"ysql\":{\"errorCount\":0,\"queries\":[]},"
            + "\"ycql\":{\"errorCount\":0,\"queries\":[]}}";
    when(mockQueryHelper.slowQueries(any())).thenReturn(Json.parse(jsonMsg));
    Universe u = createUniverse(customer.getId());
    when(mockRuntimeConfig.getBoolean("yb.rbac.use_new_authz")).thenReturn(true);
    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + u.getUniverseUUID()
            + "/slow_queries";
    Map<String, String> fakeRequestHeaders = new HashMap<>();
    fakeRequestHeaders.put("X-AUTH-TOKEN", authToken);

    Result result = doRequestWithCustomHeaders("GET", url, fakeRequestHeaders);
    assertUnauthorizedNoException(result, "Unable to authorize user");
  }

  @Test
  public void testGetSlowQueriesUsingNewRbacAuthzWithIncorrectPermissions() {
    String jsonMsg =
        "{\"ysql\":{\"errorCount\":0,\"queries\":[]},"
            + "\"ycql\":{\"errorCount\":0,\"queries\":[]}}";
    when(mockQueryHelper.slowQueries(any())).thenReturn(Json.parse(jsonMsg));
    Universe u = createUniverse(customer.getId());
    when(mockRuntimeConfig.getBoolean("yb.rbac.use_new_authz")).thenReturn(true);
    role =
        Role.create(
            customer.getUuid(),
            "FakeRole1",
            "testDescription",
            RoleType.Custom,
            new HashSet<>(Arrays.asList(permission1, permission3, permission4)));
    rd1 =
        ResourceDefinition.builder()
            .resourceType(ResourceType.UNIVERSE)
            .resourceUUIDSet(new HashSet<>(Arrays.asList(u.getUniverseUUID())))
            .build();
    ResourceGroup rG = new ResourceGroup(new HashSet<>(Arrays.asList(rd1)));
    RoleBinding.create(user, RoleBindingType.Custom, role, rG);
    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + u.getUniverseUUID()
            + "/slow_queries";
    Map<String, String> fakeRequestHeaders = new HashMap<>();
    fakeRequestHeaders.put("X-AUTH-TOKEN", authToken);

    Result result = doRequestWithCustomHeaders("GET", url, fakeRequestHeaders);
    assertUnauthorizedNoException(result, "Unable to authorize user");
  }

  @Test
  public void testSlowQueryLimitWithoutEnableNestloopOff() {
    when(mockRuntimeConfig.getString(QueryHelper.QUERY_STATS_SLOW_QUERIES_ORDER_BY_KEY))
        .thenReturn("total_time");
    when(mockRuntimeConfig.getInt(QueryHelper.QUERY_STATS_SLOW_QUERIES_LIMIT_KEY)).thenReturn(200);
    when(mockRuntimeConfig.getInt(QueryHelper.QUERY_STATS_SLOW_QUERIES_LENGTH_KEY))
        .thenReturn(1024);
    Universe universe = createUniverse(customer.getId());
    Universe.saveDetails(
        universe.getUniverseUUID(),
        u -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          universeDetails.getPrimaryCluster().userIntent.ybSoftwareVersion = "2.15.1.0-b10";
          universe.setUniverseDetails(universeDetails);
        });
    when(mockRuntimeConfigFactory.forUniverse(universe)).thenReturn(mockRuntimeConfig);
    when(mockRuntimeConfig.getBoolean(QueryHelper.SET_ENABLE_NESTLOOP_OFF_KEY)).thenReturn(false);
    ExecutorService executor = Executors.newFixedThreadPool(1);
    QueryHelper queryHelper = new QueryHelper(mockRuntimeConfigFactory, executor, mockWsClient);
    String actualSql = queryHelper.slowQuerySqlWithLimit(mockRuntimeConfig, universe, false);
    assertEquals(
        "/*+ Leading((d pg_stat_statements))  */ SELECT s.userid::regrole as rolname, d.datname,"
            + " s.queryid, LEFT(s.query, 1024) as query, s.calls, s.total_time, s.rows, s.min_time,"
            + " s.max_time, s.mean_time, s.stddev_time, s.local_blks_hit, s.local_blks_written FROM"
            + " pg_stat_statements s JOIN pg_database d ON d.oid = s.dbid ORDER BY s.total_time"
            + " DESC LIMIT 200",
        actualSql);
  }

  @Test
  public void testSlowQueryLimitWithEnableNestloopOff() {
    when(mockRuntimeConfig.getString(QueryHelper.QUERY_STATS_SLOW_QUERIES_ORDER_BY_KEY))
        .thenReturn("total_time");
    when(mockRuntimeConfig.getInt(QueryHelper.QUERY_STATS_SLOW_QUERIES_LIMIT_KEY)).thenReturn(200);
    when(mockRuntimeConfig.getInt(QueryHelper.QUERY_STATS_SLOW_QUERIES_LENGTH_KEY))
        .thenReturn(1024);
    Universe universe = createUniverse(customer.getId());
    Universe.saveDetails(
        universe.getUniverseUUID(),
        u -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          universeDetails.getPrimaryCluster().userIntent.ybSoftwareVersion = "2.17.1.0-b146";
          universe.setUniverseDetails(universeDetails);
        });
    when(mockRuntimeConfigFactory.forUniverse(universe)).thenReturn(mockRuntimeConfig);
    when(mockRuntimeConfig.getBoolean(QueryHelper.SET_ENABLE_NESTLOOP_OFF_KEY)).thenReturn(true);
    ExecutorService executor = Executors.newFixedThreadPool(1);
    QueryHelper queryHelper = new QueryHelper(mockRuntimeConfigFactory, executor, mockWsClient);
    String actualSql = queryHelper.slowQuerySqlWithLimit(mockRuntimeConfig, universe, false);
    assertEquals(
        "/*+ Leading((d pg_stat_statements)) Set(enable_nestloop off) */ SELECT s.userid::regrole"
            + " as rolname, d.datname, s.queryid, LEFT(s.query, 1024) as query, s.calls,"
            + " s.total_time, s.rows, s.min_time, s.max_time, s.mean_time, s.stddev_time,"
            + " s.local_blks_hit, s.local_blks_written FROM pg_stat_statements s JOIN pg_database d"
            + " ON d.oid = s.dbid ORDER BY s.total_time DESC LIMIT 200",
        actualSql);
  }

  @Test
  public void testTriggerHealthCheck() {
    when(mockRuntimeConfig.getBoolean("yb.health.trigger_api.enabled")).thenReturn(true);
    Universe u = createUniverse(customer.getId());
    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + u.getUniverseUUID()
            + "/trigger_health_check";

    OffsetDateTime before = OffsetDateTime.now(ZoneOffset.UTC);
    log.info("Before: {}", before);

    Result result = doRequestWithAuthToken("GET", url, authToken);
    assertOk(result);

    String json = contentAsString(result);
    log.info("Returned JSON response: {}", json);

    assertTrue(json.contains("timestamp"));
    ObjectNode node = (ObjectNode) Json.parse(json);

    String ts = node.get("timestamp").asText();
    assertNotNull(ts);
    log.info("Parsed TS string: {}", ts);

    OffsetDateTime tsFromResponse = OffsetDateTime.parse(ts);
    Duration d = Duration.between(before, tsFromResponse);
    log.info("Parsed duration: {}", d);

    assertTrue(d.toMinutes() < 60);
  }
}
