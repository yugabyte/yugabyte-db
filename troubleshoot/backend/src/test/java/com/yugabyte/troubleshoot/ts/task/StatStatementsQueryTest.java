package com.yugabyte.troubleshoot.ts.task;

import static com.yugabyte.troubleshoot.ts.TestUtils.readResourceAsJsonList;
import static com.yugabyte.troubleshoot.ts.task.StatStatementsQuery.*;
import static org.assertj.core.api.Assertions.assertThat;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.troubleshoot.ts.models.*;
import com.yugabyte.troubleshoot.ts.service.*;
import com.yugabyte.troubleshoot.ts.yba.client.YBAClient;
import com.yugabyte.troubleshoot.ts.yba.models.RunQueryResult;
import java.time.Instant;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.junit.Rule;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.scheduling.concurrent.ThreadPoolTaskExecutor;
import org.testcontainers.shaded.com.google.common.collect.ImmutableList;
import org.testcontainers.shaded.com.google.common.collect.ImmutableSet;

@ServiceTest
public class StatStatementsQueryTest {

  private static String PG_STAT_STATEMENTS_QUERY =
      PG_STAT_STATEMENTS_QUERY_PART1 + PG_STAT_STATEMENTS_QUERY_LH + PG_STAT_STATEMENTS_QUERY_PART2;

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  @Autowired private UniverseMetadataService universeMetadataService;

  @Autowired private UniverseDetailsService universeDetailsService;
  @Autowired private PgStatStatementsService pgStatStatementsService;
  @Autowired private PgStatStatementsQueryService pgStatStatementsQueryService;
  @Autowired private ThreadPoolTaskExecutor pgStatStatementsQueryExecutor;
  @Autowired private ThreadPoolTaskExecutor pgStatStatementsNodesQueryExecutor;
  @Autowired private ObjectMapper objectMapper;

  private StatStatementsQuery statStatementsQuery;

  @Mock private YBAClient ybaClient;

  @BeforeEach
  public void setUp() {
    statStatementsQuery =
        new StatStatementsQuery(
            universeMetadataService,
            universeDetailsService,
            pgStatStatementsService,
            pgStatStatementsQueryService,
            pgStatStatementsQueryExecutor,
            pgStatStatementsNodesQueryExecutor,
            objectMapper,
            ybaClient);
  }

