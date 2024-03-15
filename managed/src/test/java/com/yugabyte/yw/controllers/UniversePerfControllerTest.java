/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.common.TestHelper.testDatabase;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.CustomWsClientFactory;
import com.yugabyte.yw.common.CustomWsClientFactoryProvider;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.audit.AuditService;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.controllers.handlers.HashedTimestampColumnFinder;
import com.yugabyte.yw.controllers.handlers.UniversePerfHandler;
import com.yugabyte.yw.controllers.handlers.UnusedIndexFinder;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import java.time.OffsetDateTime;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import java.util.concurrent.Executors;
import java.util.concurrent.ThreadPoolExecutor;
import junitparams.JUnitParamsRunner;
import kamon.instrumentation.play.GuiceModule;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.mvc.Result;

@RunWith(JUnitParamsRunner.class)
public class UniversePerfControllerTest extends FakeDBApplication {
  @Rule public MockitoRule rule = MockitoJUnit.rule();

  private UniversePerfController universePerfController;
  private UniversePerfHandler universePerfHandler;
  private HashedTimestampColumnFinder hashedTimestampColumnFinder;
  private AuditService auditService;
  private ThreadPoolExecutor executorService;
  private UnusedIndexFinder unusedIndexFinder;
  private NodeUniverseManager mockNodeUniverseManager = mock(NodeUniverseManager.class);
  private PlatformExecutorFactory mockPlatformExecutorFactory = mock(PlatformExecutorFactory.class);
  private RuntimeConfGetter mockConfGetter = mock(RuntimeConfGetter.class);
  private Customer customer;
  private Universe universe;
  private OffsetDateTime mockedTime = OffsetDateTime.now();
  private List<ShellResponse> shellResponses = new ArrayList<>();
  private List<ShellResponse> shellResponsesHashTimestamp = new ArrayList<>();
  private List<ShellResponse> shellResponsesUnusedIndex = new ArrayList<>();

  @Override
  protected Application provideApplication() {
    mockNodeUniverseManager = mock(NodeUniverseManager.class);
    mockPlatformExecutorFactory = mock(PlatformExecutorFactory.class);
    hashedTimestampColumnFinder = spy(new HashedTimestampColumnFinder(mockNodeUniverseManager));
    unusedIndexFinder =
        spy(
            new UnusedIndexFinder(
                mockNodeUniverseManager, mockPlatformExecutorFactory, mockConfGetter));
    universePerfHandler = spy(new TestUniversePerfHandler(mockNodeUniverseManager));
    universePerfController =
        spy(
            new UniversePerfController(
                universePerfHandler, hashedTimestampColumnFinder, unusedIndexFinder));

    return configureApplication(
            new GuiceApplicationBuilder()
                .disable(GuiceModule.class)
                .configure(testDatabase())
                .overrides(bind(NodeUniverseManager.class).toInstance(mockNodeUniverseManager))
                .overrides(
                    bind(PlatformExecutorFactory.class).toInstance(mockPlatformExecutorFactory))
                .overrides(
                    bind(CustomWsClientFactory.class)
                        .toProvider(CustomWsClientFactoryProvider.class)))
        .build();
  }

