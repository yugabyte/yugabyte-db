package com.yugabyte.troubleshoot.ts.service.anomaly;

import com.yugabyte.troubleshoot.ts.TestUtils;
import com.yugabyte.troubleshoot.ts.models.ActiveSessionHistory;
import com.yugabyte.troubleshoot.ts.models.PgStatStatementsQuery;
import com.yugabyte.troubleshoot.ts.models.UniverseMetadata;
import com.yugabyte.troubleshoot.ts.service.ActiveSessionHistoryService;
import com.yugabyte.troubleshoot.ts.service.PgStatStatementsQueryService;
import com.yugabyte.troubleshoot.ts.service.ServiceTest;
import java.time.Instant;
import java.util.List;
import java.util.UUID;
import lombok.SneakyThrows;
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;

@ServiceTest
public class QueryLockContentionDetectorTest extends AnomalyDetectorTestBase {

  @Autowired QueryLockContentionDetector queryLockContentionDetector;
  @Autowired ActiveSessionHistoryService activeSessionHistoryService;
  @Autowired PgStatStatementsQueryService pgStatStatementsQueryService;

  @SneakyThrows
  @Test
  public void testQueryLatencyDetection() {

    List<PgStatStatementsQuery> statementQueries =
        TestUtils.readCsvAsObjects(
            "anomaly/query_lock_contention/queries.csv", PgStatStatementsQuery.class);

    pgStatStatementsQueryService.save(statementQueries);

    List<ActiveSessionHistory> ashEntries =
        TestUtils.readCsvAsObjects(
            "anomaly/query_lock_contention/ash_lock_contention.csv", ActiveSessionHistory.class);

    activeSessionHistoryService.save(ashEntries);

    UniverseMetadata metadata =
        new UniverseMetadata()
            .setId(UUID.fromString("9ad06d1f-0355-4e3c-a42c-d052b38af7bc"))
            .setCustomerId(UUID.randomUUID());
    AnomalyDetector.AnomalyDetectionResult result =
        queryLockContentionDetector.findAnomalies(
            AnomalyDetector.AnomalyDetectionContext.builder()
                .universeMetadata(metadata)
                .startTime(Instant.parse("2024-01-18T15:00:00Z"))
                .endTime(Instant.parse("2024-01-18T19:00:00Z"))
                .config(runtimeConfigService.getUniverseConfig(metadata))
                .build());

    assertResult(result, "anomaly/query_lock_contention/anomalies.json");
  }

  @Override
  protected String universeUuid() {
    return "9ad06d1f-0355-4e3c-a42c-d052b38af7bc";
  }
}
