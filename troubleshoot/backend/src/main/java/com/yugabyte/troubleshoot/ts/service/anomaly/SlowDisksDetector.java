package com.yugabyte.troubleshoot.ts.service.anomaly;

import com.google.common.collect.ImmutableList;
import com.yugabyte.troubleshoot.ts.models.Anomaly;
import com.yugabyte.troubleshoot.ts.models.AnomalyMetadata;
import com.yugabyte.troubleshoot.ts.models.GraphAnomaly;
import com.yugabyte.troubleshoot.ts.service.GraphService;
import java.util.List;
import org.springframework.stereotype.Component;

@Component
public class SlowDisksDetector extends ThresholdExceedDetector {

  protected SlowDisksDetector(
      GraphService graphService,
      AnomalyMetadataProvider metadataProvider,
      GraphAnomalyDetectionService anomalyDetectionService) {
    super(graphService, metadataProvider, anomalyDetectionService);
  }

  @Override
  protected List<GraphWithThreshold> getGraphsWithThresholds() {
    return ImmutableList.of(
        new GraphWithThreshold("disk_io_time", 99),
        new GraphWithThreshold("disk_io_queue_depth", 100));
  }

  @Override
  protected AnomalyMetadata.AnomalyType getAnomalyType() {
    return AnomalyMetadata.AnomalyType.SLOW_DISKS;
  }

  @Override
  protected Anomaly.AnomalyBuilder fillAnomaly(
      Anomaly.AnomalyBuilder builder,
      AnomalyDetectionContext context,
      String affectedNodesStr,
      GraphAnomaly graphAnomaly) {
    String summary = "Node(s) " + affectedNodesStr + " are possibly low on disk IO.";
    builder.summary(summary);
    return builder;
  }
}