  @Before
  public void setUp() {
    auditService = new AuditService();
    executorService = (ThreadPoolExecutor) Executors.newFixedThreadPool(1);
    universePerfController.setAuditService(auditService);

    // Parse different shell responses and populate in shellResponses list
    for (int i = 1; i <= 7; i++) {
      ShellResponse shellResponse =
          ShellResponse.create(
              ShellResponse.ERROR_CODE_SUCCESS,
              TestUtils.readResource(
                  "com/yugabyte/yw/controllers/universe_performance_advisor/query_distribution_shell_response_"
                      + i
                      + ".txt"));
      shellResponses.add(shellResponse);
    }

    // Parse different shell responses and populate in shellResponsesHashTimestamp list
    for (int i = 0; i <= 4; i++) {
      ShellResponse shellResponse =
          ShellResponse.create(
              ShellResponse.ERROR_CODE_SUCCESS,
              TestUtils.readResource(
                  "com/yugabyte/yw/controllers/universe_performance_advisor/"
                      + "range_hash_shell_response_"
                      + i
                      + ".txt"));
      shellResponsesHashTimestamp.add(shellResponse);
    }

    // Parse different shell responses and populate in shellResponsesUnusedIndex list
    for (int i = 0; i <= 5; i++) {
      ShellResponse shellResponse =
          ShellResponse.create(
              ShellResponse.ERROR_CODE_SUCCESS,
              TestUtils.readResource(
                  "com/yugabyte/yw/controllers/universe_performance_advisor/"
                      + "unused_index_shell_response_"
                      + i
                      + ".txt"));
      shellResponsesUnusedIndex.add(shellResponse);
    }

    when(mockConfGetter.getConfForScope(any(Universe.class), eq(UniverseConfKeys.maxThreads)))
        .thenReturn(22);

    customer = ModelFactory.testCustomer();
    universe = createUniverse(customer.getId());
    universe = Universe.saveDetails(universe.getUniverseUUID(), ApiUtils.mockUniverseUpdater());
  }

  @Test
  public void testQueryDistributionNoSuggestion1() {
    // Test case where all nodes of universe have equal query load, and hence no query distribution
    // related suggestions should be given.
    when(mockNodeUniverseManager.runYsqlCommand(any(), any(), any(), any()))
        .thenReturn(shellResponses.get(0));

    Result queryDistributionSuggestions =
        universePerfController.getQueryDistributionSuggestions(
            customer.getUuid(), universe.getUniverseUUID());
    JsonNode json = Json.parse(contentAsString(queryDistributionSuggestions));
    assertEquals(OK, queryDistributionSuggestions.status());
    assertTrue(json.isObject());
    assertNull(json.get("suggestion"));
    assertNull(json.get("description"));
    assertNull(json.get("startTime"));
    assertNotNull(json.get("endTime"));
    assertNotNull(json.get("details"));
    Iterator<JsonNode> details = json.get("details").iterator();
    while (details.hasNext()) {
      JsonNode detail = details.next();
      assertNotNull(detail.get("node").asText());
      assertEquals(1000, detail.get("numSelect").asInt());
      assertEquals(0, detail.get("numInsert").asInt());
      assertEquals(0, detail.get("numUpdate").asInt());
      assertEquals(0, detail.get("numDelete").asInt());
    }
  }

  @Test
  public void testQueryDistributionNoSuggestion2() {
    // Test case where suggestion would not be given because of minimum query count threshold
    // criteria (MINIMUM_TOTAL_QUERY_THRESHOLD_FOR_SUGGESTIONS=1000) not being satisfied.
    when(mockNodeUniverseManager.runYsqlCommand(any(), any(), any(), any()))
        .thenReturn(shellResponses.get(2), shellResponses.get(1));

    Result queryDistributionSuggestions =
        universePerfController.getQueryDistributionSuggestions(
            customer.getUuid(), universe.getUniverseUUID());
    JsonNode json = Json.parse(contentAsString(queryDistributionSuggestions));
    assertEquals(OK, queryDistributionSuggestions.status());
    assertTrue(json.isObject());
    assertNull(json.get("suggestion"));
    assertNull(json.get("description"));
    assertNull(json.get("startTime"));
    assertNotNull(json.get("endTime"));
    assertNotNull(json.get("details"));
    Iterator<JsonNode> details = json.get("details").iterator();

    int numNodesWithNumSelect999 = 0;
    int numNodesWithNumSelect0 = 0;
    while (details.hasNext()) {
      JsonNode detail = details.next();
      if (detail.get("numSelect").asInt() == 0) {
        numNodesWithNumSelect0++;
      } else if (detail.get("numSelect").asInt() == 999) {
        numNodesWithNumSelect999++;
      }
      assertNotNull(detail.get("node").asText());
      assertEquals(0, detail.get("numInsert").asInt());
      assertEquals(0, detail.get("numUpdate").asInt());
      assertEquals(0, detail.get("numDelete").asInt());
    }
    assertEquals(1, numNodesWithNumSelect999);
    assertEquals(2, numNodesWithNumSelect0);
  }

