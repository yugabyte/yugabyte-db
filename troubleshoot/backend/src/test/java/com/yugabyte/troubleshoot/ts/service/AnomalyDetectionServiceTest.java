package com.yugabyte.troubleshoot.ts.service;

import static com.yugabyte.troubleshoot.ts.models.GraphAnomaly.GraphAnomalyType.INCREASE;
import static com.yugabyte.troubleshoot.ts.models.GraphAnomaly.GraphAnomalyType.UNEVEN_DISTRIBUTION;
import static org.assertj.core.api.Assertions.assertThat;

import com.yugabyte.troubleshoot.ts.CommonUtils;
import com.yugabyte.troubleshoot.ts.models.GraphAnomaly;
import com.yugabyte.troubleshoot.ts.models.GraphData;
import com.yugabyte.troubleshoot.ts.models.GraphPoint;
import java.util.List;
import org.junit.jupiter.api.Test;
import org.testcontainers.shaded.com.google.common.collect.ImmutableList;

public class AnomalyDetectionServiceTest {

  AnomalyDetectionService anomalyDetectionService = new AnomalyDetectionService();

  @Test
  public void testNoAnomalies2Weeks() {
    GraphData graphData = loadGraph("anomaly/no_anomalies_2_weeks.csv");
    List<GraphAnomaly> anomalies = anomalyDetectionService.getAnomalies(INCREASE, graphData);
    assertThat(anomalies).isEmpty();
  }

  @Test
  public void testFluctuation2Weeks() {
    GraphData graphData = loadGraph("anomaly/fluctuation_2_weeks.csv");
    List<GraphAnomaly> anomalies = anomalyDetectionService.getAnomalies(INCREASE, graphData);
    assertThat(anomalies).isEmpty();
  }

  @Test
  public void testSpike2WeeksAnomalies() {
    GraphData graphData = loadGraph("anomaly/2_to_5_hours_increase_2_weeks.csv");
    List<GraphAnomaly> anomalies = anomalyDetectionService.getAnomalies(INCREASE, graphData);
    assertThat(anomalies)
        .containsExactly(
            new GraphAnomaly().setType(INCREASE).setStartTime(1707528960L).setEndTime(1707543360L),
            new GraphAnomaly().setType(INCREASE).setStartTime(1708310160L).setEndTime(1708310160L),
            new GraphAnomaly().setType(INCREASE).setStartTime(1708515360L).setEndTime(1708518960L),
            new GraphAnomaly().setType(INCREASE).setStartTime(1708526160L));
  }

  @Test
  public void testSlowIncrease2Weeks() {
    GraphData graphData = loadGraph("anomaly/slow_increase_2_weeks.csv");
    List<GraphAnomaly> anomalies = anomalyDetectionService.getAnomalies(INCREASE, graphData);
    assertThat(anomalies)
        .containsExactly(new GraphAnomaly().setType(INCREASE).setStartTime(1708151760L));
  }

  @Test
  public void testSlowDecrease2Weeks() {
    GraphData graphData = loadGraph("anomaly/slow_decrease_2_weeks.csv");
    List<GraphAnomaly> anomalies = anomalyDetectionService.getAnomalies(INCREASE, graphData);
    assertThat(anomalies)
        .containsExactly(
            new GraphAnomaly().setType(INCREASE).setStartTime(1707320160L).setEndTime(1707698160L));
  }

  @Test
  public void testIncreaseInTheMiddle2Weeks() {
    GraphData graphData = loadGraph("anomaly/increase_in_the_middle_2_weeks.csv");
    List<GraphAnomaly> anomalies = anomalyDetectionService.getAnomalies(INCREASE, graphData);
    assertThat(anomalies)
        .containsExactly(new GraphAnomaly().setType(INCREASE).setStartTime(1707924960L));
  }

  @Test
  public void testDecreaseInTheMiddle2Weeks() {
    GraphData graphData = loadGraph("anomaly/decrease_in_the_middle_2_weeks.csv");
    List<GraphAnomaly> anomalies = anomalyDetectionService.getAnomalies(INCREASE, graphData);
    assertThat(anomalies)
        .containsExactly(
            new GraphAnomaly().setType(INCREASE).setStartTime(1707320160L).setEndTime(1707921360L));
  }

  @Test
  public void testNoAnomalies1Day() {
    GraphData graphData = loadGraph("anomaly/no_anomalies_1_day.csv");
    List<GraphAnomaly> anomalies = anomalyDetectionService.getAnomalies(INCREASE, graphData);
    assertThat(anomalies).isEmpty();
  }

  @Test
  public void testFluctuation1Day() {
    GraphData graphData = loadGraph("anomaly/fluctuation_1_day.csv");
    List<GraphAnomaly> anomalies = anomalyDetectionService.getAnomalies(INCREASE, graphData);
    assertThat(anomalies).isEmpty();
  }

