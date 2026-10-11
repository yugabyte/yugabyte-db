// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.YsqlQueryExecutor;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.queries.QueryHelper;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class SlowQueriesAggregatorTest extends FakeDBApplication {

  @Mock QueryHelper mockQueryHelper;
  @Mock YsqlQueryExecutor mockYsqlQueryExecutor;
  @Mock RuntimeConfGetter mockConfGetter;
  @Mock PlatformScheduler mockPlatformScheduler;

  private SlowQueriesAggregator slowQueriesAggregator;
  private Universe universe;

  @Before
  public void setUp() {
    slowQueriesAggregator =
        new SlowQueriesAggregator(
            mockQueryHelper, mockYsqlQueryExecutor, mockConfGetter, mockPlatformScheduler);
    Customer customer = ModelFactory.testCustomer();
    Provider provider = ModelFactory.awsProvider(customer);
    universe = ModelFactory.createFromConfig(provider, "slow-queries-universe", "r1-az1-1-1");
    when(mockConfGetter.getConfForScope(
            any(Universe.class), eq(UniverseConfKeys.slowQueryDisableAggregation)))
        .thenReturn(true);
  }

  private void enableAggregationForTestUniverse() {
    when(mockConfGetter.getConfForScope(universe, UniverseConfKeys.slowQueryDisableAggregation))
        .thenReturn(false);
  }

  @Test
  public void testScheduleRunnerDisablesCommandLoggingWhenFlagEnabled() {
    enableAggregationForTestUniverse();
    when(mockConfGetter.getConfForScope(universe, UniverseConfKeys.slowQueryDisableCommandLogging))
        .thenReturn(true);
    when(mockConfGetter.getConfForScope(universe, UniverseConfKeys.slowQueryRetentionDays))
        .thenReturn(7);
    when(mockQueryHelper.slowQueries(universe)).thenReturn(Json.newObject());

    slowQueriesAggregator.scheduleRunner();

    verify(mockYsqlQueryExecutor, times(2)).executeQueryInNodeShell(any(), any(), any(), eq(false));
    verify(mockYsqlQueryExecutor, never()).executeQueryBatchInNodeShell(any(), any(), any(), any());
  }

  @Test
  public void testScheduleRunnerLogsCommandsWhenFlagDisabled() {
    enableAggregationForTestUniverse();
    when(mockConfGetter.getConfForScope(universe, UniverseConfKeys.slowQueryDisableCommandLogging))
        .thenReturn(false);
    when(mockConfGetter.getConfForScope(universe, UniverseConfKeys.slowQueryRetentionDays))
        .thenReturn(7);
    when(mockQueryHelper.slowQueries(universe)).thenReturn(Json.newObject());

    slowQueriesAggregator.scheduleRunner();

    verify(mockYsqlQueryExecutor, times(2)).executeQueryInNodeShell(any(), any(), any(), eq(true));
  }

  @Test
  public void testScheduleRunnerDisablesBatchCommandLoggingWhenFlagEnabled() {
    enableAggregationForTestUniverse();
    when(mockConfGetter.getConfForScope(universe, UniverseConfKeys.slowQueryDisableCommandLogging))
        .thenReturn(true);
    when(mockConfGetter.getConfForScope(universe, UniverseConfKeys.slowQueryRetentionDays))
        .thenReturn(7);

    ObjectNode response = Json.newObject();
    ObjectNode ysql = Json.newObject();
    ObjectNode query = Json.newObject();
    query.put("dbid", "1");
    query.put("userid", "2");
    query.put("queryid", "3");
    query.put("query", "SELECT * FROM users");
    query.put("datname", "postgres");
    query.put("P99", "1.0");
    query.put("rows", "1");
    query.put("calls", "1");
    query.put("mean_time", "1.0");
    ysql.set("queries", Json.newArray().add(query));
    response.set("ysql", ysql);
    when(mockQueryHelper.slowQueries(universe)).thenReturn(response);

    slowQueriesAggregator.scheduleRunner();

    verify(mockYsqlQueryExecutor)
        .executeQueryBatchInNodeShell(any(), any(), any(), any(), eq(false));
  }
}
