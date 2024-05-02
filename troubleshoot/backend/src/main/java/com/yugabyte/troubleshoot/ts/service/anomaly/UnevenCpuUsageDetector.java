package com.yugabyte.troubleshoot.ts.service.anomaly;

import com.yugabyte.troubleshoot.ts.models.*;
import com.yugabyte.troubleshoot.ts.service.GraphService;
import java.util.*;
import org.springframework.stereotype.Component;

@Component
public class UnevenCpuUsageDetector extends UnevenDistributionDetector {

  private static final double MIN_ANOMALY_VALUE = 10.0;

  protected UnevenCpuUsageDetector(
      GraphService graphService,
      AnomalyMetadataProvider metadataProvider,
      GraphAnomalyDetectionService anomalyDetectionService) {
    super(graphService, metadataProvider, anomalyDetectionService);
  }

  @Override
  protected double getMinAnomalyValue() {
    return MIN_ANOMALY_VALUE;
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
}
