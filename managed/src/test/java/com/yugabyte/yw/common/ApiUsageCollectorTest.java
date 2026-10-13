// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.ApiUsageCollector.ClientKey;
import com.yugabyte.yw.common.ApiUsageCollector.Counts;
import com.yugabyte.yw.common.ApiUsageCollector.DeprecatedApiKey;
import com.yugabyte.yw.common.ApiUsageCollector.Snapshot;
import java.time.Instant;
import org.junit.Test;

public class ApiUsageCollectorTest {
  private static final ClientKey CLI = new ClientKey("v1", "yba-cli", "2025.2.0", "api_token");
  private static final DeprecatedApiKey DEPRECATED =
      new DeprecatedApiKey("v1", "POST", "/api/v1/customers/$cUUID/old", "yba-cli");

  @Test
  public void testCountsByStatus() {
    ApiUsageCollector collector = new ApiUsageCollector();
    collector.record(CLI, null, 200);
    collector.record(CLI, DEPRECATED, 404);
    collector.record(CLI, null, 503);

    Snapshot snapshot = collector.snapshot(Instant.now());
    assertEquals(new Counts(3, 1, 1), snapshot.clients().get(CLI));
    assertEquals(new Counts(1, 1, 0), snapshot.deprecatedApis().get(DEPRECATED));
  }

  @Test
  public void testAcknowledgeKeepsCallsAfterSnapshot() {
    ApiUsageCollector collector = new ApiUsageCollector();
    collector.record(CLI, null, 200);
    Instant sentAt = Instant.parse("2026-10-06T00:00:00Z");
    Snapshot first = collector.snapshot(sentAt);
    collector.record(CLI, null, 200);
    collector.acknowledge(first);

    Snapshot second = collector.snapshot(sentAt.plusSeconds(60));
    assertEquals(new Counts(1, 0, 0), second.clients().get(CLI));
    assertEquals(sentAt, second.windowStart());

    collector.acknowledge(second);
    assertTrue(collector.snapshot(Instant.now()).clients().isEmpty());
  }

  @Test
  public void testUnacknowledgedSnapshotIsReportedAgain() {
    ApiUsageCollector collector = new ApiUsageCollector();
    collector.record(CLI, null, 200);
    collector.snapshot(Instant.now());
    collector.record(CLI, null, 200);
    assertEquals(new Counts(2, 0, 0), collector.snapshot(Instant.now()).clients().get(CLI));
  }

  @Test
  public void testEntriesAreCapped() {
    ApiUsageCollector collector = new ApiUsageCollector();
    for (int i = 0; i < ApiUsageCollector.MAX_ENTRIES; i++) {
      collector.record(
          new ClientKey("v1", "client" + i, "", "none"),
          new DeprecatedApiKey("v1", "GET", "/api/v1/old" + i, "client" + i),
          200);
    }
    // Over the cap: counted as dropped calls, once per call and per map.
    ClientKey overCap = new ClientKey("v1", "overcap", "", "none");
    DeprecatedApiKey overCapRoute = new DeprecatedApiKey("v1", "GET", "/api/v1/new", "overcap");
    for (int i = 0; i < 3; i++) {
      collector.record(overCap, overCapRoute, 200);
    }
    collector.record(new ClientKey("v1", "client0", "", "none"), null, 200);

    Snapshot snapshot = collector.snapshot(Instant.now());
    assertEquals(ApiUsageCollector.MAX_ENTRIES, snapshot.clients().size());
    assertEquals(ApiUsageCollector.MAX_ENTRIES, snapshot.deprecatedApis().size());
    assertEquals(3, snapshot.droppedClientCalls());
    assertEquals(3, snapshot.droppedDeprecatedApiCalls());
    assertEquals(
        new Counts(2, 0, 0), snapshot.clients().get(new ClientKey("v1", "client0", "", "none")));

    collector.acknowledge(snapshot);
    assertEquals(0, collector.snapshot(Instant.now()).droppedClientCalls());
  }

  @Test
  public void testToJson() {
    ApiUsageCollector collector = new ApiUsageCollector();
    collector.record(CLI, DEPRECATED, 500);
    JsonNode json = collector.snapshot(Instant.parse("2026-10-06T00:00:00Z")).toJson();

    assertEquals(
        Instant.parse("2026-10-06T00:00:00Z").getEpochSecond(), json.get("window_end").asLong());
    JsonNode client = json.get("clients").get(0);
    assertEquals("v1", client.get("api_version").asText());
    assertEquals("yba-cli", client.get("client").asText());
    assertEquals("2025.2.0", client.get("client_version").asText());
    assertEquals("api_token", client.get("auth_type").asText());
    assertEquals(1, client.get("calls").asLong());
    assertEquals(1, client.get("errors_5xx").asLong());
    JsonNode deprecated = json.get("deprecated_apis").get(0);
    assertEquals("/api/v1/customers/$cUUID/old", deprecated.get("route").asText());
    assertEquals("POST", deprecated.get("method").asText());
  }
}