  @Test
  public void testQueryDistributionNoSuggestion3() {
    // Test case where one of the nodes was overloaded more than 1 hours back, but no node is
    // overloaded in last 1 hour, and hence no suggestions.

    // Return 1000 numSelect for 1 node, 0 for remaining nodes
    when(mockNodeUniverseManager.runYsqlCommand(any(), any(), any(), any()))
        .thenReturn(shellResponses.get(0), shellResponses.get(1));

    Result queryDistributionSuggestions =
        universePerfController.getQueryDistributionSuggestions(
            customer.getUuid(), universe.getUniverseUUID());
    JsonNode json = Json.parse(contentAsString(queryDistributionSuggestions));
    assertEquals(OK, queryDistributionSuggestions.status());
    assertTrue(json.isObject());

    assertNull(json.get("startTime"));
    assertNotNull(json.get("endTime"));
    assertNotNull(json.get("details"));
    Iterator<JsonNode> details = json.get("details").elements();

    int numNodesWithNumSelect1000 = 0;
    int numNodesWithNumSelect0 = 0;
    String nodeWithHeavyQueryLoad = null;
    while (details.hasNext()) {
      JsonNode detail = details.next();
      if (detail.get("numSelect").asInt() == 0) {
        numNodesWithNumSelect0++;
      } else if (detail.get("numSelect").asInt() == 1000) {
        nodeWithHeavyQueryLoad = detail.get("node").asText();
        numNodesWithNumSelect1000++;
      }
      assertNotNull(detail.get("node").asText());
      assertEquals(0, detail.get("numInsert").asInt());
      assertEquals(0, detail.get("numUpdate").asInt());
      assertEquals(0, detail.get("numDelete").asInt());
    }
    assertEquals(1, numNodesWithNumSelect1000);
    assertEquals(2, numNodesWithNumSelect0);

    assertTrue(json.get("description").asText().contains(nodeWithHeavyQueryLoad));
    assertEquals(
        "Redistribute queries to other nodes in the cluster", json.get("suggestion").asText());

    // Go 2 hours ahead in time.
    mockedTime = mockedTime.plusHours(2);

    // Mock the query response such that all nodes had no load in last 2 hours.
    when(mockNodeUniverseManager.runYsqlCommand(any(), any(), any(), any()))
        .thenReturn(shellResponses.get(3));
    queryDistributionSuggestions =
        universePerfController.getQueryDistributionSuggestions(
            customer.getUuid(), universe.getUniverseUUID());
    json = Json.parse(contentAsString(queryDistributionSuggestions));
    assertEquals(OK, queryDistributionSuggestions.status());
    assertTrue(json.isObject());

    assertNull(json.get("description"));
    assertNull(json.get("suggestion"));
    assertNotNull(json.get("startTime"));
    assertNotNull(json.get("endTime"));
    assertNotNull(json.get("details"));
  }

  @Test
  public void testQueryDistributionWithSuggestion1() {
    // Test case when one node is overloaded.

    // Return 1000 numSelect for 1 node, 0 for remaining nodes
    when(mockNodeUniverseManager.runYsqlCommand(any(), any(), any(), any()))
        .thenReturn(shellResponses.get(0), shellResponses.get(1));

    Result queryDistributionSuggestions =
        universePerfController.getQueryDistributionSuggestions(
            customer.getUuid(), universe.getUniverseUUID());
    JsonNode json = Json.parse(contentAsString(queryDistributionSuggestions));
    assertEquals(OK, queryDistributionSuggestions.status());
    assertTrue(json.isObject());

    assertNull(json.get("startTime"));
    assertNotNull(json.get("endTime"));
    assertNotNull(json.get("details"));
    Iterator<JsonNode> details = json.get("details").elements();

    int numNodesWithNumSelect1000 = 0;
    int numNodesWithNumSelect0 = 0;
    String nodeWithHeavyQueryLoad = null;
    while (details.hasNext()) {
      JsonNode detail = details.next();
      if (detail.get("numSelect").asInt() == 0) {
        numNodesWithNumSelect0++;
      } else if (detail.get("numSelect").asInt() == 1000) {
        nodeWithHeavyQueryLoad = detail.get("node").asText();
        numNodesWithNumSelect1000++;
      }
      assertNotNull(detail.get("node").asText());
      assertEquals(0, detail.get("numInsert").asInt());
      assertEquals(0, detail.get("numUpdate").asInt());
      assertEquals(0, detail.get("numDelete").asInt());
    }
    assertEquals(1, numNodesWithNumSelect1000);
    assertEquals(2, numNodesWithNumSelect0);

    assertTrue(json.get("description").asText().contains(nodeWithHeavyQueryLoad));
    assertEquals(
        "Redistribute queries to other nodes in the cluster", json.get("suggestion").asText());
  }

