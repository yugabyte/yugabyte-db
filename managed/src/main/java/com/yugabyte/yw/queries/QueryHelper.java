// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.queries;

import static play.mvc.Http.Status.SERVICE_UNAVAILABLE;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.CustomWsClientFactory;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.YsqlQueryExecutor;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.RunQueryFormData;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Collection;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.ThreadPoolExecutor;
import lombok.extern.slf4j.Slf4j;
import play.Configuration;
import play.libs.Json;
import play.libs.ws.WSClient;

@Slf4j
@Singleton
public class QueryHelper {
  private static final String RESET_QUERY_SQL = "SELECT pg_stat_statements_reset()";
  private static final String SLOW_QUERY_STATS_UNLIMITED_SQL =
      "SELECT a.rolname, t.datname, t.queryid, "
          + "t.query, t.calls, t.total_time, t.rows, t.min_time, t.max_time, t.mean_time, t.stddev_time, "
          + "t.local_blks_hit, t.local_blks_written FROM pg_authid a JOIN (SELECT * FROM "
          + "pg_stat_statements s JOIN pg_database d ON s.dbid = d.oid) t ON a.oid = t.userid";
  public static final String QUERY_STATS_SLOW_QUERIES_ORDER_BY_KEY =
      "yb.query_stats.slow_queries.order_by";
  public static final String QUERY_STATS_SLOW_QUERIES_LIMIT_KEY =
      "yb.query_stats.slow_queries.limit";

  public static final String QUERY_STATS_TASK_QUEUE_SIZE_CONF_KEY = "yb.query_stats.queue_capacity";

  private final RuntimeConfigFactory runtimeConfigFactory;
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
      RuntimeConfigFactory runtimeConfigFactory,
      PlatformExecutorFactory platformExecutorFactory,
      CustomWsClientFactory customWsClientFactory) {

    this(
        runtimeConfigFactory,
        createExecutor(platformExecutorFactory),
        createWsClient(customWsClientFactory, runtimeConfigFactory));
  }

  public QueryHelper(
      RuntimeConfigFactory runtimeConfigFactory, ExecutorService threadPool, WSClient wsClient) {
    this.runtimeConfigFactory = runtimeConfigFactory;
    this.threadPool = threadPool;
    this.wsClient = wsClient;
  }

  @Inject YsqlQueryExecutor ysqlQueryExecutor;

  public JsonNode liveQueries(Universe universe) {
    return queryUniverseNodes(universe, QueryAction.FETCH_LIVE_QUERIES);
  }

  public JsonNode slowQueries(Universe universe) throws IllegalArgumentException {
    return queryUniverseNodes(universe, QueryAction.FETCH_SLOW_QUERIES);
  }

  public JsonNode resetQueries(Universe universe) {
    return queryUniverseNodes(universe, QueryAction.RESET_STATS);
  }

  /** Runs provided {@link QueryAction QueryAction} on every node in the provided universe. */
  public JsonNode queryUniverseNodes(Universe universe, QueryAction queryAction)
      throws IllegalArgumentException {
    final Config config = runtimeConfigFactory.forUniverse(universe);
    if (queriesWillExceedTaskQueue(config, universe)) {
      throw new PlatformServiceException(
          SERVICE_UNAVAILABLE, "Not enough room to queue the requested tasks");
    }
    int ysqlErrorCount = 0;
    int ycqlErrorCount = 0;
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
              callable =
                  () -> {
                    RunQueryFormData ysqlQuery = new RunQueryFormData();
                    ysqlQuery.query = slowQuerySqlWithLimit(config);
                    ysqlQuery.db_name = "postgres";
                    return ysqlQueryExecutor.executeQueryInNodeShell(universe, ysqlQuery, node);
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
                    ysqlQuery.query = RESET_QUERY_SQL;
                    ysqlQuery.db_name = "postgres";
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
            // TODO: PLAT-3977 group by queryid instead of query
            // TODO: PLAT-3986 Sort and limit the merged data
            JsonNode ysqlResponse = response.get("result");
            for (JsonNode queryObject : ysqlResponse) {
              String queryStatement = queryObject.get("query").asText();
              if (!isExcluded(queryStatement, config)) {
                if (queryMap.containsKey(queryStatement)) {
                  // Calculate new query stats
                  ObjectNode previousQueryObj = (ObjectNode) queryMap.get(queryStatement);
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
                } else {
                  queryMap.put(queryStatement, queryObject);
                }
              }
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
    ycqlJson.put("errorCount", ycqlErrorCount);
    responseJson.set("ysql", ysqlJson);
    responseJson.set("ycql", ycqlJson);

    return responseJson;
  }

  private static ExecutorService createExecutor(PlatformExecutorFactory platformExecutorFactory) {
    return platformExecutorFactory.createExecutor("query_stats", Executors.defaultThreadFactory());
  }

  private static WSClient createWsClient(
      CustomWsClientFactory customWsClientFactory, RuntimeConfigFactory runtimeConfigFactory) {
    return customWsClientFactory.forCustomConfig(
        runtimeConfigFactory.globalRuntimeConf().getValue(Util.LIVE_QUERY_TIMEOUTS));
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
  public String slowQuerySqlWithLimit(Config config) {
    String orderBy = config.getString(QUERY_STATS_SLOW_QUERIES_ORDER_BY_KEY);
    int limit = config.getInt(QUERY_STATS_SLOW_QUERIES_LIMIT_KEY);
    return String.format(
        "%s ORDER BY t.%s DESC LIMIT %d", SLOW_QUERY_STATS_UNLIMITED_SQL, orderBy, limit);
  }

  private boolean isExcluded(String queryStatement, Config config) {
    final List<String> excludedQueries = config.getStringList("yb.query_stats.excluded_queries");
    return excludedQueries.contains(queryStatement)
        || queryStatement.startsWith(SLOW_QUERY_STATS_UNLIMITED_SQL);
  }

  private void concatArrayNodes(ArrayNode destination, JsonNode source) {
    for (JsonNode node : source) {
      destination.add(node);
    }
  }
}
