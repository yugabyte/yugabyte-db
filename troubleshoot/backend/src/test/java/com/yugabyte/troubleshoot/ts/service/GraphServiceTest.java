package com.yugabyte.troubleshoot.ts.service;

import static org.assertj.core.api.Assertions.assertThat;
import static org.springframework.test.web.client.match.MockRestRequestMatchers.requestTo;
import static org.springframework.test.web.client.response.MockRestResponseCreators.withSuccess;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.troubleshoot.ts.CommonUtils;
import com.yugabyte.troubleshoot.ts.models.*;
import java.time.Duration;
import java.time.Instant;
import java.util.List;
import java.util.stream.Collectors;
import java.util.stream.IntStream;
import lombok.SneakyThrows;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.http.MediaType;
import org.springframework.test.web.client.MockRestServiceServer;
import org.springframework.web.client.RestTemplate;
import org.testcontainers.shaded.com.google.common.collect.ImmutableList;
import org.testcontainers.shaded.com.google.common.collect.ImmutableMap;

@ServiceTest
public class GraphServiceTest {

  Instant periodEnd = Instant.ofEpochSecond(1706284172);

  @Autowired UniverseMetadataService universeMetadataService;

  @Autowired UniverseDetailsService universeDetailsService;

  @Autowired GraphService graphService;

  @Autowired PgStatStatementsService pgStatStatementsService;

  @Autowired ObjectMapper objectMapper;

  @Autowired private RestTemplate prometheusClientTemplate;

  private MockRestServiceServer server;

  UniverseMetadata metadata;
  UniverseDetails details;

  @BeforeEach
  public void setUp() {
    metadata = UniverseMetadataServiceTest.testData();
    universeMetadataService.save(metadata);
    details = UniverseDetailsServiceTest.testData(metadata.getId());
    universeDetailsService.save(details);
    pgStatStatementsService.save(createStatements("node1", 1L, 50));
    pgStatStatementsService.save(createStatements("node1", 2L, 30));
    pgStatStatementsService.save(createStatements("node2", 1L, 30));
    pgStatStatementsService.save(createStatements("node3", 1L, 30));

    server = MockRestServiceServer.createServer(prometheusClientTemplate);
  }

  @SneakyThrows
  @Test
  public void testOverallPssGraph() {
    GraphQuery graphQuery = new GraphQuery();
    graphQuery.setEnd(periodEnd);
    graphQuery.setStart(periodEnd.minus(Duration.ofMinutes(200)));
    graphQuery.setFilters(ImmutableMap.of(GraphFilter.queryId, ImmutableList.of("1")));
    graphQuery.setName("query_latency");
    graphQuery.setSettings(new GraphSettings());

    List<GraphResponse> response =
        graphService.getGraphs(metadata.getId(), ImmutableList.of(graphQuery));

    String expectedResponseStr = CommonUtils.readResource("query/overall_pss_response.json");
    expectedResponseStr =
        expectedResponseStr.replaceAll("<universe_uuid>", metadata.getId().toString());
    JsonNode expectedResponse = objectMapper.readTree(expectedResponseStr);
    String actualResponseStr = objectMapper.writeValueAsString(response);
    JsonNode actualResponse = objectMapper.readTree(actualResponseStr);

    assertThat(actualResponse).isEqualTo(expectedResponse);
  }

  @SneakyThrows
  @Test
  public void testOutlierPssGraph() {
    GraphQuery graphQuery = new GraphQuery();
    graphQuery.setEnd(periodEnd);
    graphQuery.setStart(periodEnd.minus(Duration.ofMinutes(200)));
    graphQuery.setFilters(ImmutableMap.of(GraphFilter.queryId, ImmutableList.of("1")));
    graphQuery.setName("query_latency");
    GraphSettings settings = new GraphSettings();
    settings.setSplitMode(GraphSettings.SplitMode.TOP);
    settings.setSplitType(GraphSettings.SplitType.NODE);
    settings.setSplitCount(2);
    settings.setReturnAggregatedValue(true);
    graphQuery.setSettings(settings);

    List<GraphResponse> response =
        graphService.getGraphs(metadata.getId(), ImmutableList.of(graphQuery));

    String expectedResponseStr = CommonUtils.readResource("query/outlier_nodes_pss_response.json");
    expectedResponseStr =
        expectedResponseStr.replaceAll("<universe_uuid>", metadata.getId().toString());
    JsonNode expectedResponse = objectMapper.readTree(expectedResponseStr);
    String actualResponseStr = objectMapper.writeValueAsString(response);
    JsonNode actualResponse = objectMapper.readTree(actualResponseStr);

    assertThat(actualResponse).isEqualTo(expectedResponse);
  }

