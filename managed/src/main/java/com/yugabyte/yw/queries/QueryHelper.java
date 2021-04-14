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
import com.yugabyte.yw.models.helpers.NodeDetails;
import org.joda.time.DateTime;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.Configuration;
import play.libs.Json;

import java.io.IOException;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Arrays;
import java.util.ArrayList;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;

@Singleton
public class QueryHelper {
  public static final Logger LOG = LoggerFactory.getLogger(QueryHelper.class);
  public static final Integer QUERY_EXECUTOR_THREAD_POOL = 5;

  private static final String SLOW_QUERY_STATS_SQL = "SELECT a.rolname, t.datname, t.queryid, " +
    "t.query, t.calls, t.total_time, t.rows, t.min_time, t.max_time, t.mean_time, t.stddev_time, " +
    "t.local_blks_hit, t.local_blks_written FROM pg_authid a JOIN (SELECT * FROM " +
    "pg_stat_statements s JOIN pg_database d ON s.dbid = d.oid) t ON a.oid = t.userid";
  private static final Set<String> EXCLUDED_QUERY_STATEMENTS = new HashSet<>(Arrays.asList(
    "SET extra_float_digits = 3",
    SLOW_QUERY_STATS_SQL
  ));

  public enum QueryApi {
    YSQL,
    YCQL
  }

  @Inject
  YsqlQueryExecutor ysqlQueryExecutor;

  public JsonNode liveQueries(Universe universe) {
    return query(universe, false);
  }

  public JsonNode slowQueries(Universe universe) {
    RunQueryFormData ysqlQuery = new RunQueryFormData();
    ysqlQuery.query = SLOW_QUERY_STATS_SQL;
    ysqlQuery.db_name = "postgres";
    JsonNode ysqlResponse = ysqlQueryExecutor.executeQuery(universe, ysqlQuery).get("result");
    ArrayNode queries = Json.newArray();
    if (ysqlResponse.isArray()) {
      for (JsonNode queryObject : ysqlResponse) {
        if (!EXCLUDED_QUERY_STATEMENTS.contains(queryObject.get("query").asText())) {
          queries.add(queryObject);
        }
      }
    }
    ObjectNode ysqlJson = Json.newObject();
    ysqlJson.set("queries", queries);
    ObjectNode responseJson = Json.newObject();
    responseJson.set("ysql", ysqlJson);
    return responseJson;
  }

  public JsonNode resetQueries(Universe universe) {
    RunQueryFormData ysqlQuery = new RunQueryFormData();
    ysqlQuery.query = "SELECT pg_stat_statements_reset()";
    ysqlQuery.db_name = "postgres";
    return ysqlQueryExecutor.executeQuery(universe, ysqlQuery);
  }

  public JsonNode query(Universe universe, boolean fetchSlowQueries) {
    ExecutorService threadPool = Executors.newFixedThreadPool(QUERY_EXECUTOR_THREAD_POOL);
    Set<Future<JsonNode>> futures = new HashSet<Future<JsonNode>>();
    ObjectNode responseJson = Json.newObject();
    ObjectNode ysqlJson = Json.newObject();
    ysqlJson.put("errorCount", 0);
    ysqlJson.putArray("queries");
    ObjectNode ycqlJson = Json.newObject();
    ycqlJson.put("errorCount", 0);
    ycqlJson.putArray("queries");
    for (NodeDetails node : universe.getNodes()) {
      if (node.isActive() && node.isTserver) {
        String ip = node.cloudInfo.private_ip == null ?
          node.cloudInfo.private_dns :
          node.cloudInfo.private_ip;
        Callable<JsonNode> callable;

        if (fetchSlowQueries) {
          callable = new SlowQueryExecutor(
            node.nodeName,
            ip,
            node.ysqlServerHttpPort
          );
          Future<JsonNode> future = threadPool.submit(callable);
          futures.add(future);
        } else {
          callable = new LiveQueryExecutor(
            node.nodeName,
            ip,
            node.ysqlServerHttpPort,
            QueryApi.YSQL
          );

          Future<JsonNode> future = threadPool.submit(callable);
          futures.add(future);

          callable = new LiveQueryExecutor(
            node.nodeName,
            ip,
            node.yqlServerHttpPort,
            QueryApi.YCQL
          );
          future = threadPool.submit(callable);
          futures.add(future);
        }
      }
    }

    try {
      for (Future<JsonNode> future : futures) {
        JsonNode response = future.get();
        if (response.has("error")) {
          String type = response.get("type").asText();
          if (type == "ysql") {
            ysqlJson.put("errorCount", ysqlJson.get("errorCount").asInt() + 1);
          } else if (type == "ycql") {
            ycqlJson.put("errorCount", ycqlJson.get("errorCount").asInt() + 1);
          }
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
    } catch (InterruptedException e) {
      LOG.error("Error fetching live query data", e);
    } catch (ExecutionException e) {
      LOG.error("Error fetching live query data", e);
      e.printStackTrace();
    } finally {
      threadPool.shutdown();
    }

    responseJson.set("ysql", ysqlJson);
    responseJson.set("ycql", ycqlJson);

    threadPool.shutdown();
    return responseJson;
  }

  private void concatArrayNodes(ArrayNode destination, JsonNode source) {
    for (JsonNode node : source) {
      destination.add(node);
    }
  }
}
