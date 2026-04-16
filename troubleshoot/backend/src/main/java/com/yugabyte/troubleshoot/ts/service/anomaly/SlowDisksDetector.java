package com.yugabyte.troubleshoot.ts.service.anomaly;

import com.google.common.collect.ImmutableList;
import com.yugabyte.troubleshoot.ts.models.Anomaly;
import com.yugabyte.troubleshoot.ts.models.AnomalyMetadata;
import com.yugabyte.troubleshoot.ts.models.GraphAnomaly;
import com.yugabyte.troubleshoot.ts.models.RuntimeConfigKey;
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
  protected List<GraphWithThreshold> getGraphsWithThresholds(AnomalyDetectionContext context) {
    return ImmutableList.of(
        new GraphWithThreshold(
            "disk_io_time",
            context.getConfig().getDouble(RuntimeConfigKey.SLOW_DISKS_THRESHOLD_IO_TIME)),
        new GraphWithThreshold(
            "disk_io_queue_depth",
            context.getConfig().getDouble(RuntimeConfigKey.SLOW_DISKS_THRESHOLD_QUEUE_SIZE)));
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

  @Override
  protected RuntimeConfigKey getMinAnomalyDurationKey() {
    return RuntimeConfigKey.SLOW_DISKS_MIN_ANOMALY_DURATION;
  }
}
