package com.yugabyte.troubleshoot.ts.service;

import static org.assertj.core.api.Assertions.assertThat;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.troubleshoot.ts.TestUtils;
import com.yugabyte.troubleshoot.ts.models.*;
import java.time.Duration;
import java.time.Instant;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import java.util.stream.IntStream;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.testcontainers.shaded.com.google.common.collect.ImmutableList;
import org.testcontainers.shaded.com.google.common.collect.ImmutableMap;

@ServiceTest
public class GraphServiceTest {

  Instant periodEnd = Instant.ofEpochSecond(1706284172);

  UUID universeUuids = UUID.fromString("ca938bda-0db0-493f-904a-fd835fdabcd2");

  @Autowired GraphService graphService;

  @Autowired PgStatStatementsService pgStatStatementsService;

  @Autowired ObjectMapper objectMapper;

  @BeforeEach
  public void setUp() {
    pgStatStatementsService.save(createStatements("node1", 1L, 50));
    pgStatStatementsService.save(createStatements("node1", 2L, 30));
    pgStatStatementsService.save(createStatements("node2", 1L, 30));
    pgStatStatementsService.save(createStatements("node3", 1L, 30));
  }

  @Test
  public void testOverallGraph() {
    GraphQuery graphQuery = new GraphQuery();
    graphQuery.setEnd(periodEnd);
    graphQuery.setStart(periodEnd.minus(Duration.ofMinutes(200)));
    graphQuery.setFilters(ImmutableMap.of(GraphFilter.queryId, ImmutableList.of("1")));
    graphQuery.setName("query_latency");
    graphQuery.setSettings(new GraphSettings());

    List<GraphResponse> response =
        graphService.getGraphs(universeUuids, ImmutableList.of(graphQuery));

    JsonNode expectedResponse = TestUtils.readResourceAsJson("query/overall_response.json");
    JsonNode actualResponse = objectMapper.valueToTree(response);

    assertThat(actualResponse).isEqualTo(expectedResponse);
  }

  @Test
  public void testOutlierGraph() {
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
        graphService.getGraphs(universeUuids, ImmutableList.of(graphQuery));

    JsonNode expectedResponse = TestUtils.readResourceAsJson("query/outlier_nodes_response.json");
    JsonNode actualResponse = objectMapper.valueToTree(response);

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
        .setUniverseId(universeUuids)
        .setScheduledTimestamp(
            Instant.ofEpochSecond(periodEnd.getEpochSecond() - beforeSeconds - 10))
        .setActualTimestamp(Instant.ofEpochSecond(periodEnd.getEpochSecond() - beforeSeconds))
        .setNodeName(nodeName)
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