  @Test
  public void testQueryDistributionWithSuggestion2() {
    // Test case where multiple nodes are overloaded, and the most overloaded node is reported.

    // Return 1000 numSelect for 1 node, 0 for remaining nodes
    when(mockNodeUniverseManager.runYsqlCommand(any(), any(), any(), any()))
        .thenReturn(shellResponses.get(0), shellResponses.get(4), shellResponses.get(5));

    Result queryDistributionSuggestions =
        universePerfController.getQueryDistributionSuggestions(
            customer.getUuid(), universe.getUniverseUUID());
    JsonNode json = Json.parse(contentAsString(queryDistributionSuggestions));
    assertEquals(OK, queryDistributionSuggestions.status());
    assertTrue(json.isObject());

    assertNull(json.get("startTime"));
    assertNotNull(json.get("endTime"));
    assertNotNull(json.get("details"));
    Iterator<JsonNode> details = json.get("details").elements();

    int numNodesWithNumSelect1000 = 0;
    String nodeWithHeavyQueryLoad = null;
    while (details.hasNext()) {
      JsonNode detail = details.next();
      if (detail.get("numSelect").asInt() == 1000) {
        nodeWithHeavyQueryLoad = detail.get("node").asText();
        numNodesWithNumSelect1000++;
      }
      assertNotNull(detail.get("node").asText());
      assertEquals(0, detail.get("numInsert").asInt());
      assertEquals(0, detail.get("numUpdate").asInt());
      assertEquals(0, detail.get("numDelete").asInt());
    }
    assertEquals(1, numNodesWithNumSelect1000);

    assertTrue(json.get("description").asText().contains(nodeWithHeavyQueryLoad));
    assertEquals(
        "Node "
            + nodeWithHeavyQueryLoad
            + " processed 233.33% more queries than average of other 2 nodes.",
        json.get("description").asText());
    assertEquals(
        "Redistribute queries to other nodes in the cluster", json.get("suggestion").asText());
  }

  @Test
  public void testQueryDistributionWithSuggestion3() {
    // Test case when one node is overloaded. This is similar to
    // testQueryDistributionWithSuggestion1, but tests different queries instead of only select.

    // Return 100 numSelect, 200 numDelete, 300 numInsert, 400 numUpdate queries for 1 node. 0 for
    // remaining nodes.
    when(mockNodeUniverseManager.runYsqlCommand(any(), any(), any(), any()))
        .thenReturn(shellResponses.get(6), shellResponses.get(1));

    Result queryDistributionSuggestions =
        universePerfController.getQueryDistributionSuggestions(
            customer.getUuid(), universe.getUniverseUUID());
    JsonNode json = Json.parse(contentAsString(queryDistributionSuggestions));
    assertEquals(OK, queryDistributionSuggestions.status());
    assertTrue(json.isObject());

    assertNull(json.get("startTime"));
    assertNotNull(json.get("endTime"));
    assertNotNull(json.get("details"));
    Iterator<JsonNode> details = json.get("details").elements();

    int numNodesWithNumSelect100 = 0;
    String nodeWithHeavyQueryLoad = null;
    while (details.hasNext()) {
      JsonNode detail = details.next();
      if (detail.get("numSelect").asInt() == 100) {
        numNodesWithNumSelect100++;
        assertEquals(200, detail.get("numDelete").asInt());
        assertEquals(300, detail.get("numInsert").asInt());
        assertEquals(400, detail.get("numUpdate").asInt());
        nodeWithHeavyQueryLoad = detail.get("node").asText();
      } else {
        assertEquals(0, detail.get("numSelect").asInt());
        assertEquals(0, detail.get("numInsert").asInt());
        assertEquals(0, detail.get("numUpdate").asInt());
        assertEquals(0, detail.get("numDelete").asInt());
      }
      assertNotNull(detail.get("node").asText());
    }

    assertEquals(1, numNodesWithNumSelect100);

    assertTrue(json.get("description").asText().contains(nodeWithHeavyQueryLoad));
    assertEquals(
        "Redistribute queries to other nodes in the cluster", json.get("suggestion").asText());
  }

