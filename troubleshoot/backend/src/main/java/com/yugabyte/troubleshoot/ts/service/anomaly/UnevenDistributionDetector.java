package com.yugabyte.troubleshoot.ts.service.anomaly;

import com.yugabyte.troubleshoot.ts.models.*;
import com.yugabyte.troubleshoot.ts.service.GraphService;
import java.time.Duration;
import java.util.*;

public abstract class UnevenDistributionDetector extends AnomalyDetectorBase {

  private static final long MIN_ANOMALY_SIZE_MILLIS = Duration.ofMinutes(5).toMillis();

  protected UnevenDistributionDetector(
      GraphService graphService,
      AnomalyMetadataProvider metadataProvider,
      GraphAnomalyDetectionService anomalyDetectionService) {
    super(graphService, metadataProvider, anomalyDetectionService);
  }

  protected abstract double getMinAnomalyValue();

  protected long getMinAnomalySizeMillis() {
    return MIN_ANOMALY_SIZE_MILLIS;
  }

  protected abstract String getGraphName();

  public AnomalyDetectionResult findAnomalies(AnomalyDetectionContext context) {
    AnomalyDetectionResult result = new AnomalyDetectionResult();

    GraphResponse response = queryNodeMetric(context, getGraphName(), result);
    if (!result.isSuccess()) {
      return result;
    }

    AnomalyDetectionContext contextWithUpdatedStep =
        context.toBuilder().stepSeconds(response.getStepSeconds()).build();

    long minAnomalySize = Math.max(response.getStepSeconds() * 1000, getMinAnomalySizeMillis());
    GraphAnomalyDetectionService.AnomalyDetectionSettings detectionSettings =
        new GraphAnomalyDetectionService.AnomalyDetectionSettings()
            .setMinimalAnomalyDurationMillis(minAnomalySize)
            .setMinimalAnomalyValue(getMinAnomalyValue());
    detectionSettings
        .getIncreaseDetectionSettings()
        .setWindowMinSize(minAnomalySize)
        .setWindowMaxSize(minAnomalySize * 2);

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
}
