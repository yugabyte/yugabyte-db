package com.yugabyte.troubleshoot.ts.service;

import com.yugabyte.troubleshoot.ts.models.Anomaly;
import com.yugabyte.troubleshoot.ts.models.AnomalyMetadata;
import com.yugabyte.troubleshoot.ts.service.anomaly.AnomalyDetector;
import com.yugabyte.troubleshoot.ts.service.anomaly.AnomalyMetadataProvider;
import io.ebean.Database;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import java.util.concurrent.Future;
import lombok.extern.slf4j.Slf4j;
import org.springframework.scheduling.concurrent.ThreadPoolTaskExecutor;
import org.springframework.stereotype.Service;

@Slf4j
@Service
public class TroubleshootingService {

  private final Database database;
  private final List<AnomalyDetector> anomalyDetectors;
  private final AnomalyMetadataProvider anomalyMetadataProvider;
  private final ThreadPoolTaskExecutor anomalyDetectionExecutor;

  public TroubleshootingService(
      Database database,
      List<AnomalyDetector> anomalyDetectors,
      AnomalyMetadataProvider anomalyMetadataProvider,
      ThreadPoolTaskExecutor anomalyDetectionExecutor) {
    this.database = database;
    this.anomalyDetectors = anomalyDetectors;
    this.anomalyMetadataProvider = anomalyMetadataProvider;
    this.anomalyDetectionExecutor = anomalyDetectionExecutor;
  }

  public List<AnomalyMetadata> getAnomaliesMetadata() {
    return anomalyMetadataProvider.getMetadataList();
  }

  public List<Anomaly> findAnomalies(UUID universeUuid, Instant startTime, Instant endTime) {
    List<Future<AnomalyDetector.AnomalyDetectionResult>> futures = new ArrayList<>();
    for (AnomalyDetector detector : anomalyDetectors) {
      futures.add(
          anomalyDetectionExecutor.submit(
              () -> detector.findAnomalies(universeUuid, startTime, endTime)));
    }
    List<Anomaly> result = new ArrayList<>();
    for (Future<AnomalyDetector.AnomalyDetectionResult> future : futures) {
      try {
        AnomalyDetector.AnomalyDetectionResult detectionResult = future.get();
        if (detectionResult.isSuccess()) {
          result.addAll(detectionResult.getAnomalies());
        } else {
          log.warn("Failure during anomaly detection: {}", detectionResult.getErrorMessages());
        }
      } catch (Exception e) {
        log.warn("Failure during anomaly detection", e);
      }
    }
    return result;
  }
}
