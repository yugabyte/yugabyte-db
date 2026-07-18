package com.yugabyte.troubleshoot.ts.service.anomaly;

import com.yugabyte.troubleshoot.ts.models.*;
import com.yugabyte.troubleshoot.ts.service.GraphService;
import java.util.*;
import org.springframework.stereotype.Component;

@Component
public class UnevenCpuUsageDetector extends UnevenDistributionDetector {

  protected UnevenCpuUsageDetector(
      GraphService graphService,
      AnomalyMetadataProvider metadataProvider,
      GraphAnomalyDetectionService anomalyDetectionService) {
    super(graphService, metadataProvider, anomalyDetectionService);
  }

  @Override
  protected String getGraphName() {
    return "cpu_usage";
  }

  @Override
  protected AnomalyMetadata.AnomalyType getAnomalyType() {
    return AnomalyMetadata.AnomalyType.HOT_NODE_CPU;
  }

  @Override
  protected Anomaly.AnomalyBuilder fillAnomaly(
      Anomaly.AnomalyBuilder builder,
      AnomalyDetectionContext context,
      String affectedNodesStr,
      GraphAnomaly graphAnomaly) {
    String summary =
        "Node(s) "
            + affectedNodesStr
            + " consume significantly more CPU than average of the other nodes.";
    builder.summary(summary);
    return builder;
  }

  @Override
  protected RuntimeConfigKey getMinAnomalyValueKey() {
    return RuntimeConfigKey.UNEVEN_CPU_MIN_ANOMALY_VALUE;
  }

  @Override
  protected RuntimeConfigKey getMinAnomalyDurationKey() {
    return RuntimeConfigKey.UNEVEN_CPU_MIN_ANOMALY_DURATION;
  }

  @Override
  protected RuntimeConfigKey getThresholdRatioKey() {
    return RuntimeConfigKey.UNEVEN_CPU_THRESHOLD;
  }
}
