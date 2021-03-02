// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.queries;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.forms.SlowQueriesParams;
import com.yugabyte.yw.models.MetricConfig;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.Configuration;
import play.api.Play;
import play.libs.Json;

import java.util.*;
import java.util.concurrent.Callable;

public class SlowQueryExecutor implements Callable<JsonNode> {
  public static final Logger LOG = LoggerFactory.getLogger(LiveQueryExecutor.class);

  private final ApiHelper apiHelper;
  // hostname can be either IP address or DNS
  private String hostName;
  private String nodeName;
  private int port;

  public SlowQueryExecutor(String nodeName, String hostName,
                           int port) {
    this.nodeName = nodeName;
    this.hostName = hostName;
    this.port = port;
    this.apiHelper = Play.current().injector().instanceOf(ApiHelper.class);
  }

  @Override
  public JsonNode call() {
    String url = String.format("http://%s:%d/statements", hostName, port);
    JsonNode response = apiHelper.getRequest(url);
    return processStatementData(response);
  }

  private JsonNode processStatementData(JsonNode response) {
    ObjectNode responseJson = Json.newObject();
    ObjectMapper mapper = new ObjectMapper();
    if (response.has("statements")) {
      for (JsonNode objNode : response.get("statements")) {
        try {
          // Rather than access the JSON through .get()'s we convert to POJO
          SlowQueriesParams params = mapper.treeToValue(
            objNode,
            SlowQueriesParams.class
          );
          ObjectNode rowData = Json.newObject();
          // Random UUID intended for table row key
          rowData.put("id", UUID.randomUUID().toString());
          rowData.put("nodeName", nodeName);
          rowData.put("privateIp", hostName);
          rowData.put("query", params.query);
          rowData.put("calls", params.calls);
          rowData.put("averageTime", params.mean_time);
          rowData.put("totalTime", params.total_time);
          rowData.put("minTime", params.min_time);
          rowData.put("maxTime", params.max_time);
          rowData.put("stdDevTime", params.stddev_time);
          if (!responseJson.has("ysql")) {
            ArrayNode ysqlArray = responseJson.putArray("ysql");
            ysqlArray.add(rowData);
          } else {
            ArrayNode ysqlArray = (ArrayNode) responseJson.get("ysql");
            ysqlArray.add(rowData);
          }
        } catch (JsonProcessingException exception) {
          // Try to process all connections even if there is an exception
          LOG.error("Unable to process JSON from YSQL query. {}", objNode.asText());
        }
      }
    }
    return responseJson;
  }
}
