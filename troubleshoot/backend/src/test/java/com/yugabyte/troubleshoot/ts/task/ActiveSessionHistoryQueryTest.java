package com.yugabyte.troubleshoot.ts.task;

import static com.yugabyte.troubleshoot.ts.CommonUtils.SYSTEM_PLATFORM;
import static com.yugabyte.troubleshoot.ts.TestUtils.readResourceAsJsonList;
import static com.yugabyte.troubleshoot.ts.task.ActiveSessionHistoryQuery.*;
import static org.assertj.core.api.Assertions.assertThat;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.troubleshoot.ts.TestUtils;
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
public class ActiveSessionHistoryQueryTest {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  @Autowired private UniverseMetadataService universeMetadataService;

  @Autowired private UniverseDetailsService universeDetailsService;
  @Autowired private ActiveSessionHistoryService activeSessionHistoryService;
  @Autowired private ActiveSessionHistoryQueryStateService ashQueryStateService;
  @Autowired private RuntimeConfigService runtimeConfigService;
  @Autowired private ThreadPoolTaskExecutor activeSessionHistoryQueryExecutor;
  @Autowired private ThreadPoolTaskExecutor activeSessionHistoryNodesQueryExecutor;
  @Autowired private ObjectMapper objectMapper;

  private ActiveSessionHistoryQuery activeSessionHistoryQuery;

  @Mock private YBAClient ybaClient;

  private UniverseMetadata universeMetadata;

  private String ashQuery;

  @BeforeEach
  public void setUp() {
    activeSessionHistoryQuery =
        new ActiveSessionHistoryQuery(
            universeMetadataService,
            universeDetailsService,
            activeSessionHistoryService,
            ashQueryStateService,
            runtimeConfigService,
            activeSessionHistoryQueryExecutor,
            activeSessionHistoryNodesQueryExecutor,
            ybaClient);

    universeMetadata = UniverseMetadataServiceTest.testData();
    universeMetadataService.save(universeMetadata);

    ashQuery =
        ASH_QUERY_NO_TIMESTAMP
            + ASH_ORDER_AND_LIMIT
            + runtimeConfigService
                .getUniverseConfig(universeMetadata)
                .getLong(RuntimeConfigKey.ASH_QUERY_BATCH);
  }

  @Test
  public void testStatsQueries() throws InterruptedException {
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
    runQueryResult.setResult(readResourceAsJsonList("ash/node1ash.json"));
    when(ybaClient.runSqlQuery(universeMetadata, SYSTEM_PLATFORM, ashQuery, "node1"))
        .thenReturn(runQueryResult);

    runQueryResult = new RunQueryResult();
    runQueryResult.setResult(readResourceAsJsonList("ash/node2ash.json"));
    when(ybaClient.runSqlQuery(universeMetadata, SYSTEM_PLATFORM, ashQuery, "node2"))
        .thenReturn(runQueryResult);
    Map<UUID, UniverseProgress> progresses = activeSessionHistoryQuery.processAllUniverses();

    UniverseProgress progress = progresses.get(universeMetadata.getId());
    while (progress.getStartTimestamp() == 0L || progress.inProgress) {
      Thread.sleep(10);
    }
    List<ActiveSessionHistory> ashEntries = activeSessionHistoryService.listAll();
    List<ActiveSessionHistoryQueryState> ashQueryStates = ashQueryStateService.listAll();

    List<ActiveSessionHistory> ashExpected =
        TestUtils.readCsvAsObjects("ash/ashStored.csv", ActiveSessionHistory.class);
    ashExpected.forEach(e -> e.setUniverseId(universeMetadata.getId()));
    assertThat(ashEntries).containsExactlyInAnyOrderElementsOf(ashExpected);
    assertThat(ashQueryStates)
        .containsExactlyInAnyOrder(
            new ActiveSessionHistoryQueryState()
                .setId(
                    new ActiveSessionHistoryQueryStateId()
                        .setUniverseId(universeMetadata.getId())
                        .setNodeName("node1"))
                .setLastSampleTime(Instant.parse("2024-04-08T14:26:21Z")),
            new ActiveSessionHistoryQueryState()
                .setId(
                    new ActiveSessionHistoryQueryStateId()
                        .setUniverseId(universeMetadata.getId())
                        .setNodeName("node2"))
                .setLastSampleTime(Instant.parse("2024-04-08T14:26:21Z")));
  }
}
