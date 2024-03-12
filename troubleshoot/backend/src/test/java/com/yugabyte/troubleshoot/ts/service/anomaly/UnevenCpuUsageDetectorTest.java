package com.yugabyte.troubleshoot.ts.service.anomaly;

import static org.assertj.core.api.Assertions.assertThat;
import static org.springframework.test.web.client.match.MockRestRequestMatchers.requestTo;
import static org.springframework.test.web.client.response.MockRestResponseCreators.withSuccess;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.troubleshoot.ts.CommonUtils;
import com.yugabyte.troubleshoot.ts.TestUtils;
import com.yugabyte.troubleshoot.ts.models.*;
import com.yugabyte.troubleshoot.ts.service.*;
import java.time.Instant;
import java.util.List;
import java.util.UUID;
import lombok.SneakyThrows;
import lombok.extern.slf4j.Slf4j;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.http.MediaType;
import org.springframework.test.web.client.MockRestServiceServer;
import org.springframework.web.client.RestTemplate;

@ServiceTest
@Slf4j
public class UnevenCpuUsageDetectorTest {

  @Autowired UnevenCpuUsageDetector unevenCpuUsageDetector;
  @Autowired UniverseMetadataService universeMetadataService;
  @Autowired UniverseDetailsService universeDetailsService;

  @Autowired private RestTemplate prometheusClientTemplate;

  private MockRestServiceServer server;
  @Autowired ObjectMapper objectMapper;

  @BeforeEach
  public void setUp() {
    server = MockRestServiceServer.createServer(prometheusClientTemplate);

    UniverseMetadata universeMetadata = UniverseMetadataServiceTest.testData();
    universeMetadata.setId(UUID.fromString("59b6e66f-83ed-4fff-a3c6-b93568237fab"));
    universeMetadataService.save(universeMetadata);

    UniverseDetails universeDetails = UniverseDetailsServiceTest.testData();
    universeDetails.setUniverseUUID(UUID.fromString("59b6e66f-83ed-4fff-a3c6-b93568237fab"));
    universeDetailsService.save(universeDetails);
  }

  @SneakyThrows
  @Test
  public void testUnevenCpuUsageDetection() {

    String queryResponse = CommonUtils.readResource("anomaly/uneven_cpu_usage/prom_response.json");

    String promQuery =
        "http://localhost:9090/api/v1/query_range?query=avg(rate"
            + "(node_cpu_seconds_total%7Bmode%3D~%22user%7Csystem%22,%20"
            + "universe_uuid%3D%2259b6e66f-83ed-4fff-a3c6-b93568237fab%22%7D%5B144s%5D))%20"
            + "by%20(mode,%20exported_instance)%20*%20100%20and%20"
            + "topk(2147483647,%20avg(rate(node_cpu_seconds_total%7Bmode%3D~%22user%7Csystem%22,%20"
            + "universe_uuid%3D%2259b6e66f-83ed-4fff-a3c6-b93568237fab%22%7D%5B144s%5D@1705604400))"
            + "%20by%20(mode,%20exported_instance)%20*%20100)%20by%20(mode)&"
            + "start=2024-01-18T15:00:00.000Z&end=2024-01-18T19:00:00.000Z&step=144s";
    this.server
        .expect(requestTo(promQuery))
        .andRespond(withSuccess(queryResponse, MediaType.APPLICATION_JSON));

    AnomalyDetector.AnomalyDetectionResult result =
        unevenCpuUsageDetector.findAnomalies(
            AnomalyDetector.AnomalyDetectionContext.builder()
                .universeUuid(UUID.fromString("59b6e66f-83ed-4fff-a3c6-b93568237fab"))
                .startTime(Instant.parse("2024-01-18T15:00:00Z"))
                .endTime(Instant.parse("2024-01-18T19:00:00Z"))
                .build());

    assertResult(result, "anomaly/uneven_cpu_usage/anomalies.json");
  }