  @SneakyThrows
  @Test
  public void testOverallMetricsGraph() {
    GraphQuery graphQuery = new GraphQuery();
    graphQuery.setEnd(periodEnd);
    graphQuery.setStart(periodEnd.minus(Duration.ofMinutes(200)));
    graphQuery.setName("ysql_sql_latency");
    graphQuery.setSettings(new GraphSettings());

    String expectedQuery =
        "http://localhost:9090/api/v1/query_range?query="
            + "(avg(rate(rpc_latency_sum%7Bexport_type%3D%22ysql_export%22,%20"
            + "universe_uuid%3D%22<universe_uuid>%22,%20"
            + "service_type%3D%22SQLProcessor%22,%20server_type%3D%22yb_ysqlserver%22,%20"
            + "service_method%3D~%22SelectStmt%7CInsertStmt%7CUpdateStmt%7CDeleteStmt%22%7D%5B120s%5D))"
            + "%20by%20(service_method))%20/%20(avg(rate(rpc_latency_count%7B"
            + "export_type%3D%22ysql_export%22,%20"
            + "universe_uuid%3D%22<universe_uuid>%22,%20"
            + "service_type%3D%22SQLProcessor%22,%20server_type%3D%22yb_ysqlserver%22,%20"
            + "service_method%3D~%22SelectStmt%7CInsertStmt%7CUpdateStmt%7CDeleteStmt%22%7D%5B120s%5D))"
            + "%20by%20(service_method))&start=2024-01-26T12:28:00.000Z&end=2024-01-26T15:48:00.000Z&"
            + "step=120s";
    expectedQuery = expectedQuery.replaceAll("<universe_uuid>", metadata.getId().toString());

    String queryResponse = CommonUtils.readResource("query/prom_overall_query_response.json");
    this.server
        .expect(requestTo(expectedQuery))
        .andRespond(withSuccess(queryResponse, MediaType.APPLICATION_JSON));

    List<GraphResponse> response =
        graphService.getGraphs(metadata.getId(), ImmutableList.of(graphQuery));

    String expectedResponseStr = CommonUtils.readResource("query/overall_metrics_response.json");
    JsonNode expectedResponse = objectMapper.readTree(expectedResponseStr);
    String actualResponseStr = objectMapper.writeValueAsString(response);
    JsonNode actualResponse = objectMapper.readTree(actualResponseStr);

    assertThat(actualResponse).isEqualTo(expectedResponse);
  }

