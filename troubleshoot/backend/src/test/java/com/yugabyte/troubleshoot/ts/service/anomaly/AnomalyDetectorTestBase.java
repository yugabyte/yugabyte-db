package com.yugabyte.troubleshoot.ts.service.anomaly;

import static org.assertj.core.api.Assertions.assertThat;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.troubleshoot.ts.TestUtils;
import com.yugabyte.troubleshoot.ts.models.Anomaly;
import com.yugabyte.troubleshoot.ts.models.UniverseDetails;
import com.yugabyte.troubleshoot.ts.models.UniverseMetadata;
import com.yugabyte.troubleshoot.ts.service.*;
import java.util.List;
import java.util.UUID;
import lombok.SneakyThrows;
import lombok.extern.slf4j.Slf4j;
import org.junit.jupiter.api.BeforeEach;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.test.web.client.MockRestServiceServer;
import org.springframework.web.client.RestTemplate;

@Slf4j
public abstract class AnomalyDetectorTestBase {

  @Autowired UniverseMetadataService universeMetadataService;
  @Autowired UniverseDetailsService universeDetailsService;
  @Autowired protected RestTemplate prometheusClientTemplate;
  protected MockRestServiceServer server;
  @Autowired protected ObjectMapper objectMapper;

  protected String universeUuid() {
    return "59b6e66f-83ed-4fff-a3c6-b93568237fab";
  }

  @BeforeEach
  public void setUp() {
    server = MockRestServiceServer.createServer(prometheusClientTemplate);

    UniverseMetadata universeMetadata = UniverseMetadataServiceTest.testData();
    universeMetadata.setId(UUID.fromString(universeUuid()));
    universeMetadataService.save(universeMetadata);

    UniverseDetails universeDetails = UniverseDetailsServiceTest.testData();
    universeDetails.setUniverseUUID(UUID.fromString(universeUuid()));
    universeDetailsService.save(universeDetails);
  }

  @SneakyThrows
  protected void assertResult(AnomalyDetector.AnomalyDetectionResult result, String path) {
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
