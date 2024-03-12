package com.yugabyte.troubleshoot.ts.service.anomaly;

import static org.assertj.core.api.Assertions.assertThat;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.troubleshoot.ts.TestUtils;
import com.yugabyte.troubleshoot.ts.models.*;
import com.yugabyte.troubleshoot.ts.service.*;
import java.time.Instant;
import java.util.List;
import java.util.UUID;
import lombok.SneakyThrows;
import lombok.extern.slf4j.Slf4j;
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;

@ServiceTest
@Slf4j
public class QueryLatencyDetectorTest {

  @Autowired QueryLatencyDetector queryLatencyDetector;
  @Autowired UniverseMetadataService universeMetadataService;
  @Autowired UniverseDetailsService universeDetailsService;
  @Autowired PgStatStatementsService pgStatStatementsService;
  @Autowired PgStatStatementsQueryService pgStatStatementsQueryService;
  @Autowired ObjectMapper objectMapper;

  @SneakyThrows
  @Test
  public void testQueryLatencyDetection() {
    UniverseMetadata universeMetadata = UniverseMetadataServiceTest.testData();
    universeMetadata.setId(UUID.fromString("9ad06d1f-0355-4e3c-a42c-d052b38af7bc"));
    universeMetadataService.save(universeMetadata);

    UniverseDetails universeDetails = UniverseDetailsServiceTest.testData();
    universeDetails.setUniverseUUID(UUID.fromString("9ad06d1f-0355-4e3c-a42c-d052b38af7bc"));
    universeDetailsService.save(universeDetails);

    List<PgStatStatementsQuery> statementQueries =
        TestUtils.readCsvAsObjects(
            "anomaly/query_latency/queries.csv", PgStatStatementsQuery.class);

    pgStatStatementsQueryService.save(statementQueries);

    List<PgStatStatements> statementStats =
        TestUtils.readCsvAsObjects(
            "anomaly/query_latency/query_latency_multi_nodes.csv", PgStatStatements.class);

    pgStatStatementsService.save(statementStats);

    AnomalyDetector.AnomalyDetectionResult result =
        queryLatencyDetector.findAnomalies(
            AnomalyDetector.AnomalyDetectionContext.builder()
                .universeUuid(UUID.fromString("9ad06d1f-0355-4e3c-a42c-d052b38af7bc"))
                .startTime(Instant.parse("2024-01-18T15:00:00Z"))
                .endTime(Instant.parse("2024-01-18T19:00:00Z"))
                .build());

    assertThat(result.isSuccess()).isTrue();
    assertThat(result.getErrorMessages()).isEmpty();
    assertThat(result.getAnomalies()).hasSize(2);

    List<Anomaly> expected =
        TestUtils.readResourceAsList(
            objectMapper, "anomaly/query_latency/anomalies.json", Anomaly.class);
    List<Anomaly> actual =
        result.getAnomalies().stream()
            .map(a -> a.toBuilder().uuid(null).detectionTime(null).build())
            .toList();
    log.info("Actual anomalies found: {}", objectMapper.writeValueAsString(actual));
    assertThat(actual).containsExactlyElementsOf(expected);
  }
}
