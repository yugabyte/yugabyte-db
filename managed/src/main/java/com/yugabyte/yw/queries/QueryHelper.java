// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.queries;

import static play.mvc.Http.Status.SERVICE_UNAVAILABLE;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.ImmutableMap;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.CustomWsClientFactory;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.YsqlQueryExecutor;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.forms.RunQueryFormData;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.ArrayList;
import java.util.Collection;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.ThreadPoolExecutor;
import lombok.extern.slf4j.Slf4j;
import org.yb.perf_advisor.Utils;
import play.libs.Json;
import play.libs.ws.WSClient;

@Slf4j
@Singleton
public class QueryHelper {
  private static final String RESET_QUERY_SQL = "SELECT pg_stat_statements_reset()";
  private static final String STAT_RESET_TIME_SQL =
      "SELECT stats_reset from pg_stat_statements_info";
  private static final String STAT_RESET_FIELD = "stats_reset";
  private static final String SLOW_QUERY_STATS_UNLIMITED_SQL_1 =
      "SELECT s.userid::regrole as rolname, d.datname, s.queryid, LEFT(s.query, %d) as query,"
          + " s.calls, s.rows, s.local_blks_hit, s.local_blks_written, ";
  private static final String SLOW_QUERY_STATS_PG11 =
      "s.total_time, s.min_time, s.max_time, s.mean_time, s.stddev_time ";
  private static final String SLOW_QUERY_STATS_PG15 =
      "s.total_exec_time, s.min_exec_time, s.max_exec_time, s.mean_exec_time, s.stddev_exec_time,"
          + " s.min_plan_time, s.max_plan_time, s.mean_plan_time, s.total_plan_time,"
          + " stddev_plan_time ";
  private static final String SLOW_QUERY_STATS_UNLIMITED_SQL_2 =
      "FROM pg_stat_statements s JOIN pg_database d ON d.oid = s.dbid";
  private static final String HISTOGRAM_QUERY = ", s.yb_latency_histogram ";
  public static final String QUERY_STATS_SLOW_QUERIES_ORDER_BY_KEY =
      "yb.query_stats.slow_queries.order_by";
  public static final String QUERY_STATS_SLOW_QUERIES_LIMIT_KEY =
      "yb.query_stats.slow_queries.limit";
  public static final String QUERY_STATS_SLOW_QUERIES_LENGTH_KEY =
      "yb.query_stats.slow_queries.query_length";
  public static final String SET_ENABLE_NESTLOOP_OFF_STATEMENT = "Set(enable_nestloop off)";
  public static final String SET_ENABLE_NESTLOOP_OFF_KEY =
      "yb.query_stats.slow_queries.set_enable_nestloop_off";

  public static final String QUERY_STATS_TASK_QUEUE_SIZE_CONF_KEY = "yb.query_stats.queue_capacity";

  public static final String LIST_USER_DATABASES_SQL =
      "SELECT datname from pg_database where datname NOT IN "
          + "('template1', 'template0', 'system_platform')";

  /** YBDB 2.18 versions above this threshold support latency histogram. */
  public static final String MINIMUM_VERSION_THRESHOLD_LATENCY_HISTOGRAM_SUPPORT_2_18 =
      "2.18.1.0-b67";

  /** YBDB versions above this threshold support latency histogram. */
  public static final String MINIMUM_VERSION_THRESHOLD_LATENCY_HISTOGRAM_SUPPORT = "2.19.1.0-b80";

  public static final Map<String, String> PG_15_T0_PG_11_SLOW_QUERY_FIELDS_MAP =
      ImmutableMap.of(
          "total_exec_time",
          "total_time",
          "min_exec_time",
          "min_time",
          "max_exec_time",
          "max_time",
          "mean_exec_time",
          "mean_time",
          "stddev_exec_time",
          "stddev_time");

  private final RuntimeConfGetter confGetter;
  private final ExecutorService threadPool;
  private final WSClient wsClient;

