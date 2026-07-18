package com.yugabyte.troubleshoot.ts.service.anomaly;

import static org.springframework.test.web.client.match.MockRestRequestMatchers.requestTo;
import static org.springframework.test.web.client.response.MockRestResponseCreators.withSuccess;

import com.yugabyte.troubleshoot.ts.CommonUtils;
import com.yugabyte.troubleshoot.ts.models.*;
import com.yugabyte.troubleshoot.ts.service.*;
import java.time.Instant;
import java.util.UUID;
import lombok.SneakyThrows;
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.http.MediaType;

@ServiceTest
public class SlowDisksDetectorTest extends AnomalyDetectorTestBase {

  @Autowired SlowDisksDetector slowDisksDetector;

  @SneakyThrows
  @Test
  public void testSlowDiskDetection() {
    UniverseMetadata universeMetadata = UniverseMetadataServiceTest.testData();
    universeMetadata.setId(UUID.fromString("9ad06d1f-0355-4e3c-a42c-d052b38af7bc"));
    universeMetadataService.save(universeMetadata);

    UniverseDetails universeDetails = UniverseDetailsServiceTest.testData();
    universeDetails.setUniverseUUID(UUID.fromString("9ad06d1f-0355-4e3c-a42c-d052b38af7bc"));
    universeDetailsService.save(universeDetails);

    String queryResponseUsage =
        CommonUtils.readResource("anomaly/slow_disks/prom_response_usage.json");

    String promQueryUsage =
        "http://localhost:9090/api/v1/query_range?query="
            + "avg(rate(node_disk_io_time_seconds_total%7B"
            + "universe_uuid%3D%2259b6e66f-83ed-4fff-a3c6-b93568237fab%22%7D%5B144s%5D))"
            + "%20by%20(exported_instance,%20device)%20*%20100%20and%20topk(2147483647,%20"
            + "avg(rate(node_disk_io_time_seconds_total%7B"
            + "universe_uuid%3D%2259b6e66f-83ed-4fff-a3c6-b93568237fab%22%7D%5B14400s%5D@1705604400))"
            + "%20by%20(exported_instance,%20device)%20*%20100)%20by%20(device)"
            + "&start=2024-01-18T15:00:00.000Z&end=2024-01-18T19:00:00.000Z&step=144s";
    this.server
        .expect(requestTo(promQueryUsage))
        .andRespond(withSuccess(queryResponseUsage, MediaType.APPLICATION_JSON));

    String queryResponseQueueSize =
        CommonUtils.readResource("anomaly/slow_disks/prom_response_queue_size.json");

    String promQueryQueueSize =
        "http://localhost:9090/api/v1/query_range?query="
            + "avg(rate(node_disk_io_time_weighted_seconds_total%7B"
            + "universe_uuid%3D%2259b6e66f-83ed-4fff-a3c6-b93568237fab%22%7D%5B144s%5D))"
            + "%20by%20(exported_instance,%20device)%20and%20topk(2147483647,%20"
            + "avg(rate(node_disk_io_time_weighted_seconds_total%7B"
            + "universe_uuid%3D%2259b6e66f-83ed-4fff-a3c6-b93568237fab%22%7D%5B14400s%5D@1705604400))"
            + "%20by%20(exported_instance,%20device))%20by%20(device)"
            + "&start=2024-01-18T15:00:00.000Z&end=2024-01-18T19:00:00.000Z&step=144s";
    this.server
        .expect(requestTo(promQueryQueueSize))
        .andRespond(withSuccess(queryResponseQueueSize, MediaType.APPLICATION_JSON));

    UniverseMetadata metadata =
        new UniverseMetadata()
            .setId(UUID.fromString("59b6e66f-83ed-4fff-a3c6-b93568237fab"))
            .setCustomerId(UUID.randomUUID());
    AnomalyDetector.AnomalyDetectionResult result =
        slowDisksDetector.findAnomalies(
            AnomalyDetector.AnomalyDetectionContext.builder()
                .universeMetadata(metadata)
                .startTime(Instant.parse("2024-01-18T15:00:00Z"))
                .endTime(Instant.parse("2024-01-18T19:00:00Z"))
                .config(runtimeConfigService.getUniverseConfig(metadata))
                .build());

    assertResult(result, "anomaly/slow_disks/anomalies.json");
  }
}