  @SneakyThrows
  @Test
  public void testOverallMetricsGraphWithFilters() {
    GraphQuery graphQuery = new GraphQuery();
    graphQuery.setEnd(periodEnd);
    graphQuery.setStart(periodEnd.minus(Duration.ofMinutes(200)));
    graphQuery.setName("ysql_sql_latency");
    graphQuery.setFilters(
        ImmutableMap.of(
            GraphFilter.regionCode, ImmutableList.of("us-west-1"),
            GraphFilter.instanceType, ImmutableList.of("tserver")));
    graphQuery.setSettings(new GraphSettings());

    String expectedQuery =
        "http://localhost:9090/api/v1/query_range?query="
            + "(avg(rate(rpc_latency_sum%7B"
            + "export_type%3D%22ysql_export%22,%20"
            + "universe_uuid%3D%22<universe_uuid>%22,%20"
            + "service_type%3D%22SQLProcessor%22,%20exported_instance%3D~%22node2%7Cnode3%22,%20"
            + "server_type%3D%22yb_ysqlserver%22,%20"
            + "service_method%3D~%22SelectStmt%7CInsertStmt%7CUpdateStmt%7CDeleteStmt%22%7D%5B120s%5D))"
            + "%20by%20(service_method))%20/%20(avg(rate(rpc_latency_count%7B"
            + "export_type%3D%22ysql_export%22,%20"
            + "universe_uuid%3D%22<universe_uuid>%22,%20"
            + "service_type%3D%22SQLProcessor%22,%20exported_instance%3D~%22node2%7Cnode3%22,%20"
            + "server_type%3D%22yb_ysqlserver%22,%20"
            + "service_method%3D~%22SelectStmt%7CInsertStmt%7CUpdateStmt%7CDeleteStmt%22%7D%5B120s%5D))"
            + "%20by%20(service_method))&start=2024-01-26T12:28:00.000Z&end=2024-01-26T15:48:00.000Z"
            + "&step=120s";
    expectedQuery = expectedQuery.replaceAll("<universe_uuid>", metadata.getId().toString());

    String queryResponse = CommonUtils.readResource("query/prom_overall_query_response.json");
    this.server
        .expect(requestTo(expectedQuery))
        .andRespond(withSuccess(queryResponse, MediaType.APPLICATION_JSON));

    List<GraphResponse> response =
        graphService.getGraphs(metadata.getId(), ImmutableList.of(graphQuery));

    String expectedResponseStr = CommonUtils.readResource("query/overall_metrics_response.json");
    JsonNode expectedResponse = objectMapper.readTree(expectedResponseStr);
    String actualResponseStr = objectMapper.writeValueAsString(response);
    JsonNode actualResponse = objectMapper.readTree(actualResponseStr);

    assertThat(actualResponse).isEqualTo(expectedResponse);
  }

