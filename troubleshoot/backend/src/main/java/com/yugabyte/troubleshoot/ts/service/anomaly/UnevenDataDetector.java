package com.yugabyte.troubleshoot.ts.service.anomaly;

import com.google.common.collect.ImmutableList;
import com.yugabyte.troubleshoot.ts.models.*;
import com.yugabyte.troubleshoot.ts.service.GraphService;
import java.time.Duration;
import org.springframework.stereotype.Component;

@Component
public class UnevenDataDetector extends UnevenDistributionDetector {

  protected UnevenDataDetector(
      GraphService graphService,
      AnomalyMetadataProvider metadataProvider,
      GraphAnomalyDetectionService anomalyDetectionService) {
    super(graphService, metadataProvider, anomalyDetectionService);
  }

  @Override
  protected String getGraphName() {
    return "table_size";
  }

  @Override
  protected AnomalyMetadata.AnomalyType getAnomalyType() {
    return AnomalyMetadata.AnomalyType.HOT_NODE_DATA;
  }

  @Override
  protected Anomaly.AnomalyBuilder fillAnomaly(
      Anomaly.AnomalyBuilder builder,
      AnomalyDetectionContext context,
      String affectedNodesStr,
      GraphAnomaly graphAnomaly) {
    String databaseName = graphAnomaly.getLabelFirstValue(GraphFilter.dbName.name());
    String tableName = graphAnomaly.getLabelFirstValue(GraphFilter.tableName.name());
    Anomaly.TableInfo tableInfo =
        Anomaly.TableInfo.builder()
            .databaseName(databaseName)
            .tableId(graphAnomaly.getLabelFirstValue(GraphFilter.tableId.name()))
            .tableName(tableName)
            .build();

    String summary =
        "Node(s) "
            + affectedNodesStr
            + " store significantly more data for table '"
            + tableName
            + "' in database '"
            + databaseName
            + "' than average of the other nodes.";
    builder.summary(summary);
    builder.affectedTables(ImmutableList.of(tableInfo));
    return builder;
  }

  @Override
  protected String graphLinesGroupBy(GraphData graphData) {
    return graphData.getNamespaceName() + graphData.getTableId();
  }

  @Override
  protected String anomaliesGroupBy(GraphAnomaly anomaly) {
    return anomaly.getLabelFirstValue(GraphFilter.dbName.name())
        + "_"
        + anomaly.getLabelFirstValue(GraphFilter.tableId.name());
  }

  protected AnomalyDetectionResult findAnomaliesInternal(AnomalyDetectionContext context) {
    AnomalyDetectionContext updatedContext =
        context.toBuilder().stepSeconds(Duration.ofHours(3).toSeconds()).build();

    return super.findAnomaliesInternal(updatedContext);
  }

  @Override
  protected RuntimeConfigKey getMinAnomalyValueKey() {
    return RuntimeConfigKey.UNEVEN_DATA_MIN_ANOMALY_VALUE;
  }

  @Override
  protected RuntimeConfigKey getMinAnomalyDurationKey() {
    return RuntimeConfigKey.UNEVEN_DATA_MIN_ANOMALY_DURATION;
  }

  @Override
  protected RuntimeConfigKey getThresholdRatioKey() {
    return RuntimeConfigKey.UNEVEN_DATA_THRESHOLD;
  }
}