  @SneakyThrows
  @Test
  public void testShortAnomaly() {
    String queryResponse =
        CommonUtils.readResource("anomaly/uneven_cpu_usage/prom_response_short_anomaly.json");

    String promQuery =
        "http://localhost:9090/api/v1/query_range?query="
            + "avg(rate(node_cpu_seconds_total%7Bmode%3D~%22user%7Csystem%22,%20"
            + "universe_uuid%3D%2259b6e66f-83ed-4fff-a3c6-b93568237fab%22%7D%5B30s%5D))%20"
            + "by%20(mode,%20exported_instance)%20*%20100%20and%20topk(2147483647,%20"
            + "avg(rate(node_cpu_seconds_total%7Bmode%3D~%22user%7Csystem%22,%20"
            + "universe_uuid%3D%2259b6e66f-83ed-4fff-a3c6-b93568237fab%22%7D%5B30s%5D@1709655210))"
            + "%20by%20(mode,%20exported_instance)%20*%20100)%20by%20(mode)&"
            + "start=2024-03-05T15:58:30.000Z&end=2024-03-05T16:13:30.000Z&step=30s";
    this.server
        .expect(requestTo(promQuery))
        .andRespond(withSuccess(queryResponse, MediaType.APPLICATION_JSON));

    AnomalyDetector.AnomalyDetectionResult result =
        unevenCpuUsageDetector.findAnomalies(
            AnomalyDetector.AnomalyDetectionContext.builder()
                .universeUuid(UUID.fromString("59b6e66f-83ed-4fff-a3c6-b93568237fab"))
                .startTime(Instant.parse("2024-03-05T15:58:41Z"))
                .endTime(Instant.parse("2024-03-05T16:13:41Z"))
                .build());

    assertResult(result, "anomaly/uneven_cpu_usage/short_anomaly.json");
  }

  @SneakyThrows
  @Test
  public void testLargerAnomaly() {
    String queryResponse =
        CommonUtils.readResource("anomaly/uneven_cpu_usage/prom_response_larger_anomaly.json");
    String promQuery =
        "http://localhost:9090/api/v1/query_range?query="
            + "avg(rate(node_cpu_seconds_total%7Bmode%3D~%22user%7Csystem%22,%20"
            + "universe_uuid%3D%2259b6e66f-83ed-4fff-a3c6-b93568237fab%22%7D%5B35s%5D))%20"
            + "by%20(mode,%20exported_instance)%20*%20100%20and%20topk(2147483647,%20"
            + "avg(rate(node_cpu_seconds_total%7Bmode%3D~%22user%7Csystem%22,%20"
            + "universe_uuid%3D%2259b6e66f-83ed-4fff-a3c6-b93568237fab%22%7D%5B35s%5D@1709773555))"
            + "%20by%20(mode,%20exported_instance)%20*%20100)%20by%20(mode)&"
            + "start=2024-03-07T00:05:50.000Z&end=2024-03-07T01:05:55.000Z&step=35s";
    this.server
        .expect(requestTo(promQuery))
        .andRespond(withSuccess(queryResponse, MediaType.APPLICATION_JSON));

    AnomalyDetector.AnomalyDetectionResult result =
        unevenCpuUsageDetector.findAnomalies(
            AnomalyDetector.AnomalyDetectionContext.builder()
                .universeUuid(UUID.fromString("59b6e66f-83ed-4fff-a3c6-b93568237fab"))
                .startTime(Instant.parse("2024-03-07T00:06:17Z"))
                .endTime(Instant.parse("2024-03-07T01:06:15Z"))
                .build());

    assertResult(result, "anomaly/uneven_cpu_usage/larger_anomaly.json");
  }

  @SneakyThrows
  private void assertResult(AnomalyDetector.AnomalyDetectionResult result, String path) {
    List<Anomaly> expected = TestUtils.readResourceAsList(objectMapper, path, Anomaly.class);

    assertThat(result.isSuccess()).isTrue();
    assertThat(result.getErrorMessages()).isEmpty();

    List<Anomaly> actual =
        result.getAnomalies().stream()
            .map(a -> a.toBuilder().uuid(null).detectionTime(null).build())
            .toList();
    log.info("Actual anomalies found: {}", objectMapper.writeValueAsString(actual));
    assertThat(actual).containsExactlyElementsOf(expected);
  }
}