  public enum QueryApi {
    YSQL,
    YCQL
  }

  private enum QueryAction {
    FETCH_LIVE_QUERIES,
    FETCH_SLOW_QUERIES,
    RESET_STATS
  }

  @Inject
  public QueryHelper(
      RuntimeConfGetter confGetter,
      PlatformExecutorFactory platformExecutorFactory,
      CustomWsClientFactory customWsClientFactory) {

    this(
        confGetter,
        createExecutor(platformExecutorFactory),
        createWsClient(customWsClientFactory, confGetter));
  }

  public QueryHelper(RuntimeConfGetter confGetter, ExecutorService threadPool, WSClient wsClient) {
    this.confGetter = confGetter;
    this.threadPool = threadPool;
    this.wsClient = wsClient;
  }

  @Inject YsqlQueryExecutor ysqlQueryExecutor;
  @Inject GFlagsValidation gFlagsValidation;

  public JsonNode liveQueries(Universe universe) {
    return queryUniverseNodes(universe, QueryAction.FETCH_LIVE_QUERIES);
  }

  public JsonNode slowQueries(Universe universe) throws IllegalArgumentException {
    return queryUniverseNodes(universe, QueryAction.FETCH_SLOW_QUERIES);
  }

  public JsonNode resetQueries(Universe universe) {
    return queryUniverseNodes(universe, QueryAction.RESET_STATS);
  }

  public static boolean supportsLatencyHistogram(Universe universe) {
    return universe.getVersions().stream()
        .allMatch(
            clusterVersion ->
                Util.compareYbVersions(
                            MINIMUM_VERSION_THRESHOLD_LATENCY_HISTOGRAM_SUPPORT,
                            clusterVersion,
                            true /* suppressFormatError */)
                        < 0
                    || (Util.compareYbVersions(
                                MINIMUM_VERSION_THRESHOLD_LATENCY_HISTOGRAM_SUPPORT_2_18,
                                clusterVersion,
                                true /* suppressFormatError */)
                            < 0
                        && Util.compareYbVersions(
                                clusterVersion, "2.19.0.0-b0", true /* suppressFormatError */)
                            < 0));
  }

  private boolean isPgVersionHigherThan11(Universe universe) {
    try {
      Optional<Integer> pgVersion =
          gFlagsValidation.getYsqlMajorVersion(
              universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion);
      return pgVersion.isPresent() && pgVersion.get() >= 15;
    } catch (Exception e) {
      return false;
    }
  }

