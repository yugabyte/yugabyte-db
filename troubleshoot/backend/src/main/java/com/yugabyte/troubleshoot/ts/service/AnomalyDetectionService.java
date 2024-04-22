package com.yugabyte.troubleshoot.ts.service;

import com.google.common.collect.ImmutableList;
import com.yugabyte.troubleshoot.ts.models.GraphAnomaly;
import com.yugabyte.troubleshoot.ts.models.GraphData;
import com.yugabyte.troubleshoot.ts.models.GraphPoint;
import java.time.Duration;
import java.util.*;
import java.util.function.Supplier;
import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.Getter;
import lombok.Value;
import lombok.experimental.Accessors;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.tuple.MutablePair;
import org.apache.commons.lang3.tuple.Pair;
import org.springframework.stereotype.Service;

@Service
public class AnomalyDetectionService {

  IncreaseDetectionSettings DEFAULT_INCREASE_DETECTION_SETTINGS = new IncreaseDetectionSettings();
  IncreaseDetectionSettings UNEVEN_DISTRIBUTION_DETECTION_SETTINGS =
      new IncreaseDetectionSettings()
          .setAnomalyType(GraphAnomaly.GraphAnomalyType.UNEVEN_DISTRIBUTION)
          .setWindowMinSize(Duration.ofHours(1).toSeconds())
          .setWindowMaxSize(Duration.ofHours(2).toSeconds());

  public List<GraphAnomaly> getAnomalies(GraphAnomaly.GraphAnomalyType type, GraphData graphData) {
    return getAnomalies(type, ImmutableList.of(graphData));
  }

  public List<GraphAnomaly> getAnomalies(
      GraphAnomaly.GraphAnomalyType type, List<GraphData> graphDatas) {

    List<GraphAnomaly> result = new ArrayList<>();
    switch (type) {
      case INCREASE:
        for (GraphData graphData : graphDatas) {
          if (CollectionUtils.isEmpty(graphData.getPoints())) {
            continue;
          }
          GraphData baseline = getIncreaseBaseline(graphData, DEFAULT_INCREASE_DETECTION_SETTINGS);
          result.addAll(getIncreases(baseline, graphData, DEFAULT_INCREASE_DETECTION_SETTINGS));
        }
        break;
      case UNEVEN_DISTRIBUTION:
        List<GraphData> baselines = getUnevenDistributionBaselines(graphDatas);
        for (int i = 0; i < graphDatas.size(); i++) {
          GraphData graphData = graphDatas.get(i);
          GraphData baseline = baselines.get(i);
          if (CollectionUtils.isEmpty(graphData.getPoints())) {
            continue;
          }
          result.addAll(getIncreases(baseline, graphData, UNEVEN_DISTRIBUTION_DETECTION_SETTINGS));
        }
        break;
      default:
        throw new RuntimeException("Unsupported anomaly type " + type.name());
    }
    return result;
  }

  private List<GraphData> getUnevenDistributionBaselines(List<GraphData> graphDatas) {
    // We're assuming all graphs have the same timestamps, except that
    // some graphs may have less timestamps than the others
    Map<Long, MutablePair<Double, Integer>> timestampToSumCountMap = new HashMap<>();
    graphDatas.stream()
        .flatMap(gd -> gd.getPoints().stream())
        .forEach(
            pt -> {
              if (pt.getY().isInfinite() || pt.getY().isNaN()) {
                return;
              }
              MutablePair<Double, Integer> sumCount = timestampToSumCountMap.get(pt.getX());
              if (sumCount == null) {
                sumCount = new MutablePair<>(pt.getY(), 1);
                timestampToSumCountMap.put(pt.getX(), sumCount);
              } else {
                sumCount.setLeft(sumCount.getLeft() + pt.getY());
                sumCount.setRight(sumCount.getRight() + 1);
              }
            });
    List<GraphData> result = new ArrayList<>();
    for (GraphData graphData : graphDatas) {
      GraphData baseline = new GraphData();
      result.add(baseline);
      for (GraphPoint pt : graphData.getPoints()) {
        Pair<Double, Integer> sumCount = timestampToSumCountMap.get(pt.getX());
        GraphPoint baselinePoint = new GraphPoint().setX(pt.getX()).setY(pt.getY());
        if (sumCount != null
            && sumCount.getRight() > 1
            && !pt.getY().isInfinite()
            && !pt.getY().isNaN()) {
          baselinePoint.setY((sumCount.getLeft() - pt.getY()) / (sumCount.getRight() - 1));
        }
        baseline.getPoints().add(baselinePoint);
      }
    }
    return result;
  }

