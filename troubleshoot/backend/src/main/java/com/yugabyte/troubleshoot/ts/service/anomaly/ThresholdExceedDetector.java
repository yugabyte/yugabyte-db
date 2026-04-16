package com.yugabyte.troubleshoot.ts.service.anomaly;

import com.yugabyte.troubleshoot.ts.models.*;
import com.yugabyte.troubleshoot.ts.service.GraphService;
import java.util.*;
import lombok.Value;

public abstract class ThresholdExceedDetector extends AnomalyDetectorBase {

  protected ThresholdExceedDetector(
      GraphService graphService,
      AnomalyMetadataProvider metadataProvider,
      GraphAnomalyDetectionService anomalyDetectionService) {
    super(graphService, metadataProvider, anomalyDetectionService);
  }

  protected abstract List<GraphWithThreshold> getGraphsWithThresholds(
      AnomalyDetectionContext context);

  protected AnomalyDetectionResult findAnomaliesInternal(AnomalyDetectionContext context) {
    AnomalyDetectionResult result = new AnomalyDetectionResult();

    List<GraphAnomaly> anomalies = new ArrayList<>();
    AnomalyDetectionContext.AnomalyDetectionContextBuilder contextWithUpdatedStep =
        context.toBuilder();
    getGraphsWithThresholds(context)
        .forEach(
            graphWithThreshold -> {
              GraphResponse response =
                  queryNodeMetric(context, graphWithThreshold.getGraphName(), result);
              if (!result.isSuccess()) {
                return;
              }

              long minAnomalyDurationMillis =
                  Math.max(
                      response.getStepSeconds() * 1000,
                      context.getConfig().getDuration(getMinAnomalyDurationKey()).toMillis());
              contextWithUpdatedStep.stepSeconds(response.getStepSeconds());
              GraphAnomalyDetectionService.AnomalyDetectionSettings detectionSettings =
                  new GraphAnomalyDetectionService.AnomalyDetectionSettings()
                      .setMinimalAnomalyDurationMillis(minAnomalyDurationMillis);
              detectionSettings
                  .getIncreaseDetectionSettings()
                  .setWindowMinSize(minAnomalyDurationMillis)
                  .setWindowMaxSize(minAnomalyDurationMillis * 2);
              detectionSettings
                  .getThresholdExceedSettings()
                  .setThreshold(graphWithThreshold.getThreshold());

              List<List<GraphData>> groupedLines = groupGraphLines(response.getData());

              groupedLines.forEach(
                  graphs ->
                      anomalies.addAll(
                          anomalyDetectionService.getAnomalies(
                              GraphAnomaly.GraphAnomalyType.EXCEED_THRESHOLD,
                              graphs,
                              detectionSettings)));
            });

    if (!result.isSuccess()) {
      return result;
    }

    groupAndCreateAnomalies(contextWithUpdatedStep.build(), anomalies, result);

    return result;
  }

  @Value
  protected static class GraphWithThreshold {
    String graphName;
    double threshold;
  }
}