  /** Runs provided {@link QueryAction QueryAction} on every node in the provided universe. */
  public JsonNode queryUniverseNodes(Universe universe, QueryAction queryAction)
      throws IllegalArgumentException {
    final Config config = confGetter.getUniverseConf(universe);
    if (queriesWillExceedTaskQueue(config, universe)) {
      throw new PlatformServiceException(
          SERVICE_UNAVAILABLE, "Not enough room to queue the requested tasks");
    }
    Boolean supportsLatencyHistogram = supportsLatencyHistogram(universe);
    boolean usePG15Fields = isPgVersionHigherThan11(universe);
    int ysqlErrorCount = 0;
    int ycqlErrorCount = 0;
    boolean fetchResetTimeSubmitted = false;
    String pgStatResetTime = null;
    ObjectNode responseJson = Json.newObject();
    ObjectNode ysqlJson = Json.newObject();
    ObjectNode ycqlJson = Json.newObject();
    Set<Future<JsonNode>> futures = new HashSet<>();

    ysqlJson.putArray("queries");
    ycqlJson.putArray("queries");
    for (NodeDetails node : universe.getNodes()) {
      if (node.isActive() && node.isTserver) {
        String ip = null;
        CloudSpecificInfo cloudInfo = node.cloudInfo;

        if (cloudInfo != null) {
          ip =
              node.cloudInfo.private_ip == null
                  ? node.cloudInfo.private_dns
                  : node.cloudInfo.private_ip;
        }

        if (ip == null) {
          log.error("Node {} does not have a private IP or DNS name, skipping", node.nodeName);
          continue;
        }

        Callable<JsonNode> callable;

        switch (queryAction) {
          case FETCH_SLOW_QUERIES:
            {
              // Submit query to fetch reset time.
              if (!fetchResetTimeSubmitted && usePG15Fields) {
                // We reset stats on all nodes at the same time so only query reset time from one
                // node.
                fetchResetTimeSubmitted = true;
                callable =
                    () -> {
                      RunQueryFormData ysqlQuery = new RunQueryFormData();
                      ysqlQuery.setQuery(STAT_RESET_TIME_SQL);
                      ysqlQuery.setDbName("postgres");
                      return ysqlQueryExecutor.executeQueryInNodeShell(
                          universe,
                          ysqlQuery,
                          node,
                          confGetter.getConfForScope(
                              universe, UniverseConfKeys.slowQueryTimeoutSecs));
                    };
                Future<JsonNode> future = threadPool.submit(callable);
                futures.add(future);
              }
              callable =
                  () -> {
                    RunQueryFormData ysqlQuery = new RunQueryFormData();
                    ysqlQuery.setQuery(
                        slowQuerySqlWithLimit(
                            config, universe, supportsLatencyHistogram, usePG15Fields));
                    ysqlQuery.setDbName("postgres");
                    return ysqlQueryExecutor.executeQueryInNodeShell(
                        universe,
                        ysqlQuery,
                        node,
                        confGetter.getConfForScope(
                            universe, UniverseConfKeys.slowQueryTimeoutSecs));
                  };

              Future<JsonNode> future = threadPool.submit(callable);
              futures.add(future);
              break;
            }
          case FETCH_LIVE_QUERIES:
            {
              callable =
                  new LiveQueryExecutor(
                      node.nodeName, ip, node.ysqlServerHttpPort, QueryApi.YSQL, this.wsClient);

              Future<JsonNode> future = threadPool.submit(callable);
              futures.add(future);

              callable =
                  new LiveQueryExecutor(
                      node.nodeName, ip, node.yqlServerHttpPort, QueryApi.YCQL, this.wsClient);
              future = threadPool.submit(callable);
              futures.add(future);
              break;
            }
          case RESET_STATS:
            {
              callable =
                  () -> {
                    RunQueryFormData ysqlQuery = new RunQueryFormData();
                    ysqlQuery.setQuery(RESET_QUERY_SQL);
                    ysqlQuery.setDbName("postgres");
                    return ysqlQueryExecutor.executeQueryInNodeShell(universe, ysqlQuery, node);
                  };
              Future<JsonNode> future = threadPool.submit(callable);
              futures.add(future);
              break;
            }
          default:
            throw new RuntimeException("Unexpected QueryType: " + queryAction);
        }
      }
    }

    if (futures.isEmpty()) {
      throw new IllegalStateException(
          "None of the nodes are accessible by either private IP or DNS");
    }

    try {
      Map<String, JsonNode> queryMap = new HashMap<>();
      for (Future<JsonNode> future : futures) {
        JsonNode response = future.get();
        if (response.has("error")) {
          String errorMessage = response.get("error").toString();
          // If Login Credentials are incorrect we receive
          // {"error":"FATAL: password authentication failed for user \"<username in
          // header>\""}
          if (errorMessage.startsWith("\"FATAL: password authentication failed")) {
            throw new IllegalArgumentException("Incorrect Username or Password");
          }
          if (response.has("type")) {
            String type = response.get("type").asText();
            if ("ysql".equals(type)) {
              ysqlErrorCount++;
            } else if ("ycql".equals(type)) {
              ycqlErrorCount++;
            }
          }
        } else {
          if (queryAction == QueryAction.FETCH_SLOW_QUERIES) {
            // TODO: PLAT-3986 Sort and limit the merged data
            JsonNode ysqlResponse = response.get("result");
            for (JsonNode queryObject : ysqlResponse) {
              if (queryObject.has(STAT_RESET_FIELD)) {
                pgStatResetTime = queryObject.get(STAT_RESET_FIELD).asText();
                continue;
              }
              if (usePG15Fields) {
                renamePG15Fields(queryObject);
              }
              String queryID = queryObject.get("queryid").asText();
              String queryStatement = queryObject.get("query").asText();
              if (!isExcluded(queryStatement, config, supportsLatencyHistogram)) {
                if (queryMap.containsKey(queryID)) {
                  // Calculate new query stats
                  ObjectNode previousQueryObj = (ObjectNode) queryMap.get(queryID);
                  // Defining values to reuse
                  double X_a = previousQueryObj.get("mean_time").asDouble();
                  double X_b = queryObject.get("mean_time").asDouble();
                  int n_a = previousQueryObj.get("calls").asInt();
                  int n_b = queryObject.get("calls").asInt();
                  double S_a = previousQueryObj.get("stddev_time").asDouble();
                  double S_b = queryObject.get("stddev_time").asDouble();

                  double totalTime =
                      previousQueryObj.get("total_time").asDouble()
                          + queryObject.get("total_time").asDouble();
                  int totalCalls = n_a + n_b;
                  int rows = previousQueryObj.get("rows").asInt() + queryObject.get("rows").asInt();
                  double minTime =
                      Math.min(
                          previousQueryObj.get("min_time").asDouble(),
                          queryObject.get("min_time").asDouble());
                  double maxTime =
                      Math.max(
                          previousQueryObj.get("max_time").asDouble(),
                          queryObject.get("max_time").asDouble());
                  int tmpTables =
                      previousQueryObj.get("local_blks_written").asInt()
                          + queryObject.get("local_blks_written").asInt();
                  /*
                   * Formula to calculate std dev of two samples: Let mean, std dev, and size of
                   * sample A be X_a, S_a, n_a respectively; and mean, std dev, and size of sample B
                   * be X_b, S_b, n_b respectively. Then mean of combined sample X is given by
                   *              n_a X_a + n_b X_b
                   *          X = -----------------
                   *                  n_a + n_b
                   *
                   * The std dev of combined sample S is
                   *                    n_a ( S_a^2 + (X_a - X)^2) + n_b(S_b^2 + (X_b - X)^2)
                   *          S = sqrt( -----------------------------------------------------  )
                   *                                  n_a + n_b
                   */
                  double averageTime = (n_a * X_a + n_b * X_b) / totalCalls;
                  double stdDevTime =
                      Math.sqrt(
                          (n_a * (Math.pow(S_a, 2) + Math.pow(X_a - averageTime, 2))
                                  + n_b * (Math.pow(S_b, 2) + Math.pow(X_b - averageTime, 2)))
                              / totalCalls);

                  previousQueryObj.put("total_time", totalTime);
                  previousQueryObj.put("calls", totalCalls);
                  previousQueryObj.put("rows", rows);
                  previousQueryObj.put("min_time", minTime);
                  previousQueryObj.put("max_time", maxTime);
                  previousQueryObj.put("mean_time", averageTime);
                  previousQueryObj.put("local_blks_written", tmpTables);
                  previousQueryObj.put("stddev_time", stdDevTime);

                  if (supportsLatencyHistogram) {
                    Histogram histogram_a =
                        new Histogram(
                            Json.fromJson(
                                previousQueryObj.get("yb_latency_histogram"), List.class));
                    Histogram histogram_b =
                        new Histogram(
                            Json.fromJson(queryObject.get("yb_latency_histogram"), List.class));
                    histogram_a.merge(histogram_b);
                    previousQueryObj.set("yb_latency_histogram", histogram_a.getArrayNode());
                  }

                  if (usePG15Fields) {
                    double totalPlanTime =
                        previousQueryObj.get("total_plan_time").asDouble()
                            + queryObject.get("total_plan_time").asDouble();
                    double minPlanTime =
                        Math.min(
                            previousQueryObj.get("min_plan_time").asDouble(),
                            queryObject.get("min_plan_time").asDouble());
                    double maxPlanTime =
                        Math.max(
                            previousQueryObj.get("max_plan_time").asDouble(),
                            queryObject.get("max_plan_time").asDouble());
                    double meanPlanTime_a = previousQueryObj.get("mean_plan_time").asDouble();
                    double meanPlanTime_b = queryObject.get("mean_plan_time").asDouble();
                    double averagePlanTime =
                        (n_a * meanPlanTime_a + n_b * meanPlanTime_b) / totalCalls;
                    double stdPlanTime_a = previousQueryObj.get("stddev_plan_time").asDouble();
                    double stdPlanTime_b = queryObject.get("stddev_plan_time").asDouble();
                    // Using the same stddev formula mentioned above.
                    double stdPlanTime =
                        Math.sqrt(
                            (n_a
                                        * (Math.pow(stdPlanTime_a, 2)
                                            + Math.pow(meanPlanTime_a - averagePlanTime, 2))
                                    + n_b
                                        * (Math.pow(stdPlanTime_b, 2)
                                            + Math.pow(meanPlanTime_b - averagePlanTime, 2)))
                                / totalCalls);
                    previousQueryObj.put("min_plan_time", minPlanTime);
                    previousQueryObj.put("max_plan_time", maxPlanTime);
                    previousQueryObj.put("total_plan_time", totalPlanTime);
                    previousQueryObj.put("mean_plan_time", averagePlanTime);
                    previousQueryObj.put("stddev_plan_time", stdPlanTime);
                  }

                } else {
                  queryMap.put(queryID, queryObject);
                }
              }
            }

            if (supportsLatencyHistogram) {
              queryMap.forEach(
                  (queryId, queryObj) -> {
                    ObjectNode objNode = (ObjectNode) queryObj;
                    List<Map<String, Integer>> mapList = new ArrayList<>();
                    mapList.addAll(Json.fromJson(objNode.get("yb_latency_histogram"), List.class));
                    Histogram histogram = new Histogram(mapList);
                    objNode.put("P25", histogram.getPercentile(25));
                    objNode.put("P50", histogram.getPercentile(50));
                    objNode.put("P90", histogram.getPercentile(90));
                    objNode.put("P95", histogram.getPercentile(95));
                    objNode.put("P99", histogram.getPercentile(99));
                  });
            }

            ArrayNode queryArr = Json.newArray();
            ysqlJson.set("queries", queryArr.addAll(queryMap.values()));
          } else {
            if (response.has("ysql")) {
              ArrayNode arr = (ArrayNode) ysqlJson.get("queries");
              concatArrayNodes(arr, response.get("ysql"));
            } else if (response.has("ycql")) {
              ArrayNode arr = (ArrayNode) ycqlJson.get("queries");
              concatArrayNodes(arr, response.get("ycql"));
            }
          }
        }
      }
    } catch (InterruptedException e) {
      log.error("Error fetching live query data", e);
    } catch (ExecutionException e) {
      log.error("Error fetching live query data", e.getCause());
    }

    ysqlJson.put("errorCount", ysqlErrorCount);
    if (pgStatResetTime != null) {
      ysqlJson.put(STAT_RESET_FIELD, pgStatResetTime);
    }
    ycqlJson.put("errorCount", ycqlErrorCount);
    responseJson.set("ysql", ysqlJson);
    responseJson.set("ycql", ycqlJson);

    return responseJson;
  }