  private GraphData getIncreaseBaseline(GraphData graphData, IncreaseDetectionSettings settings) {
    List<Double> allPoints = new ArrayList<>();
    int baselineMaxSize = (int) (graphData.getPoints().size() * settings.baselinePointsRatio);
    for (int i = 0; i < graphData.getPoints().size(); i++) {
      Double value = graphData.getPoints().get(i).getY();
      if (value.isNaN() || value.isInfinite()) {
        continue;
      }
      allPoints.add(value);
    }
    if (allPoints.isEmpty()) {
      return null;
    }
    Collections.sort(allPoints);
    List<Double> baselinePoints = allPoints.stream().limit(baselineMaxSize).toList();
    Double average =
        baselinePoints.stream().mapToDouble(Double::doubleValue).sum() / baselinePoints.size();
    return new GraphData()
        .setPoints(
            graphData.getPoints().stream()
                .map(p -> new GraphPoint().setX(p.getX()).setY(average))
                .toList());
  }

  private List<GraphAnomaly> getIncreases(
      GraphData beselineGraph, GraphData graphData, IncreaseDetectionSettings settings) {
    SlidingWindow currentWindow = new SlidingWindow();
    Long currentAnomalyStartTime = null;
    List<GraphAnomaly> result = new ArrayList<>();
    for (int i = 0; i < graphData.getPoints().size(); i++) {
      GraphPoint graphPoint = graphData.getPoints().get(i);
      Double value = graphPoint.getY();
      Double baseline = beselineGraph.getPoints().get(i).getY();
      if (value.isNaN() || value.isInfinite()) {
        // For simplicity, we assume this value as 0 -
        // as that's how we show that on the graph typically;
        value = 0.0;
      }
      Long timestamp = graphPoint.getX();
      if (currentWindow.isEmpty()) {
        if (value > baseline * settings.thresholdRatio) {
          currentWindow.addPoint(timestamp, value, baseline);
        }
        // Until we hit increase threshold - w don't start window.
      } else {
        Long currentWindowStartTime = currentWindow.getStartTime();
        if (timestamp - currentWindowStartTime < settings.windowMinSize) {
          // Just add point to the window
          currentWindow.addPoint(timestamp, value, baseline);
        } else {
          if (currentAnomalyStartTime == null) {
            if (currentWindow.getWindowAverage()
                > currentWindow.getBaselineAverage() * settings.thresholdRatio) {
              // Start an anomaly
              currentAnomalyStartTime = currentWindowStartTime;
              // Continue to grow window until max size is reached.
              currentWindow.addPoint(timestamp, value, baseline);
            } else {
              // Move window further to the next value > threshold
              currentWindow.addPoint(timestamp, value, baseline);
              do {
                currentWindow.removeFirstPoint();
              } while (!currentWindow.isEmpty()
                  && currentWindow.getFirstValue()
                      <= currentWindow.getFirstBaselineValue() * settings.thresholdRatio);
            }
          } else {
            if (timestamp - currentWindowStartTime < settings.windowMaxSize) {
              // Just add point to the window
              currentWindow.addPoint(timestamp, value, baseline);
            } else {
              if (currentWindow.getWindowAverage()
                  > currentWindow.getBaselineAverage() * settings.thresholdRatio) {
                // Move window further
                currentWindow.addPointAndMove(timestamp, value, baseline);
              } else {
                // We faced anomaly end.
                // Need to rewind window back to the first point, higher than threshold.
                do {
                  currentWindow.removeLastPoint();
                  i--;
                } while (!currentWindow.isEmpty()
                    && currentWindow.getLastValue()
                        <= currentWindow.getLastBaselineValue() * settings.thresholdRatio);
                result.add(
                    new GraphAnomaly()
                        .setGraphName(graphData.getName())
                        .setType(settings.getAnomalyType())
                        .setStartTime(currentAnomalyStartTime)
                        .setEndTime(currentWindow.getEndTime()));
                currentAnomalyStartTime = null;
                currentWindow.clear();
              }
            }
          }
        }
      }
    }
    // Anomaly end is in the end of graph
    if (currentAnomalyStartTime != null) {
      result.add(
          new GraphAnomaly()
              .setGraphName(graphData.getName())
              .setType(settings.getAnomalyType())
              .setStartTime(currentAnomalyStartTime));
    }
    return result;
  }

