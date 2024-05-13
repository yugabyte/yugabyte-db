package com.yugabyte.troubleshoot.ts.service.anomaly;

import com.yugabyte.troubleshoot.ts.models.Anomaly;
import com.yugabyte.troubleshoot.ts.models.AnomalyMetadata;
import com.yugabyte.troubleshoot.ts.models.GraphAnomaly;
import com.yugabyte.troubleshoot.ts.models.RuntimeConfigKey;
import com.yugabyte.troubleshoot.ts.service.GraphService;
import org.springframework.stereotype.Component;

@Component
public class UnevenQueryDetector extends UnevenDistributionDetector {

  protected UnevenQueryDetector(
      GraphService graphService,
      AnomalyMetadataProvider metadataProvider,
      GraphAnomalyDetectionService anomalyDetectionService) {
    super(graphService, metadataProvider, anomalyDetectionService);
  }

  @Override
  protected String getGraphName() {
    return "tserver_rpcs_per_sec_by_universe";
  }

  @Override
  protected AnomalyMetadata.AnomalyType getAnomalyType() {
    return AnomalyMetadata.AnomalyType.HOT_NODE_READS_WRITES;
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
            + " processes significantly more reads/writes"
            + " than average of the other nodes.";
    builder.summary(summary);
    return builder;
  }

  @Override
  protected RuntimeConfigKey getMinAnomalyValueKey() {
    return RuntimeConfigKey.UNEVEN_QUERY_MIN_ANOMALY_VALUE;
  }

  @Override
  protected RuntimeConfigKey getMinAnomalyDurationKey() {
    return RuntimeConfigKey.UNEVEN_QUERY_MIN_ANOMALY_DURATION;
  }

  @Override
  protected RuntimeConfigKey getThresholdRatioKey() {
    return RuntimeConfigKey.UNEVEN_QUERY_THRESHOLD;
  }
}
