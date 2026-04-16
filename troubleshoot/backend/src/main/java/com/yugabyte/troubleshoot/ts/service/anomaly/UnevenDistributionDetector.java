package com.yugabyte.troubleshoot.ts.service.anomaly;

import com.yugabyte.troubleshoot.ts.models.*;
import com.yugabyte.troubleshoot.ts.service.GraphService;
import java.util.*;

public abstract class UnevenDistributionDetector extends AnomalyDetectorBase {

  protected UnevenDistributionDetector(
      GraphService graphService,
      AnomalyMetadataProvider metadataProvider,
      GraphAnomalyDetectionService anomalyDetectionService) {
    super(graphService, metadataProvider, anomalyDetectionService);
  }

  protected abstract String getGraphName();

  protected AnomalyDetectionResult findAnomaliesInternal(AnomalyDetectionContext context) {
    AnomalyDetectionResult result = new AnomalyDetectionResult();

    GraphResponse response = queryNodeMetric(context, getGraphName(), result);
    if (!result.isSuccess()) {
      return result;
    }

    AnomalyDetectionContext contextWithUpdatedStep =
        context.toBuilder().stepSeconds(response.getStepSeconds()).build();

    long minAnomalyDurationMillis =
        Math.max(
            response.getStepSeconds() * 1000,
            context.getConfig().getDuration(getMinAnomalyDurationKey()).toMillis());
    double minAnomalyValue = context.getConfig().getDouble(getMinAnomalyValueKey());
    double thresholdRatio = context.getConfig().getDouble(getThresholdRatioKey());
    GraphAnomalyDetectionService.AnomalyDetectionSettings detectionSettings =
        new GraphAnomalyDetectionService.AnomalyDetectionSettings()
            .setMinimalAnomalyDurationMillis(minAnomalyDurationMillis)
            .setMinimalAnomalyValue(minAnomalyValue);
    detectionSettings
        .getIncreaseDetectionSettings()
        .setThresholdRatio(thresholdRatio)
        .setWindowMinSize(minAnomalyDurationMillis)
        .setWindowMaxSize(minAnomalyDurationMillis * 2);

    List<List<GraphData>> groupedLines = groupGraphLines(response.getData());

    List<GraphAnomaly> anomalies = new ArrayList<>();
    groupedLines.forEach(
        graphs ->
            anomalies.addAll(
                anomalyDetectionService.getAnomalies(
                    GraphAnomaly.GraphAnomalyType.UNEVEN_DISTRIBUTION, graphs, detectionSettings)));

    groupAndCreateAnomalies(contextWithUpdatedStep, anomalies, result);

    return result;
  }

  protected abstract RuntimeConfigKey getMinAnomalyValueKey();

  protected abstract RuntimeConfigKey getThresholdRatioKey();
}