  @Getter
  public static class SlidingWindow {
    private final LinkedList<WindowEntry> window = new LinkedList<>();
    private Double windowAverage = 0.0;
    private Double baselineAverage = 0.0;

    public void addPoint(Long timestamp, Double value, Double baseline) {
      window.add(new WindowEntry(timestamp, value, baseline));
      windowAverage = (windowAverage * (window.size() - 1) + value) / window.size();
      baselineAverage = (baselineAverage * (window.size() - 1) + baseline) / window.size();
    }

    public void addPointAndMove(Long timestamp, Double value, Double baseline) {
      addPoint(timestamp, value, baseline);
      removeFirstPoint();
    }

    public WindowEntry removeLastPoint() {
      return removePoint(window::pollLast);
    }

    public WindowEntry removeFirstPoint() {
      return removePoint(window::pollFirst);
    }

    public boolean isEmpty() {
      return window.isEmpty();
    }

    public Long getStartTime() {
      if (isEmpty()) {
        return null;
      }
      return window.getFirst().getTimestamp();
    }

    public Long getEndTime() {
      if (isEmpty()) {
        return null;
      }
      return window.getLast().getTimestamp();
    }

    public Double getFirstValue() {
      if (isEmpty()) {
        return null;
      }
      return window.getFirst().getValue();
    }

    public Double getLastValue() {
      if (isEmpty()) {
        return null;
      }
      return window.getLast().getValue();
    }

    public Double getFirstBaselineValue() {
      if (isEmpty()) {
        return null;
      }
      return window.getFirst().getBaseline();
    }

    public Double getLastBaselineValue() {
      if (isEmpty()) {
        return null;
      }
      return window.getLast().getBaseline();
    }

    public void clear() {
      window.clear();
      windowAverage = 0.0;
      baselineAverage = 0.0;
    }

    private WindowEntry removePoint(Supplier<WindowEntry> removeFunction) {
      if (window.isEmpty()) {
        return null;
      }
      WindowEntry removed = removeFunction.get();
      if (window.isEmpty()) {
        windowAverage = 0.0;
        baselineAverage = 0.0;
      } else {
        windowAverage = (windowAverage * (window.size() + 1) - removed.getValue()) / window.size();
        baselineAverage =
            (baselineAverage * (window.size() + 1) - removed.getBaseline()) / window.size();
      }
      return removed;
    }
  }

  @Value
  @AllArgsConstructor
  private static class WindowEntry {
    long timestamp;
    double value;
    double baseline;
  }

  @Data
  @Accessors(chain = true)
  public static class IncreaseDetectionSettings {
    GraphAnomaly.GraphAnomalyType anomalyType = GraphAnomaly.GraphAnomalyType.INCREASE;
    double baselinePointsRatio = 0.25;
    double thresholdRatio = 1.5;
    long windowMinSize = Duration.ofMinutes(30).toSeconds();
    long windowMaxSize = Duration.ofHours(1).toSeconds();
  }
}
