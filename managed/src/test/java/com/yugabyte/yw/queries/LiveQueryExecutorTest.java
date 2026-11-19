/*
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.queries;

import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.queries.QueryHelper.QueryApi;
import org.junit.Before;
import org.junit.Test;
import org.mockito.Mock;
import play.libs.Json;

public class LiveQueryExecutorTest {

  @Mock ApiHelper mockClient;
  private LiveQueryExecutor liveQueryExecutor;

  @Before
  public void setUp() {
    liveQueryExecutor =
        new LiveQueryExecutor("test-node", "test-host", 12000, QueryApi.YCQL, mockClient, true);
  }

  @Test
  public void testCQLRpczResponseProcessing() {
    JsonNode cqlRpcz = TestUtils.readResourceAsJson("live_query/cql_rpcz.json");
    JsonNode processResult = liveQueryExecutor.processYCQLRowData(cqlRpcz);
    JsonNode expectedResult =
        TestUtils.readResourceAsJson("live_query/cql_rpcz_process_result.json");
    // Remove id field from each entry as it's randomly generated and hard to assert
    ArrayNode nodes = (ArrayNode) processResult.get("ycql");
    nodes.elements().forEachRemaining(node -> ((ObjectNode) node).remove("id"));
    assertThat(processResult, equalTo(expectedResult));
  }

  @Test
  public void testRpczBodyIsLoggedOnlyWhenAllowed() throws Exception {
    ApiHelper apiHelper = mock(ApiHelper.class);
    when(apiHelper.getRequest(anyString())).thenReturn(Json.newObject());
    when(apiHelper.getRequestWithoutLoggingBody(anyString())).thenReturn(Json.newObject());
    String url = "http://test-host:13000/rpcz";

    new LiveQueryExecutor("test-node", "test-host", 13000, QueryApi.YSQL, apiHelper, true).call();
    verify(apiHelper).getRequest(url);
    verify(apiHelper, never()).getRequestWithoutLoggingBody(anyString());

    new LiveQueryExecutor("test-node", "test-host", 13000, QueryApi.YSQL, apiHelper, false).call();
    verify(apiHelper).getRequestWithoutLoggingBody(url);
    verify(apiHelper, times(1)).getRequest(anyString());
  }
}
