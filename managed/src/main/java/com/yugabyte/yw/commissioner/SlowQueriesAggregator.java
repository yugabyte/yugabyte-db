package com.yugabyte.yw.commissioner;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.YsqlQueryExecutor;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.RunQueryFormData;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.queries.QueryHelper;
import java.time.Duration;
import java.time.LocalDateTime;
import java.time.format.DateTimeFormatter;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Singleton
@Slf4j
public class SlowQueriesAggregator {

  private final QueryHelper queryHelper;
  private final RuntimeConfGetter confGetter;
  private final YsqlQueryExecutor queryExecutor;
  private final PlatformScheduler scheduler;
  private final int SLOW_QUERY_AGGREGATION_INTERVAL = 1;
  private final String CREATE_TABLE_QUERY =
      "CREATE TABLE IF NOT EXISTS %s ("
          + "  timestamp   TIMESTAMP,"
          + "  dbid        BIGINT,"
          + "  userid      OID,"
          + "  queryid     BIGINT,"
          + "  query       TEXT,"
          + "  database    TEXT,"
          + "  p99         FLOAT,"
          + "  rows        BIGINT,"
          + "  calls       BIGINT,"
          + "  mean_time   FLOAT,"
          + "  PRIMARY KEY (timestamp, queryid, dbid, userid)"
          + ");";

  private final String DELETE_DATA_QUERY =
      "DELETE FROM %s WHERE timestamp < now() - interval '%d days';";
  private final String INSERT_DATA_QUERY =
      "INSERT INTO %s VALUES ('%s', %s, %s, %s, $$%s$$, '%s', %s, %s, %s, %s);";

  @Inject
  SlowQueriesAggregator(
      QueryHelper queryHelper,
      YsqlQueryExecutor ysqlQueryExecutor,
      RuntimeConfGetter confGetter,
      PlatformScheduler platformScheduler) {
    this.queryHelper = queryHelper;
    this.queryExecutor = ysqlQueryExecutor;
    this.scheduler = platformScheduler;
    this.confGetter = confGetter;
  }

  public void start() {
    // Need not schedule on standby YBA.
    if (HighAvailabilityConfig.isFollower()) {
      return;
    }
    scheduler.schedule(
        getClass().getSimpleName(),
        Duration.ZERO,
        Duration.ofHours(SLOW_QUERY_AGGREGATION_INTERVAL),
        this::scheduleRunner);
  }

  void scheduleRunner() {
    List<UUID> universeIds = Universe.find.query().select("universeUUID").findSingleAttributeList();
    for (var uuid : universeIds) {
      var maybeUniverse = Universe.maybeGet(uuid);
      if (maybeUniverse.isEmpty()) {
        continue;
      }
      var universe = maybeUniverse.get();
      if (confGetter.getConfForScope(universe, UniverseConfKeys.slowQueryDisableAggregation)) {
        continue;
      }
      try {
        var node = CommonUtils.getServerToRunYsqlQuery(universe);
        log.info("Saving slow queries data for universe {}", universe.getName());
        RunQueryFormData runQueryFormData = new RunQueryFormData();
        runQueryFormData.setDbName(Util.SYSTEM_PLATFORM_DB);
        runQueryFormData.setQuery(
            String.format(CREATE_TABLE_QUERY, Util.SLOW_QUERIES_AGGREGATATION_TABLE));

        // Run idempotent create table query.
        queryExecutor.executeQueryInNodeShell(universe, runQueryFormData, node);

        // Delete old data.
        runQueryFormData.setQuery(
            String.format(
                DELETE_DATA_QUERY,
                Util.SLOW_QUERIES_AGGREGATATION_TABLE,
                confGetter.getConfForScope(universe, UniverseConfKeys.slowQueryRetentionDays)));
        queryExecutor.executeQueryInNodeShell(universe, runQueryFormData, node);

        // Fetch slow queries and insert in the table.
        JsonNode newData = queryHelper.slowQueries(universe);
        if (newData.has("ysql") && newData.get("ysql").has("queries")) {
          var queriesData = newData.get("ysql").get("queries");
          var insertQueries = new ArrayList<String>();
          var formatter = DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss");
          var timestamp = LocalDateTime.now().format(formatter);
          for (var data : queriesData) {
            insertQueries.add(
                String.format(
                    INSERT_DATA_QUERY,
                    Util.SLOW_QUERIES_AGGREGATATION_TABLE,
                    timestamp,
                    data.get("dbid").asText(),
                    data.get("userid").asText(),
                    data.get("queryid").asText(),
                    data.get("query").asText(),
                    data.get("datname").asText(),
                    data.get("P99").asText(),
                    data.get("rows").asText(),
                    data.get("calls").asText(),
                    data.get("mean_time").asText()));
          }
          queryExecutor.executeQueryBatchInNodeShell(
              universe, Util.SYSTEM_PLATFORM_DB, insertQueries, node);
        }
      } catch (Exception e) {
        log.error(
            "Error while saving slow queries data for universe {} with error: {}",
            universe.getName(),
            e.getMessage());
      }
    }
  }
}
