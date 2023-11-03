// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.queries;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.forms.LiveQueriesParams;
import java.util.UUID;
import java.util.concurrent.Callable;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import play.libs.Json;
import play.libs.ws.WSClient;

@Slf4j
public class LiveQueryExecutor implements Callable<JsonNode> {

  private final ApiHelper apiHelper;
  // hostname can be either IP address or DNS
  private final String hostName;
  private final String nodeName;
  private final int port;
  private final QueryHelper.QueryApi apiType;

  public LiveQueryExecutor(
      String nodeName, String hostName, int port, QueryHelper.QueryApi api, WSClient wsClient) {
    this(nodeName, hostName, port, api, new ApiHelper(wsClient));
  }

  @VisibleForTesting
  LiveQueryExecutor(
      String nodeName, String hostName, int port, QueryHelper.QueryApi api, ApiHelper apiHelper) {
    this.nodeName = nodeName;
    this.hostName = hostName;
    this.port = port;
    this.apiType = api;
    this.apiHelper = apiHelper;
  }

  @Override
  public JsonNode call() throws Exception {
    String url = String.format("http://%s:%d/rpcz", hostName, port);
    try {
      JsonNode response = apiHelper.getRequest(url);
      if (apiType == QueryHelper.QueryApi.YSQL) {
        return processYSQLRowData(response);
      } else {
        return processYCQLRowData(response);
      }
    } catch (Exception e) {
      log.error(String.format("Exception while fetching url: %s", url), e);
      ObjectNode errorJson = Json.newObject();
      errorJson.put("error", e.getMessage());
      errorJson.put("type", apiType == QueryHelper.QueryApi.YSQL ? "ysql" : "ycql");
      return errorJson;
    }
  }

  // Processes YSQL connection data from /rpcz endpoint and transforms to row data
  private JsonNode processYSQLRowData(JsonNode response) {
    ObjectNode responseJson = Json.newObject();
    ObjectMapper mapper = new ObjectMapper();
    if (response.has("connections")) {
      for (JsonNode objNode : response.get("connections")) {
        if (objNode.has("backend_type")
            && objNode.get("backend_type").asText().equalsIgnoreCase("client backend")
            && objNode.has("backend_status")
            && !objNode.get("backend_status").asText().equalsIgnoreCase("idle")) {
          try {
            // Rather than access the JSON through .get()'s we convert to POJO
            LiveQueriesParams.YSQLQueryParams params =
                mapper.treeToValue(objNode, LiveQueriesParams.YSQLQueryParams.class);
            ObjectNode rowData = Json.newObject();
            // Random UUID intended for table row key
            rowData.put("id", UUID.randomUUID().toString());
            rowData.put("nodeName", nodeName);
            rowData.put("privateIp", hostName);
            rowData.put("dbName", params.db_name);
            rowData.put("sessionStatus", params.backend_status);
            rowData.put("query", params.query);
            rowData.put("elapsedMillis", params.query_running_for_ms);
            rowData.put("queryStartTime", params.query_start_time);
            rowData.put("appName", params.application_name);
            rowData.put("clientHost", params.host);
            rowData.put("clientPort", params.port);

            ArrayNode ysqlArray;
            if (!responseJson.has("ysql")) {
              ysqlArray = responseJson.putArray("ysql");
            } else {
              ysqlArray = (ArrayNode) responseJson.get("ysql");
            }

            ysqlArray.add(rowData);
          } catch (JsonProcessingException e) {
            // Try to process all connections even if there is an exception
            log.error(e.getMessage(), e);
          }
        }
      }
    }
    return responseJson;
  }

  // Similar to above helper function except for YCQL connection info
  @VisibleForTesting
  JsonNode processYCQLRowData(JsonNode response) {
    ObjectNode responseJson = Json.newObject();
    ObjectMapper mapper = new ObjectMapper();
    if (response.has("inbound_connections")) {
      for (JsonNode objNode : response.get("inbound_connections")) {
        try {
          LiveQueriesParams.YCQLQueryParams params =
              mapper.treeToValue(objNode, LiveQueriesParams.YCQLQueryParams.class);
          if (params.calls_in_flight != null) {
            for (LiveQueriesParams.QueryCallsInFlight query : params.calls_in_flight) {
              if (query.cql_details == null) {
                continue;
              }
              // Get SQL query string, joining multiple entries if necessary
              StringBuilder queryStringBuilder = new StringBuilder();
              ObjectNode rowData = Json.newObject();
              for (JsonNode callDetail : query.cql_details.call_details) {
                if (queryStringBuilder.length() > 0) {
                  queryStringBuilder.append(" ");
                }
                queryStringBuilder.append(callDetail.get("sql_string").asText());
              }
              String keyspace = StringUtils.EMPTY;
              if (params.connection_details != null
                  && params.connection_details.cql_connection_details != null
                  && params.connection_details.cql_connection_details.has("keyspace")) {
                keyspace =
                    params.connection_details.cql_connection_details.get("keyspace").asText();
              }
              String clientHost = StringUtils.EMPTY;
              String clientPort = StringUtils.EMPTY;
              int hostPortDelimiterIndex = params.remote_ip.lastIndexOf(":");
              if (hostPortDelimiterIndex < 0) {
                log.warn("Invalid remove_ip field in response: {}", params.remote_ip);
              } else {
                clientHost = params.remote_ip.substring(0, hostPortDelimiterIndex);
                clientPort = params.remote_ip.substring(hostPortDelimiterIndex + 1);
              }
              // Random UUID intended for table row key
              rowData.put("id", UUID.randomUUID().toString());
              rowData.put("nodeName", nodeName);
              rowData.put("privateIp", hostName);
              rowData.put("keyspace", keyspace);
              rowData.put("query", queryStringBuilder.toString());
              rowData.put("type", query.cql_details.type);
              rowData.put("elapsedMillis", query.elapsed_millis);
              rowData.put("clientHost", clientHost);
              rowData.put("clientPort", clientPort);

              ArrayNode ycqlArray;
              if (!responseJson.has("ycql")) {
                ycqlArray = responseJson.putArray("ycql");
              } else {
                ycqlArray = (ArrayNode) responseJson.get("ycql");
              }

              ycqlArray.add(rowData);
            }
          }
        } catch (JsonProcessingException e) {
          log.error(e.getMessage(), e);
        }
      }
    }
    return responseJson;
  }
}