  @Test
  public void testHashedTimestampColumnFinder1() {
    // Base case, no hashed timestamp indexes exist and getRangeHash returns an empty list.
    when(mockNodeUniverseManager.runYsqlCommand(any(), any(), any(), any()))
        .thenReturn(shellResponsesHashTimestamp.get(0), shellResponsesHashTimestamp.get(1));

    Result hashedTimestampResponse =
        universePerfController.getRangeHash(customer.getUuid(), universe.getUniverseUUID());
    JsonNode json = Json.parse(contentAsString(hashedTimestampResponse));

    assertEquals(OK, hashedTimestampResponse.status());
    assertTrue(json.isArray());
    assertEquals(0, json.size());
  }

  @Test
  public void testHashedTimestampColumnFinder2() {
    // Simplest case, one DB and one timestamp hash index.
    when(mockNodeUniverseManager.runYsqlCommand(any(), any(), any(), any()))
        .thenReturn(shellResponsesHashTimestamp.get(0), shellResponsesHashTimestamp.get(2));

    Result hashedTimestampResponse =
        universePerfController.getRangeHash(customer.getUuid(), universe.getUniverseUUID());
    JsonNode json = Json.parse(contentAsString(hashedTimestampResponse));

    assertEquals(OK, hashedTimestampResponse.status());
    assertTrue(json.isArray());
    assertEquals(1, json.size());

    assertEquals("yugabyte", json.get(0).get("current_database").asText());
    assertEquals("ts_test", json.get(0).get("table_name").asText());
    assertEquals("ts_test_pkey", json.get(0).get("index_name").asText());
    assertEquals(
        "CREATE UNIQUE INDEX ts_test_pkey ON public.ts_test USING lsm (ts1 HASH)",
        json.get(0).get("index_command").asText());
  }

  @Test
  public void testHashedTimestampColumnFinder3() {
    // 2 DBs and 3 timestamp hash indexes
    when(mockNodeUniverseManager.runYsqlCommand(any(), any(), any(), any()))
        .thenReturn(
            shellResponsesHashTimestamp.get(4),
            shellResponsesHashTimestamp.get(2),
            shellResponsesHashTimestamp.get(3));

    Result hashedTimestampResponse =
        universePerfController.getRangeHash(customer.getUuid(), universe.getUniverseUUID());
    JsonNode json = Json.parse(contentAsString(hashedTimestampResponse));

    assertEquals(OK, hashedTimestampResponse.status());
    assertTrue(json.isArray());
    assertEquals(3, json.size());

    assertEquals("yugabyte", json.get(0).get("current_database").asText());
    assertEquals("ts_test", json.get(0).get("table_name").asText());
    assertEquals("ts_test_pkey", json.get(0).get("index_name").asText());
    assertEquals(
        "CREATE UNIQUE INDEX ts_test_pkey ON public.ts_test USING lsm (ts1 HASH)",
        json.get(0).get("index_command").asText());
    assertEquals("yb_test", json.get(1).get("current_database").asText());
    assertEquals("ts_test", json.get(1).get("table_name").asText());
    assertEquals("ts_test_pkey", json.get(1).get("index_name").asText());
    assertEquals(
        "CREATE UNIQUE INDEX ts_test_pkey ON public.ts_test USING lsm (ts1 HASH)",
        json.get(1).get("index_command").asText());
    assertEquals("yb_test", json.get(2).get("current_database").asText());
    assertEquals("ts_test", json.get(2).get("table_name").asText());
    assertEquals("ts_test_ts2_idx", json.get(2).get("index_name").asText());
    assertEquals(
        "CREATE INDEX ts_test_ts2_idx ON public.ts_test USING lsm (ts2 HASH)",
        json.get(2).get("index_command").asText());
  }