  @Test
  public void testSpike1DayAnomalies() {
    GraphData graphData = loadGraph("anomaly/20_min_to_1_hour_increase_1_day.csv");
    List<GraphAnomaly> anomalies = anomalyDetectionService.getAnomalies(INCREASE, graphData);
    assertThat(anomalies)
        .containsExactly(
            new GraphAnomaly().setType(INCREASE).setStartTime(1708450500L).setEndTime(1708451040L),
            new GraphAnomaly().setType(INCREASE).setStartTime(1708457040L).setEndTime(1708458780L),
            new GraphAnomaly().setType(INCREASE).setStartTime(1708469160L).setEndTime(1708472640L));
  }

  @Test
  public void testSlowIncrease1Day() {
    GraphData graphData = loadGraph("anomaly/slow_increase_1_day.csv");
    List<GraphAnomaly> anomalies = anomalyDetectionService.getAnomalies(INCREASE, graphData);
    assertThat(anomalies)
        .containsExactly(new GraphAnomaly().setType(INCREASE).setStartTime(1708503300L));
  }

  @Test
  public void testSlowDecrease1Day() {
    GraphData graphData = loadGraph("anomaly/slow_decrease_1_day.csv");
    List<GraphAnomaly> anomalies = anomalyDetectionService.getAnomalies(INCREASE, graphData);
    assertThat(anomalies)
        .containsExactly(
            new GraphAnomaly().setType(INCREASE).setStartTime(1708443360L).setEndTime(1708469280L));
  }

  @Test
  public void testIncreaseInTheMiddle1Day() {
    GraphData graphData = loadGraph("anomaly/increase_in_the_middle_1_day.csv");
    List<GraphAnomaly> anomalies = anomalyDetectionService.getAnomalies(INCREASE, graphData);
    assertThat(anomalies)
        .containsExactly(new GraphAnomaly().setType(INCREASE).setStartTime(1708486560L));
  }

  @Test
  public void testDecreaseInTheMiddle1Day() {
    GraphData graphData = loadGraph("anomaly/decrease_in_the_middle_1_day.csv");
    List<GraphAnomaly> anomalies = anomalyDetectionService.getAnomalies(INCREASE, graphData);
    assertThat(anomalies)
        .containsExactly(
            new GraphAnomaly().setType(INCREASE).setStartTime(1708443360L).setEndTime(1708486500L));
  }

  @Test
  public void testUnevenDistributionSpikes() {
    GraphData plain = loadGraph("anomaly/cpu_usage_plain.csv").setName("node1");
    GraphData plain2 = loadGraph("anomaly/cpu_usage_plain_2.csv").setName("node2");
    GraphData spikes = loadGraph("anomaly/cpu_usage_spikes.csv").setName("node3");
    List<GraphAnomaly> anomalies =
        anomalyDetectionService.getAnomalies(
            UNEVEN_DISTRIBUTION, ImmutableList.of(plain, plain2, spikes));
    assertThat(anomalies)
        .containsExactly(
            new GraphAnomaly()
                .setGraphName("node3")
                .setType(UNEVEN_DISTRIBUTION)
                .setStartTime(1708445700L)
                .setEndTime(1708448100L),
            new GraphAnomaly()
                .setGraphName("node3")
                .setType(UNEVEN_DISTRIBUTION)
                .setStartTime(1708500300L)
                .setEndTime(1708507440L));
  }

  @Test
  public void testUnevenDistributionAlwaysHigh() {
    GraphData plain = loadGraph("anomaly/cpu_usage_plain.csv").setName("node1");
    GraphData plain2 = loadGraph("anomaly/cpu_usage_plain_2.csv").setName("node2");
    GraphData spikes = loadGraph("anomaly/cpu_usage_high.csv").setName("node3");
    List<GraphAnomaly> anomalies =
        anomalyDetectionService.getAnomalies(
            UNEVEN_DISTRIBUTION, ImmutableList.of(plain, plain2, spikes));
    assertThat(anomalies)
        .containsExactly(
            new GraphAnomaly()
                .setGraphName("node3")
                .setType(UNEVEN_DISTRIBUTION)
                .setStartTime(1708443360L));
  }

  private GraphData loadGraph(String resourcePath) {
    String graphDataString = CommonUtils.readResource(resourcePath);
    GraphData graphData = new GraphData();
    for (String entryStr : graphDataString.split("\n")) {
      if (entryStr.isBlank()) {
        break;
      }
      String[] parts = entryStr.split(",");
      if (parts.length < 2) {
        break;
      }
      graphData
          .getPoints()
          .add(new GraphPoint().setX(Long.parseLong(parts[0])).setY(Double.parseDouble(parts[1])));
    }
    return graphData;
  }
}