  private static ExecutorService createExecutor(PlatformExecutorFactory platformExecutorFactory) {
    return platformExecutorFactory.createExecutor("query_stats", Executors.defaultThreadFactory());
  }

  private static WSClient createWsClient(
      CustomWsClientFactory customWsClientFactory, RuntimeConfGetter confGetter) {
    return customWsClientFactory.forCustomConfig(
        confGetter.getGlobalConf().getValue(Util.LIVE_QUERY_TIMEOUTS));
  }

  /** Check if running a query per node will exceed the remaining task queue room */
  private boolean queriesWillExceedTaskQueue(Config config, Universe universe) {
    Collection<NodeDetails> universeNodes = universe.getNodes();
    ThreadPoolExecutor executor = (ThreadPoolExecutor) threadPool;
    int taskQueueSize = config.getInt(QUERY_STATS_TASK_QUEUE_SIZE_CONF_KEY);
    int unallocatedTaskQueueSpots = taskQueueSize - executor.getQueue().size();
    return universeNodes.size() > unallocatedTaskQueueSpots;
  }

  @VisibleForTesting
  public String slowQuerySqlWithLimit(
      Config config, Universe universe, Boolean supportsLatencyHistogram, boolean usePG15Fields) {
    String orderBy = config.getString(QUERY_STATS_SLOW_QUERIES_ORDER_BY_KEY);
    orderBy = (usePG15Fields ? orderBy.replace("_time", "_exec_time") : orderBy);
    int limit = config.getInt(QUERY_STATS_SLOW_QUERIES_LIMIT_KEY);
    int length = config.getInt(QUERY_STATS_SLOW_QUERIES_LENGTH_KEY);
    String setEnableNestloopOffStatementOptional =
        confGetter.getConfForScope(universe, UniverseConfKeys.setEnableNestloopOff)
            ? SET_ENABLE_NESTLOOP_OFF_STATEMENT
            : "";
    return String.format(
        "/*+ Leading((d pg_stat_statements)) %s */ %s ORDER BY s.%s DESC LIMIT %d",
        setEnableNestloopOffStatementOptional,
        String.format(SLOW_QUERY_STATS_UNLIMITED_SQL_1, length)
            + (usePG15Fields ? SLOW_QUERY_STATS_PG15 : SLOW_QUERY_STATS_PG11)
            + (supportsLatencyHistogram ? HISTOGRAM_QUERY : "")
            + SLOW_QUERY_STATS_UNLIMITED_SQL_2,
        orderBy,
        limit);
  }

