// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import ch.qos.logback.classic.Level;
import ch.qos.logback.classic.Logger;
import ch.qos.logback.classic.spi.ILoggingEvent;
import ch.qos.logback.core.read.ListAppender;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.YsqlQueryExecutor;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.RunQueryFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.queries.QueryHelper;
import io.ebean.DB;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.List;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Captor;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;
import org.slf4j.LoggerFactory;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class SlowQueriesAggregatorTest extends FakeDBApplication {

  @Mock QueryHelper mockQueryHelper;
  @Mock YsqlQueryExecutor mockYsqlQueryExecutor;
  @Mock RuntimeConfGetter mockConfGetter;
  @Mock PlatformScheduler mockPlatformScheduler;
  @Captor ArgumentCaptor<List<String>> insertsCaptor;

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

    when(mockQueryHelper.slowQueries(universe))
        .thenReturn(slowQueriesResponse("SELECT * FROM users", "postgres"));

    slowQueriesAggregator.scheduleRunner();

    verify(mockYsqlQueryExecutor)
        .executeQueryBatchInNodeShell(any(), any(), any(), any(), eq(false));
  }

  @Test
  public void testStoredQueryTextCannotEndItsLiteral() throws SQLException {
    enableAggregationForTestUniverse();
    when(mockConfGetter.getConfForScope(universe, UniverseConfKeys.slowQueryDisableCommandLogging))
        .thenReturn(true);
    when(mockConfGetter.getConfForScope(universe, UniverseConfKeys.slowQueryRetentionDays))
        .thenReturn(7);
    // The same RunQueryFormData is reused for each query, so record the query at each call.
    List<String> setupQueries = new ArrayList<>();
    when(mockYsqlQueryExecutor.executeQueryInNodeShell(any(), any(), any(), anyBoolean()))
        .thenAnswer(
            invocation -> {
              setupQueries.add(invocation.<RunQueryFormData>getArgument(1).getQuery());
              return Json.newObject();
            });
    // pg_stat_statements keeps comments, so any database user can put this text in a slow query.
    String queryText =
        "SELECT 1 /* $$, 'x', 0, 0, 0, 0); INSERT INTO slow_queries_data VALUES (now(), 0, 0, 4,"
            + " 'INJ' || 'ECTED', 'x', 0, 0, 0, 0); SELECT ($$ */ WHERE 'it''s' <> E'\\\\'";
    String databaseName = "db', 0, 0, 0, 0); SELECT ('";
    when(mockQueryHelper.slowQueries(universe))
        .thenReturn(slowQueriesResponse(queryText, databaseName));

    slowQueriesAggregator.scheduleRunner();

    verify(mockYsqlQueryExecutor)
        .executeQueryBatchInNodeShell(any(), any(), insertsCaptor.capture(), any(), eq(false));
    try (Connection connection = DB.getDefault().dataSource().getConnection()) {
      connection.setAutoCommit(false);
      try (Statement statement = connection.createStatement()) {
        statement.execute(setupQueries.get(0));
        for (String insert : insertsCaptor.getValue()) {
          statement.execute(insert);
        }
        try (ResultSet rows =
            statement.executeQuery("SELECT query, database FROM slow_queries_data")) {
          assertTrue(rows.next());
          assertEquals(queryText, rows.getString("query"));
          assertEquals(databaseName, rows.getString("database"));
          assertFalse(rows.next());
        }
      } finally {
        connection.rollback();
      }
    }
  }

  @Test
  public void testFailedInsertIsLoggedWithoutQueryText() {
    assertEquals(
        "Failed to save slow queries data for universe slow-queries-universe: <YSQL query>",
        loggedInsertFailure(true));
  }

  @Test
  public void testFailedInsertIsLoggedWithErrorWhenLoggingEnabled() {
    assertTrue(loggedInsertFailure(false).contains("LINE 1: INSERT INTO slow_queries_data"));
  }

  private String loggedInsertFailure(boolean disableCommandLogging) {
    enableAggregationForTestUniverse();
    when(mockConfGetter.getConfForScope(universe, UniverseConfKeys.slowQueryDisableCommandLogging))
        .thenReturn(disableCommandLogging);
    when(mockConfGetter.getConfForScope(universe, UniverseConfKeys.slowQueryRetentionDays))
        .thenReturn(7);
    when(mockQueryHelper.slowQueries(universe))
        .thenReturn(slowQueriesResponse("SELECT * FROM users", "postgres"));
    ObjectNode error = Json.newObject();
    error.put(
        "error", "ERROR:  syntax error\nLINE 1: INSERT INTO slow_queries_data VALUES ('2026-...");
    when(mockYsqlQueryExecutor.executeQueryBatchInNodeShell(
            any(), any(), any(), any(), anyBoolean()))
        .thenReturn(error);
    Logger logger = (Logger) LoggerFactory.getLogger(SlowQueriesAggregator.class);
    ListAppender<ILoggingEvent> appender = new ListAppender<>();
    appender.start();
    logger.addAppender(appender);
    try {
      slowQueriesAggregator.scheduleRunner();
    } finally {
      logger.detachAppender(appender);
    }
    return appender.list.stream()
        .filter(event -> Level.ERROR.equals(event.getLevel()))
        .map(ILoggingEvent::getFormattedMessage)
        .collect(Collectors.joining("\n"));
  }

  private static ObjectNode slowQueriesResponse(String queryText, String databaseName) {
    ObjectNode query = Json.newObject();
    query.put("dbid", "1");
    query.put("userid", "2");
    query.put("queryid", "3");
    query.put("query", queryText);
    query.put("datname", databaseName);
    query.put("P99", "1.0");
    query.put("rows", "1");
    query.put("calls", "1");
    query.put("mean_time", "1.0");
    ObjectNode ysql = Json.newObject();
    ysql.set("queries", Json.newArray().add(query));
    ObjectNode response = Json.newObject();
    response.set("ysql", ysql);
    return response;
  }
}
