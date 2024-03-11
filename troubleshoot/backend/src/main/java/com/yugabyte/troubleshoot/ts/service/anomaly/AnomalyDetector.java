package com.yugabyte.troubleshoot.ts.service.anomaly;

import com.yugabyte.troubleshoot.ts.models.Anomaly;
import java.time.Instant;
import java.util.*;
import lombok.Data;
import lombok.experimental.Accessors;

public interface AnomalyDetector {
  public AnomalyDetectionResult findAnomalies(
      UUID universeUuid, Instant startTime, Instant endTime);

  @Data
  @Accessors(chain = true)
  public static class AnomalyDetectionResult {
    private boolean success = true;
    private Set<String> errorMessages = new HashSet<>();
    private List<Anomaly> anomalies = new ArrayList<>();

    public void merge(AnomalyDetectionResult other) {
      success = success && other.success;
      errorMessages.addAll(other.errorMessages);
      anomalies.addAll(other.anomalies);
    }
  }
}
