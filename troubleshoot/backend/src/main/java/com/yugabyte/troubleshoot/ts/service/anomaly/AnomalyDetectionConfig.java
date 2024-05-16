package com.yugabyte.troubleshoot.ts.service.anomaly;

import com.google.common.collect.ImmutableList;
import java.util.List;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;

@Configuration
public class AnomalyDetectionConfig {

  @Bean
  public List<AnomalyDetector> anomalyDetectors(
      QueryLatencyDetector queryLatencyDetector,
      UnevenCpuUsageDetector unevenCpuUsageDetector,
      UnevenQueryDetector unevenQueryDetector,
      UnevenYsqlQueryDetector unevenYsqlQueryDetector,
      SlowDisksDetector slowDisksDetector) {
    return ImmutableList.<AnomalyDetector>builder()
        .add(queryLatencyDetector)
        .add(unevenCpuUsageDetector)
        .add(unevenQueryDetector)
        .add(unevenYsqlQueryDetector)
        .add(slowDisksDetector)
        .build();
  }
}