  @Test
  public void testStatsQueries() throws InterruptedException {
    UniverseMetadata universeMetadata = UniverseMetadataServiceTest.testData();
    universeMetadataService.save(universeMetadata);

    UniverseDetails.UniverseDefinition definition = new UniverseDetails.UniverseDefinition();
    UniverseDetails details =
        new UniverseDetails()
            .setName("universe1")
            .setUniverseUUID(universeMetadata.getId())
            .setUniverseDetails(definition);
    UniverseDetails.UniverseDefinition.Cluster cluster =
        new UniverseDetails.UniverseDefinition.Cluster();
    cluster.setUuid(UUID.randomUUID());
    UniverseDetails.UniverseDefinition.UserIntent userIntent =
        new UniverseDetails.UniverseDefinition.UserIntent();
    userIntent.setYbSoftwareVersion("2.21.0.0-b100");
    cluster.setUserIntent(userIntent);
    definition.setClusters(ImmutableList.of(cluster));

    UniverseDetails.UniverseDefinition.NodeDetails node1 =
        new UniverseDetails.UniverseDefinition.NodeDetails();
    node1.setNodeName("node1");
    node1.setPlacementUuid(cluster.getUuid());
    UniverseDetails.UniverseDefinition.NodeDetails node2 =
        new UniverseDetails.UniverseDefinition.NodeDetails();
    node2.setNodeName("node2");
    node2.setPlacementUuid(cluster.getUuid());
    definition.setNodeDetailsSet(ImmutableSet.of(node1, node2));
    universeDetailsService.save(details);

    RunQueryResult runQueryResult = new RunQueryResult();
    runQueryResult.setResult(readResourceAsJsonList("stat_statements/node1stats.t1.json"));
    when(ybaClient.runSqlQuery(
            universeMetadata, SYSTEM_PLATFORM, PG_STAT_STATEMENTS_QUERY, "node1"))
        .thenReturn(runQueryResult);

    runQueryResult = new RunQueryResult();
    runQueryResult.setResult(readResourceAsJsonList("stat_statements/node2stats.t1.json"));
    when(ybaClient.runSqlQuery(
            universeMetadata, SYSTEM_PLATFORM, PG_STAT_STATEMENTS_QUERY, "node2"))
        .thenReturn(runQueryResult);
    Map<UUID, UniverseProgress> progresses = statStatementsQuery.processAllUniverses();

    UniverseProgress progress = progresses.get(universeMetadata.getId());
    while (progress.getStartTimestamp() == 0L || progress.inProgress) {
      Thread.sleep(10);
    }
    List<PgStatStatements> stats = pgStatStatementsService.listAll();
    List<PgStatStatementsQuery> queries = pgStatStatementsQueryService.listAll();

    assertThat(stats).isEmpty();
    assertThat(queries).hasSize(2);
    assertThat(queries).allMatch(q -> q.getScheduledTimestamp() != null);
    queries.forEach(pgStatStatementsQuery -> pgStatStatementsQuery.setScheduledTimestamp(null));
    List<PgStatStatementsQuery> expectedQueries =
        ImmutableList.of(
            new PgStatStatementsQuery()
                .setId(
                    new PgStatStatementsQueryId()
                        .setUniverseId(universeMetadata.getId())
                        .setDbId("13243")
                        .setQueryId(10))
                .setQuery("query1")
                .setDbName("postgres"),
            new PgStatStatementsQuery()
                .setId(
                    new PgStatStatementsQueryId()
                        .setUniverseId(universeMetadata.getId())
                        .setDbId("13243")
                        .setQueryId(11))
                .setQuery("query2")
                .setDbName("postgres"));
    assertThat(queries).containsExactlyInAnyOrderElementsOf(expectedQueries);

    runQueryResult = new RunQueryResult();
    runQueryResult.setResult(readResourceAsJsonList("stat_statements/node1stats.t2.json"));
    when(ybaClient.runSqlQuery(
            universeMetadata, SYSTEM_PLATFORM, PG_STAT_STATEMENTS_QUERY, "node1"))
        .thenReturn(runQueryResult);

    runQueryResult = new RunQueryResult();
    runQueryResult.setResult(readResourceAsJsonList("stat_statements/node2stats.t2.json"));
    when(ybaClient.runSqlQuery(
            universeMetadata, SYSTEM_PLATFORM, PG_STAT_STATEMENTS_QUERY, "node2"))
        .thenReturn(runQueryResult);
    progresses = statStatementsQuery.processAllUniverses();

    progress = progresses.get(universeMetadata.getId());
    while (progress.getStartTimestamp() == 0L || progress.inProgress) {
      Thread.sleep(10);
    }
    stats = pgStatStatementsService.listAll();
    queries = pgStatStatementsQueryService.listAll();

    assertThat(queries).hasSize(2);
    assertThat(queries).allMatch(q -> q.getScheduledTimestamp() != null);
    queries.forEach(pgStatStatementsQuery -> pgStatStatementsQuery.setScheduledTimestamp(null));
    assertThat(queries).containsExactlyInAnyOrderElementsOf(expectedQueries);

    assertThat(stats).hasSize(3);
    assertThat(stats).allMatch(s -> s.getScheduledTimestamp() != null);
    stats.forEach(pgStatStatements -> pgStatStatements.setScheduledTimestamp(null));
    assertThat(stats)
        .containsExactlyInAnyOrder(
            new PgStatStatements()
                .setActualTimestamp(Instant.parse("2023-12-25T15:58:39.246982Z"))
                .setNodeName("node2")
                .setUniverseId(universeMetadata.getId())
                .setDbId("13243")
                .setQueryId(10)
                .setRps(0.0016666666666666668)
                .setRowsAvg(1.0)
                .setAvgLatency(2.0)
                .setMeanLatency(2.0)
                .setP90Latency(2.0)
                .setP99Latency(2.0)
                .setMaxLatency(2.0),
            new PgStatStatements()
                .setActualTimestamp(Instant.parse("2023-12-25T15:58:39.246982Z"))
                .setNodeName("node1")
                .setUniverseId(universeMetadata.getId())
                .setDbId("13243")
                .setQueryId(10)
                .setRps(0.16666666666666666)
                .setRowsAvg(4.95)
                .setAvgLatency(12.9)
                .setMeanLatency(1.3)
                .setP90Latency(1.4)
                .setP99Latency(1.4)
                .setMaxLatency(2.0),
            new PgStatStatements()
                .setActualTimestamp(Instant.parse("2023-12-25T15:58:39.246982Z"))
                .setNodeName("node1")
                .setUniverseId(universeMetadata.getId())
                .setDbId("13243")
                .setQueryId(11)
                .setRps(0.16666666666666666)
                .setRowsAvg(9.9)
                .setAvgLatency(12.91)
                .setMeanLatency(1.3)
                .setP90Latency(1.4)
                .setP99Latency(2.0)
                .setMaxLatency(2.0));
  }
}
