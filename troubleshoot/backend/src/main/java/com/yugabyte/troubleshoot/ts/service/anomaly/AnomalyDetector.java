package com.yugabyte.troubleshoot.ts.service.anomaly;

import com.yugabyte.troubleshoot.ts.models.Anomaly;
import com.yugabyte.troubleshoot.ts.models.RuntimeConfig;
import com.yugabyte.troubleshoot.ts.models.UniverseMetadata;
import java.time.Instant;
import java.util.*;
import lombok.Builder;
import lombok.Data;
import lombok.Value;
import lombok.experimental.Accessors;

public interface AnomalyDetector {
  public AnomalyDetectionResult findAnomalies(AnomalyDetectionContext context);

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

  @Value
  @Builder(toBuilder = true)
  @Accessors(chain = true)
  class AnomalyDetectionContext {
    UniverseMetadata universeMetadata;
    Instant startTime;
    Long stepSeconds;
    Instant endTime;
    Object customContext;
    RuntimeConfig config;

    UUID getUniverseUuid() {
      return universeMetadata.getId();
    }
  }
}