  @Test
  public void testHashedTimestampColumnFinder4() {
    // 2 DBs and 1 timestamp hash index, second DB lacks hashed timestamp columns
    when(mockNodeUniverseManager.runYsqlCommand(any(), any(), any(), any()))
        .thenReturn(
            shellResponsesHashTimestamp.get(4),
            shellResponsesHashTimestamp.get(2),
            shellResponsesHashTimestamp.get(1));

    Result hashedTimestampResponse =
        universePerfController.getRangeHash(customer.getUuid(), universe.getUniverseUUID());
    JsonNode json = Json.parse(contentAsString(hashedTimestampResponse));

    assertEquals(OK, hashedTimestampResponse.status());
    assertTrue(json.isArray());
    assertEquals(1, json.size());

    assertEquals("yugabyte", json.get(0).get("current_database").asText());
    assertEquals("ts_test", json.get(0).get("table_name").asText());
    assertEquals("ts_test_pkey", json.get(0).get("index_name").asText());
    assertEquals(
        "CREATE UNIQUE INDEX ts_test_pkey ON public.ts_test USING lsm (ts1 HASH)",
        json.get(0).get("index_command").asText());
  }

  @Test
  public void testUnusedIndexFinder1() {
    // Base case, no unused indexes exist and getUnusedIndexes returns an empty list.
    when(mockNodeUniverseManager.runYsqlCommand(any(), any(), any(), any()))
        .thenReturn(shellResponsesUnusedIndex.get(0), shellResponsesUnusedIndex.get(1));
    when(mockPlatformExecutorFactory.createFixedExecutor(anyString(), anyInt(), any()))
        .thenReturn(executorService);

    Result unusedIndexResponse =
        universePerfController.getUnusedIndexes(customer.getUuid(), universe.getUniverseUUID());

    JsonNode json = Json.parse(contentAsString(unusedIndexResponse));
    assertEquals(OK, unusedIndexResponse.status());
    assertTrue(json.isArray());
    assertEquals(0, json.size());
  }

  @Test
  public void testUnusedIndexFinder2() {
    // 1 unused index, 1 DB
    when(mockNodeUniverseManager.runYsqlCommand(any(), any(), any(), any()))
        .thenReturn(shellResponsesUnusedIndex.get(0), shellResponsesUnusedIndex.get(2));
    when(mockPlatformExecutorFactory.createFixedExecutor(anyString(), anyInt(), any()))
        .thenReturn(executorService);
    // Seems like there are 3 nodes, and shellResponsesUnusedIndex.get(2) just gets called again for
    // the two nodes following the first because it is the last parameter in thenReturn.
    Result unusedIndexResponse =
        universePerfController.getUnusedIndexes(customer.getUuid(), universe.getUniverseUUID());

    JsonNode json = Json.parse(contentAsString(unusedIndexResponse));
    assertEquals(OK, unusedIndexResponse.status());
    assertTrue(json.isArray());
    assertEquals(1, json.size());

    assertEquals("yugabyte", json.get(0).get("current_database").asText());
    assertEquals("ts_test", json.get(0).get("table_name").asText());
    assertEquals("ts_test_b_idx", json.get(0).get("index_name").asText());
    assertEquals(
        "CREATE INDEX ts_test_b_idx ON public.ts_test USING lsm (b HASH)",
        json.get(0).get("index_command").asText());
  }

