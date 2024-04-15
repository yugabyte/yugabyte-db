package com.yugabyte.troubleshoot.ts.service.anomaly;

import static org.springframework.test.web.client.match.MockRestRequestMatchers.requestTo;
import static org.springframework.test.web.client.response.MockRestResponseCreators.withSuccess;

import com.yugabyte.troubleshoot.ts.CommonUtils;
import com.yugabyte.troubleshoot.ts.models.UniverseMetadata;
import com.yugabyte.troubleshoot.ts.service.ServiceTest;
import java.time.Instant;
import java.util.UUID;
import lombok.SneakyThrows;
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.http.MediaType;

@ServiceTest
public class UnevenDataDetectorTest extends AnomalyDetectorTestBase {

  @Autowired UnevenDataDetector unevenDataDetector;

  @SneakyThrows
  @Test
  public void testUnevenDataDetection() {

    String queryResponse = CommonUtils.readResource("anomaly/uneven_data/prom_response.json");

    String promQuery =
        "http://localhost:9090/api/v1/query_range?query=sum(max%20without%20(metric_type)"
            + "(last_over_time(%7Bexport_type%3D%22tserver_export%22,%20"
            + "saved_name%3D~%22rocksdb_current_version_sst_files_size%7Clog_wal_size%22,%20"
            + "universe_uuid%3D%2259b6e66f-83ed-4fff-a3c6-b93568237fab%22%7D%5B10800s%5D)))%20by%20"
            + "(exported_instance,%20table_id,%20table_name,%20namespace_name)%20"
            + "and%20topk(2147483647,%20sum(max%20without%20(metric_type)"
            + "(last_over_time(%7Bexport_type%3D%22tserver_export%22,%20"
            + "saved_name%3D~%22rocksdb_current_version_sst_files_size%7Clog_wal_size%22,%20"
            + "universe_uuid%3D%2259b6e66f-83ed-4fff-a3c6-b93568237fab"
            + "%22%7D%5B10800s%5D@1705600800)))"
            + "%20by%20(exported_instance,%20table_id,%20table_name,%20namespace_name))"
            + "%20by%20(table_id,%20table_name,%20namespace_name)"
            + "&start=2024-01-18T15:00:00.000Z&end=2024-01-18T18:00:00.000Z&step=10800s";
    this.server
        .expect(requestTo(promQuery))
        .andRespond(withSuccess(queryResponse, MediaType.APPLICATION_JSON));

    UniverseMetadata metadata =
        new UniverseMetadata()
            .setId(UUID.fromString("59b6e66f-83ed-4fff-a3c6-b93568237fab"))
            .setCustomerId(UUID.randomUUID());
    AnomalyDetector.AnomalyDetectionResult result =
        unevenDataDetector.findAnomalies(
            AnomalyDetector.AnomalyDetectionContext.builder()
                .universeMetadata(metadata)
                .startTime(Instant.parse("2024-01-18T15:00:00Z"))
                .endTime(Instant.parse("2024-01-18T19:00:00Z"))
                .config(runtimeConfigService.getUniverseConfig(metadata))
                .build());

    assertResult(result, "anomaly/uneven_data/anomalies.json");
  }
}
