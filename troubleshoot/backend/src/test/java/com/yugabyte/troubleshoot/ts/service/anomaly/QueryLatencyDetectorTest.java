package com.yugabyte.troubleshoot.ts.service.anomaly;

import com.yugabyte.troubleshoot.ts.TestUtils;
import com.yugabyte.troubleshoot.ts.models.*;
import com.yugabyte.troubleshoot.ts.service.*;
import java.time.Instant;
import java.util.List;
import java.util.UUID;
import lombok.SneakyThrows;
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;

@ServiceTest
public class QueryLatencyDetectorTest extends AnomalyDetectorTestBase {

  @Autowired QueryLatencyDetector queryLatencyDetector;
  @Autowired PgStatStatementsService pgStatStatementsService;
  @Autowired PgStatStatementsQueryService pgStatStatementsQueryService;

  @SneakyThrows
  @Test
  public void testQueryLatencyDetection() {

    List<PgStatStatementsQuery> statementQueries =
        TestUtils.readCsvAsObjects(
            "anomaly/query_latency/queries.csv", PgStatStatementsQuery.class);

    pgStatStatementsQueryService.save(statementQueries);

    List<PgStatStatements> statementStats =
        TestUtils.readCsvAsObjects(
            "anomaly/query_latency/query_latency_multi_nodes.csv", PgStatStatements.class);

    pgStatStatementsService.save(statementStats);

    UniverseMetadata metadata =
        new UniverseMetadata()
            .setId(UUID.fromString("9ad06d1f-0355-4e3c-a42c-d052b38af7bc"))
            .setCustomerId(UUID.randomUUID());
    AnomalyDetector.AnomalyDetectionResult result =
        queryLatencyDetector.findAnomalies(
            AnomalyDetector.AnomalyDetectionContext.builder()
                .universeMetadata(metadata)
                .startTime(Instant.parse("2024-01-18T15:00:00Z"))
                .endTime(Instant.parse("2024-01-18T19:00:00Z"))
                .config(runtimeConfigService.getUniverseConfig(metadata))
                .build());

    assertResult(result, "anomaly/query_latency/anomalies.json");
  }

  @Override
  protected String universeUuid() {
    return "9ad06d1f-0355-4e3c-a42c-d052b38af7bc";
  }
}
