package com.yugabyte.troubleshoot.ts.service.anomaly;

import com.yugabyte.troubleshoot.ts.models.Anomaly;
import com.yugabyte.troubleshoot.ts.models.AnomalyMetadata;
import com.yugabyte.troubleshoot.ts.models.GraphAnomaly;
import com.yugabyte.troubleshoot.ts.service.GraphService;
import org.springframework.stereotype.Component;

@Component
public class UnevenYsqlQueryDetector extends UnevenDistributionDetector {

  // In case it's less than 5 queries per second - we don't care.
  private static final double MIN_ANOMALY_VALUE = 5.0;

  protected UnevenYsqlQueryDetector(
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
    return "ysql_server_rpc_per_second";
  }

  @Override
  protected AnomalyMetadata.AnomalyType getAnomalyType() {
    return AnomalyMetadata.AnomalyType.HOT_NODE_YSQL_QUERIES;
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
            + " processes significantly more YSQL queries"
            + " than average of the other nodes.";
    builder.summary(summary);
    return builder;
  }
}
