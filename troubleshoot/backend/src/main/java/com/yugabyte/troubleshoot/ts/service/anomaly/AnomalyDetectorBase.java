package com.yugabyte.troubleshoot.ts.service.anomaly;

import com.yugabyte.troubleshoot.ts.service.GraphService;
import java.time.Instant;
import org.apache.commons.lang3.tuple.ImmutablePair;
import org.apache.commons.lang3.tuple.Pair;

public abstract class AnomalyDetectorBase implements AnomalyDetector {

  protected final GraphService graphService;
  protected final AnomalyMetadataProvider metadataProvider;

  protected final GraphAnomalyDetectionService anomalyDetectionService;

  protected AnomalyDetectorBase(
      GraphService graphService,
      AnomalyMetadataProvider metadataProvider,
      GraphAnomalyDetectionService anomalyDetectionService) {
    this.graphService = graphService;
    this.metadataProvider = metadataProvider;
    this.anomalyDetectionService = anomalyDetectionService;
  }

  protected Pair<Instant, Instant> calculateGraphStartEndTime(
      Instant detectionStartTime,
      Instant detectionEndTime,
      Instant anomalyStartTime,
      Instant anomalyEndTime) {
    long startTime = detectionStartTime.getEpochSecond();
    long endTime = detectionEndTime.getEpochSecond();

    long effectiveAnomalyStartTime =
        anomalyStartTime != null ? anomalyStartTime.getEpochSecond() : startTime;
    long effectiveAnomalyEndTime =
        anomalyEndTime != null ? anomalyEndTime.getEpochSecond() : endTime;
    long anomalyMiddle = (effectiveAnomalyStartTime + effectiveAnomalyEndTime) / 2;
    long anomalySize = effectiveAnomalyEndTime - effectiveAnomalyStartTime;
    long anomalyBasedStartTime = anomalyMiddle - anomalySize * 2;
    long anomalyBasedEndTime = anomalyMiddle + anomalySize * 2;

    if (anomalyBasedStartTime > startTime) {
      startTime = anomalyBasedStartTime;
    }
    if (anomalyBasedEndTime < endTime) {
      endTime = anomalyBasedEndTime;
    }
    return ImmutablePair.of(Instant.ofEpochSecond(startTime), Instant.ofEpochSecond(endTime));
  }
}