  public JsonNode listDatabaseNames(Universe universe) {
    NodeDetails randomTServer = CommonUtils.getARandomLiveTServer(universe);
    RunQueryFormData ysqlQuery = new RunQueryFormData();
    ysqlQuery.setQuery(LIST_USER_DATABASES_SQL);
    ysqlQuery.setDbName("postgres");
    return ysqlQueryExecutor.executeQueryInNodeShell(universe, ysqlQuery, randomTServer);
  }

  private boolean isExcluded(
      String queryStatement, Config config, Boolean supportsLatencyHistogram) {
    final List<String> excludedQueries = config.getStringList("yb.query_stats.excluded_queries");
    final List<String> excludedPerfAdvisorQueries = Utils.getScriptQueryStatements();
    return excludedQueries.contains(queryStatement)
        || queryStatement.contains(SLOW_QUERY_STATS_UNLIMITED_SQL_2)
        || queryStatement.contains(LIST_USER_DATABASES_SQL)
        || excludedPerfAdvisorQueries.contains(queryStatement);
  }

  private void concatArrayNodes(ArrayNode destination, JsonNode source) {
    for (JsonNode node : source) {
      destination.add(node);
    }
  }

  /**
   * Renames fields in the given JSON node based on the mapping from PostgreSQL 15 to PostgreSQL 11
   * slow query fields. For each entry in the mapping, if the JSON node contains the new field name,
   * it will be renamed to the old field name.
   *
   * @param node the JSON node whose fields are to be renamed
   */
  private void renamePG15Fields(JsonNode node) {
    for (Map.Entry<String, String> entry : PG_15_T0_PG_11_SLOW_QUERY_FIELDS_MAP.entrySet()) {
      String newField = entry.getKey();
      String oldField = entry.getValue();
      if (node.has(newField)) {
        ((ObjectNode) node).set(oldField, node.get(newField));
        ((ObjectNode) node).remove(newField);
      }
    }
  }
}
