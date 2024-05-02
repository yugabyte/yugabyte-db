package com.yugabyte.troubleshoot.ts.service.anomaly;

import com.yugabyte.troubleshoot.ts.models.*;
import com.yugabyte.troubleshoot.ts.service.GraphService;
import java.util.*;
import java.util.stream.Collectors;
import lombok.Value;

public abstract class ThresholdExceedDetector extends AnomalyDetectorBase {

  protected ThresholdExceedDetector(
      GraphService graphService,
      AnomalyMetadataProvider metadataProvider,
      GraphAnomalyDetectionService anomalyDetectionService) {
    super(graphService, metadataProvider, anomalyDetectionService);
  }

  protected abstract List<GraphWithThreshold> getGraphsWithThresholds();

  public AnomalyDetectionResult findAnomalies(AnomalyDetectionContext context) {
    AnomalyDetectionResult result = new AnomalyDetectionResult();

    List<GraphAnomaly> anomalies = new ArrayList<>();
    AnomalyDetectionContext.AnomalyDetectionContextBuilder contextWithUpdatedStep =
        context.toBuilder();
    getGraphsWithThresholds()
        .forEach(
            graphWithThreshold -> {
              GraphResponse response =
                  queryNodeMetric(context, graphWithThreshold.getGraphName(), result);
              if (!result.isSuccess()) {
                return;
              }

              long minAnomalySize =
                  Math.max(response.getStepSeconds() * 1000, getMinAnomalySizeMillis());
              contextWithUpdatedStep.stepSeconds(response.getStepSeconds());
              GraphAnomalyDetectionService.AnomalyDetectionSettings detectionSettings =
                  new GraphAnomalyDetectionService.AnomalyDetectionSettings()
                      .setMinimalAnomalyDurationMillis(minAnomalySize);
              detectionSettings
                  .getIncreaseDetectionSettings()
                  .setWindowMinSize(minAnomalySize)
                  .setWindowMaxSize(minAnomalySize * 2);
              detectionSettings
                  .getThresholdExceedSettings()
                  .setThreshold(graphWithThreshold.getThreshold());

              Map<String, List<GraphData>> graphsByLineName =
                  response.getData().stream()
                      .collect(Collectors.groupingBy(GraphData::getName, Collectors.toList()));

              graphsByLineName.forEach(
                  (mode, graphs) ->
                      anomalies.addAll(
                          anomalyDetectionService.getAnomalies(
                              GraphAnomaly.GraphAnomalyType.EXCEED_THRESHOLD,
                              graphs,
                              detectionSettings)));
            });

    if (!result.isSuccess()) {
      return result;
    }

    List<GraphAnomaly> mergedAnomalies = anomalyDetectionService.mergeAnomalies(anomalies);
    createAnomalies(result, mergedAnomalies, contextWithUpdatedStep.build());

    return result;
  }

  @Value
  protected static class GraphWithThreshold {
    String graphName;
    double threshold;
  }
}
