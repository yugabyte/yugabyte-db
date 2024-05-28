package com.yugabyte.troubleshoot.ts.service.anomaly;

import static org.springframework.test.web.client.match.MockRestRequestMatchers.requestTo;
import static org.springframework.test.web.client.response.MockRestResponseCreators.withSuccess;

import com.yugabyte.troubleshoot.ts.CommonUtils;
import com.yugabyte.troubleshoot.ts.models.*;
import com.yugabyte.troubleshoot.ts.service.*;
import java.time.Instant;
import java.util.UUID;
import lombok.SneakyThrows;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.http.MediaType;

@ServiceTest
public class UnevenCpuUsageDetectorTest extends AnomalyDetectorTestBase {

  @Autowired UnevenCpuUsageDetector unevenCpuUsageDetector;

  UniverseMetadata metadata;

  @BeforeEach
  public void setUp() {
    super.setUp();
    metadata =
        new UniverseMetadata()
            .setId(UUID.fromString("59b6e66f-83ed-4fff-a3c6-b93568237fab"))
            .setCustomerId(UUID.randomUUID());
  }

  @SneakyThrows
  @Test
  public void testUnevenCpuUsageDetection() {

    String queryResponse = CommonUtils.readResource("anomaly/uneven_cpu_usage/prom_response.json");

    String promQuery =
        "http://localhost:9090/api/v1/query_range?query="
            + "avg(rate(node_cpu_seconds_total%7Bmode%3D~%22user%7Csystem%22,%20"
            + "universe_uuid%3D%2259b6e66f-83ed-4fff-a3c6-b93568237fab%22%7D%5B144s%5D))"
            + "%20by%20(mode,%20exported_instance)%20*%20100%20and%20topk(2147483647,%20"
            + "avg(rate(node_cpu_seconds_total%7Bmode%3D~%22user%7Csystem%22,%20"
            + "universe_uuid%3D%2259b6e66f-83ed-4fff-a3c6-b93568237fab%22%7D%5B14400s%5D@1705604400))"
            + "%20by%20(mode,%20exported_instance)%20*%20100)%20by%20(mode)"
            + "&start=2024-01-18T15:00:00.000Z&end=2024-01-18T19:00:00.000Z&step=144s";
    this.server
        .expect(requestTo(promQuery))
        .andRespond(withSuccess(queryResponse, MediaType.APPLICATION_JSON));

    AnomalyDetector.AnomalyDetectionResult result =
        unevenCpuUsageDetector.findAnomalies(
            AnomalyDetector.AnomalyDetectionContext.builder()
                .universeMetadata(metadata)
                .startTime(Instant.parse("2024-01-18T15:00:00Z"))
                .endTime(Instant.parse("2024-01-18T19:00:00Z"))
                .config(runtimeConfigService.getUniverseConfig(metadata))
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
            + "universe_uuid%3D%2259b6e66f-83ed-4fff-a3c6-b93568237fab%22%7D%5B30s%5D))"
            + "%20by%20(mode,%20exported_instance)%20*%20100%20and%20topk(2147483647,%20"
            + "avg(rate(node_cpu_seconds_total%7Bmode%3D~%22user%7Csystem%22,%20"
            + "universe_uuid%3D%2259b6e66f-83ed-4fff-a3c6-b93568237fab%22%7D%5B900s%5D@1709655210))"
            + "%20by%20(mode,%20exported_instance)%20*%20100)%20by%20(mode)"
            + "&start=2024-03-05T15:58:30.000Z&end=2024-03-05T16:13:30.000Z&step=30s";
    this.server
        .expect(requestTo(promQuery))
        .andRespond(withSuccess(queryResponse, MediaType.APPLICATION_JSON));

    AnomalyDetector.AnomalyDetectionResult result =
        unevenCpuUsageDetector.findAnomalies(
            AnomalyDetector.AnomalyDetectionContext.builder()
                .universeMetadata(metadata)
                .startTime(Instant.parse("2024-03-05T15:58:41Z"))
                .endTime(Instant.parse("2024-03-05T16:13:41Z"))
                .config(runtimeConfigService.getUniverseConfig(metadata))
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
            + "universe_uuid%3D%2259b6e66f-83ed-4fff-a3c6-b93568237fab%22%7D%5B35s%5D))"
            + "%20by%20(mode,%20exported_instance)%20*%20100%20and%20topk(2147483647,%20"
            + "avg(rate(node_cpu_seconds_total%7Bmode%3D~%22user%7Csystem%22,%20"
            + "universe_uuid%3D%2259b6e66f-83ed-4fff-a3c6-b93568237fab%22%7D%5B3605s%5D@1709773555))"
            + "%20by%20(mode,%20exported_instance)%20*%20100)%20by%20(mode)"
            + "&start=2024-03-07T00:05:50.000Z&end=2024-03-07T01:05:55.000Z&step=35s";
    this.server
        .expect(requestTo(promQuery))
        .andRespond(withSuccess(queryResponse, MediaType.APPLICATION_JSON));

    AnomalyDetector.AnomalyDetectionResult result =
        unevenCpuUsageDetector.findAnomalies(
            AnomalyDetector.AnomalyDetectionContext.builder()
                .universeMetadata(metadata)
                .startTime(Instant.parse("2024-03-07T00:06:17Z"))
                .endTime(Instant.parse("2024-03-07T01:06:15Z"))
                .config(runtimeConfigService.getUniverseConfig(metadata))
                .build());

    assertResult(result, "anomaly/uneven_cpu_usage/larger_anomaly.json");
  }
}