  @Test
  public void testUnusedIndexFinder3() {
    // 3 unused indexes, 2 DBs (3 nodes means 3 of the same command per DB).
    when(mockNodeUniverseManager.runYsqlCommand(any(), any(), any(), any()))
        .thenReturn(
            shellResponsesUnusedIndex.get(4),
            shellResponsesUnusedIndex.get(2),
            shellResponsesUnusedIndex.get(2),
            shellResponsesUnusedIndex.get(2),
            shellResponsesUnusedIndex.get(3),
            shellResponsesUnusedIndex.get(3),
            shellResponsesUnusedIndex.get(3));
    when(mockPlatformExecutorFactory.createFixedExecutor(anyString(), anyInt(), any()))
        .thenReturn(executorService, (ThreadPoolExecutor) Executors.newFixedThreadPool(1));
    Result unusedIndexResponse =
        universePerfController.getUnusedIndexes(customer.getUuid(), universe.getUniverseUUID());

    JsonNode json = Json.parse(contentAsString(unusedIndexResponse));
    assertEquals(OK, unusedIndexResponse.status());
    assertTrue(json.isArray());
    assertEquals(3, json.size());

    assertEquals("yugabyte", json.get(0).get("current_database").asText());
    assertEquals("ts_test", json.get(0).get("table_name").asText());
    assertEquals("ts_test_b_idx", json.get(0).get("index_name").asText());
    assertEquals(
        "CREATE INDEX ts_test_b_idx ON public.ts_test USING lsm (b HASH)",
        json.get(0).get("index_command").asText());
    assertEquals("yb_test", json.get(1).get("current_database").asText());
    assertEquals("ts_test", json.get(1).get("table_name").asText());
    assertEquals("ts_test_ts2_idx", json.get(1).get("index_name").asText());
    assertEquals(
        "CREATE INDEX ts_test_ts2_idx ON public.ts_test USING lsm (ts2 HASH)",
        json.get(1).get("index_command").asText());
    assertEquals("yb_test", json.get(2).get("current_database").asText());
    assertEquals("ts_test", json.get(2).get("table_name").asText());
    assertEquals("ts_test_pkey", json.get(2).get("index_name").asText());
    assertEquals(
        "CREATE UNIQUE INDEX ts_test_pkey ON public.ts_test USING lsm (ts1 HASH)",
        json.get(2).get("index_command").asText());
  }

  @Test
  public void testUnusedIndexFinder4() {
    // 2 indexes, 1 DB, one index eliminated since it only shows up twice across three nodes
    when(mockNodeUniverseManager.runYsqlCommand(any(), any(), any(), any()))
        .thenReturn(
            shellResponsesUnusedIndex.get(0),
            shellResponsesUnusedIndex.get(3),
            shellResponsesUnusedIndex.get(3),
            shellResponsesUnusedIndex.get(5));
    when(mockPlatformExecutorFactory.createFixedExecutor(anyString(), anyInt(), any()))
        .thenReturn(executorService);
    Result unusedIndexResponse =
        universePerfController.getUnusedIndexes(customer.getUuid(), universe.getUniverseUUID());

    JsonNode json = Json.parse(contentAsString(unusedIndexResponse));
    assertEquals(OK, unusedIndexResponse.status());
    assertTrue(json.isArray());
    assertEquals(1, json.size());

    assertEquals("yb_test", json.get(0).get("current_database").asText());
    assertEquals("ts_test", json.get(0).get("table_name").asText());
    assertEquals("ts_test_ts2_idx", json.get(0).get("index_name").asText());
    assertEquals(
        "CREATE INDEX ts_test_ts2_idx ON public.ts_test USING lsm (ts2 HASH)",
        json.get(0).get("index_command").asText());
  }

  private class TestUniversePerfHandler extends UniversePerfHandler {

    public TestUniversePerfHandler(NodeUniverseManager nodeUniverseManager) {
      super(nodeUniverseManager);
    }

    @Override
    public OffsetDateTime getCurrentOffsetDateTime() {
      return mockedTime;
    }
  }
}