  @SneakyThrows
  @Test
  public void testOutlierNodeMetricsGraphWithFilters() {
    GraphQuery graphQuery = new GraphQuery();
    graphQuery.setEnd(periodEnd);
    graphQuery.setStart(periodEnd.minus(Duration.ofMinutes(200)));
    graphQuery.setName("ysql_sql_latency");
    graphQuery.setFilters(
        ImmutableMap.of(
            GraphFilter.regionCode, ImmutableList.of("us-west-1"),
            GraphFilter.instanceType, ImmutableList.of("tserver")));
    GraphSettings settings = new GraphSettings();
    settings.setSplitMode(GraphSettings.SplitMode.TOP);
    settings.setSplitType(GraphSettings.SplitType.NODE);
    settings.setSplitCount(2);
    settings.setReturnAggregatedValue(true);
    graphQuery.setSettings(settings);

    String expectedQuery =
        "http://localhost:9090/api/v1/query_range?query=((avg(rate(rpc_latency_sum%7B"
            + "export_type%3D%22ysql_export%22,%20"
            + "universe_uuid%3D%22<universe_uuid>%22,%20"
            + "service_type%3D%22SQLProcessor%22,%20exported_instance%3D~%22node2%7Cnode3%22,%20"
            + "server_type%3D%22yb_ysqlserver%22,%20"
            + "service_method%3D~%22SelectStmt%7CInsertStmt%7CUpdateStmt%7CDeleteStmt%22%7D%5B120s%5D))"
            + "%20by%20(service_method,%20exported_instance))%20/%20(avg(rate(rpc_latency_count%7B"
            + "export_type%3D%22ysql_export%22,%20"
            + "universe_uuid%3D%22<universe_uuid>%22,%20"
            + "service_type%3D%22SQLProcessor%22,%20exported_instance%3D~%22node2%7Cnode3%22,%20"
            + "server_type%3D%22yb_ysqlserver%22,%20"
            + "service_method%3D~%22SelectStmt%7CInsertStmt%7CUpdateStmt%7CDeleteStmt%22%7D%5B120s%5D))"
            + "%20by%20(service_method,%20exported_instance))%20and%20"
            + "topk(2,%20(avg(rate(rpc_latency_sum%7Bexport_type%3D%22ysql_export%22,%20"
            + "universe_uuid%3D%22<universe_uuid>%22,%20"
            + "service_type%3D%22SQLProcessor%22,%20exported_instance%3D~%22node2%7Cnode3%22,%20"
            + "server_type%3D%22yb_ysqlserver%22,%20"
            + "service_method%3D~%22SelectStmt%7CInsertStmt%7CUpdateStmt%7CDeleteStmt%22%7D%5B120s%5D"
            + "@1706284080))%20by%20(service_method,%20exported_instance))%20/%20"
            + "(avg(rate(rpc_latency_count%7Bexport_type%3D%22ysql_export%22,%20"
            + "universe_uuid%3D%22<universe_uuid>%22,%20"
            + "service_type%3D%22SQLProcessor%22,%20exported_instance%3D~%22node2%7Cnode3%22,%20"
            + "server_type%3D%22yb_ysqlserver%22,%20"
            + "service_method%3D~%22SelectStmt%7CInsertStmt%7CUpdateStmt%7CDeleteStmt%22%7D%5B120s%5D"
            + "@1706284080))%20by%20(service_method,%20exported_instance)))%20by%20(service_method))%20"
            + "or%20avg((avg(rate(rpc_latency_sum%7Bexport_type%3D%22ysql_export%22,%20"
            + "universe_uuid%3D%22<universe_uuid>%22,%20"
            + "service_type%3D%22SQLProcessor%22,%20exported_instance%3D~%22node2%7Cnode3%22,%20"
            + "server_type%3D%22yb_ysqlserver%22,%20"
            + "service_method%3D~%22SelectStmt%7CInsertStmt%7CUpdateStmt%7CDeleteStmt%22%7D%5B120s%5D))"
            + "%20by%20(service_method,%20exported_instance))%20/%20(avg(rate(rpc_latency_count%7B"
            + "export_type%3D%22ysql_export%22,%20"
            + "universe_uuid%3D%22<universe_uuid>%22,%20"
            + "service_type%3D%22SQLProcessor%22,%20exported_instance%3D~%22node2%7Cnode3%22,%20"
            + "server_type%3D%22yb_ysqlserver%22,%20"
            + "service_method%3D~%22SelectStmt%7CInsertStmt%7CUpdateStmt%7CDeleteStmt%22%7D%5B120s%5D))"
            + "%20by%20(service_method,%20exported_instance)%20!%3D%200))%20by%20(service_method)"
            + "&start=2024-01-26T12:28:00.000Z&end=2024-01-26T15:48:00.000Z&step=120s";
    expectedQuery = expectedQuery.replaceAll("<universe_uuid>", metadata.getId().toString());

    String queryResponse = CommonUtils.readResource("query/prom_outlier_nodes_query_response.json");
    this.server
        .expect(requestTo(expectedQuery))
        .andRespond(withSuccess(queryResponse, MediaType.APPLICATION_JSON));

    List<GraphResponse> response =
        graphService.getGraphs(metadata.getId(), ImmutableList.of(graphQuery));

    String expectedResponseStr =
        CommonUtils.readResource("query/outlier_node_metrics_response.json");
    JsonNode expectedResponse = objectMapper.readTree(expectedResponseStr);
    String actualResponseStr = objectMapper.writeValueAsString(response);
    JsonNode actualResponse = objectMapper.readTree(actualResponseStr);

    assertThat(actualResponse).isEqualTo(expectedResponse);
  }

  private List<PgStatStatements> createStatements(String nodeName, long queryId, int count) {
    return IntStream.range(0, count)
        .mapToObj(
            i ->
                createStatements(
                    Duration.ofMinutes(5 * i).toSeconds(), nodeName, queryId, (double) i))
        .collect(Collectors.toList());
  }

  private PgStatStatements createStatements(
      long beforeSeconds, String nodeName, long queryId, Double value) {
    return new PgStatStatements()
        .setUniverseId(metadata.getId())
        .setScheduledTimestamp(
            Instant.ofEpochSecond(periodEnd.getEpochSecond() - beforeSeconds - 10))
        .setActualTimestamp(Instant.ofEpochSecond(periodEnd.getEpochSecond() - beforeSeconds))
        .setNodeName(nodeName)
        .setDbId("12345")
        .setQueryId(queryId)
        .setRps(value)
        .setRowsAvg(value * 2)
        .setAvgLatency(value * 3)
        .setMeanLatency(value * 4)
        .setP90Latency(value * 5)
        .setP99Latency(value * 6)
        .setMaxLatency(value * 7);
  }
}
