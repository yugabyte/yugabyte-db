// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.queries;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.YsqlQueryExecutor;
import com.yugabyte.yw.forms.RunQueryFormData;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;

import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Slf4j
@Singleton
public class QueryHelper {
  public static final Integer QUERY_EXECUTOR_THREAD_POOL = 5;

  private static final String SLOW_QUERY_STATS_SQL =
      "SELECT a.rolname, t.datname, t.queryid, "
          + "t.query, t.calls, t.total_time, t.rows, t.min_time, t.max_time, t.mean_time, t.stddev_time, "
          + "t.local_blks_hit, t.local_blks_written FROM pg_authid a JOIN (SELECT * FROM "
          + "pg_stat_statements s JOIN pg_database d ON s.dbid = d.oid) t ON a.oid = t.userid";
  private static final Set<String> EXCLUDED_QUERY_STATEMENTS =
      new HashSet<>(Arrays.asList("SET extra_float_digits = 3", SLOW_QUERY_STATS_SQL));

  public enum QueryApi {
    YSQL,
    YCQL
  }

  @Inject YsqlQueryExecutor ysqlQueryExecutor;

  public JsonNode liveQueries(Universe universe) {
    return query(universe, false, null, null);
  }

  public JsonNode slowQueries(Universe universe, String username, String password)
      throws IllegalArgumentException {
    return query(universe, true, username, password);
  }

  public JsonNode resetQueries(Universe universe) {
    RunQueryFormData ysqlQuery = new RunQueryFormData();
    ysqlQuery.query = "SELECT pg_stat_statements_reset()";
    ysqlQuery.db_name = "postgres";
    return ysqlQueryExecutor.executeQuery(universe, ysqlQuery);
  }

  public JsonNode query(
      Universe universe, boolean fetchSlowQueries, String username, String password)
      throws IllegalArgumentException {
    ExecutorService threadPool = Executors.newFixedThreadPool(QUERY_EXECUTOR_THREAD_POOL);
    Set<Future<JsonNode>> futures = new HashSet<>();
    int ysqlErrorCount = 0;
    int ycqlErrorCount = 0;
    ObjectNode responseJson = Json.newObject();
    ObjectNode ysqlJson = Json.newObject();
    ysqlJson.putArray("queries");
    ObjectNode ycqlJson = Json.newObject();
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

        if (fetchSlowQueries) {
          callable =
              new SlowQueryExecutor(
                  ip, node.ysqlServerRpcPort, universe, SLOW_QUERY_STATS_SQL, username, password);
          Future<JsonNode> future = threadPool.submit(callable);
          futures.add(future);
        } else {
          callable =
              new LiveQueryExecutor(node.nodeName, ip, node.ysqlServerHttpPort, QueryApi.YSQL);

          Future<JsonNode> future = threadPool.submit(callable);
          futures.add(future);

          callable =
              new LiveQueryExecutor(node.nodeName, ip, node.yqlServerHttpPort, QueryApi.YCQL);
          future = threadPool.submit(callable);
          futures.add(future);
        }
      }
    }

    if (futures.isEmpty()) {
      throw new IllegalStateException(
          "None of the nodes are accessible by either private IP or DNS");
    }

    threadPool.shutdown();

    try {
      Map<String, JsonNode> queryMap = new HashMap<>();
      for (Future<JsonNode> future : futures) {
        JsonNode response = future.get();
        if (response.has("error")) {
          String errorMessage = response.get("error").toString();
          // If Login Credentials are incorrect we receive
          // {"error":"FATAL: password authentication failed for user \"<username in header>\""}
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
          if (fetchSlowQueries) {
            JsonNode ysqlResponse = response.get("result");
            for (JsonNode queryObject : ysqlResponse) {
              String queryStatement = queryObject.get("query").asText();
              if (!EXCLUDED_QUERY_STATEMENTS.contains(queryStatement)) {
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
                   * be X_b, S_b, n_b respectively. Then mean of combined sample X is given by n_a
                   * X_a + n_b X_b X = ----------------- n_a + n_b
                   *
                   * <p>The std dev of combined sample S is n_a ( S_a^2 + (X_a - X)^2) + n_b(S_b^2 +
                   * (X_b - X)^2) S = ----------------------------------------------------- n_a +
                   * n_b
                   */
                  double averageTime = (n_a * X_a + n_b * X_b) / totalCalls;
                  double stdDevTime =
                      (n_a * (Math.pow(S_a, 2) + Math.pow(X_a - averageTime, 2))
                              + n_b * (Math.pow(S_b, 2) + Math.pow(X_b - averageTime, 2)))
                          / totalCalls;
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

  private void concatArrayNodes(ArrayNode destination, JsonNode source) {
    for (JsonNode node : source) {
      destination.add(node);
    }
  }
}
