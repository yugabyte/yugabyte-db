package com.yugabyte.troubleshoot.ts.service.anomaly;

import static com.yugabyte.troubleshoot.ts.models.GraphAnomaly.GraphAnomalyType.INCREASE;
import static com.yugabyte.troubleshoot.ts.models.GraphAnomaly.GraphAnomalyType.UNEVEN_DISTRIBUTION;
import static com.yugabyte.troubleshoot.ts.service.anomaly.GraphAnomalyDetectionService.LINE_NAME;
import static org.assertj.core.api.Assertions.assertThat;

import com.google.common.collect.ImmutableMap;
import com.yugabyte.troubleshoot.ts.CommonUtils;
import com.yugabyte.troubleshoot.ts.models.GraphAnomaly;
import com.yugabyte.troubleshoot.ts.models.GraphData;
import com.yugabyte.troubleshoot.ts.models.GraphPoint;
import java.time.Duration;
import java.util.Collections;
import java.util.List;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.testcontainers.shaded.com.google.common.collect.ImmutableList;

public class GraphAnomalyDetectionServiceTest {

  GraphAnomalyDetectionService.AnomalyDetectionSettings settings;
  GraphAnomalyDetectionService anomalyDetectionService = new GraphAnomalyDetectionService();

  @BeforeEach
  public void setUp() {
    settings = new GraphAnomalyDetectionService.AnomalyDetectionSettings();
    settings
        .getIncreaseDetectionSettings()
        .setWindowMinSize(Duration.ofMinutes(20).toMillis())
        .setWindowMaxSize(Duration.ofMinutes(40).toMillis());
  }

  @Test
  public void testNoAnomalies2Weeks() {
    GraphData graphData = loadGraph("anomaly/no_anomalies_2_weeks.csv");
    List<GraphAnomaly> anomalies =
        anomalyDetectionService.getAnomalies(INCREASE, graphData, settings);
    assertThat(anomalies).isEmpty();
  }

  @Test
  public void testFluctuation2Weeks() {
    GraphData graphData = loadGraph("anomaly/fluctuation_2_weeks.csv");
    List<GraphAnomaly> anomalies =
        anomalyDetectionService.getAnomalies(INCREASE, graphData, settings);
    assertThat(anomalies).isEmpty();
  }

  @Test
  public void testSpike2WeeksAnomalies() {
    GraphData graphData = loadGraph("anomaly/2_to_5_hours_increase_2_weeks.csv");
    List<GraphAnomaly> anomalies =
        anomalyDetectionService.getAnomalies(INCREASE, graphData, settings);
    assertThat(anomalies)
        .containsExactly(
            new GraphAnomaly()
                .setType(INCREASE)
                .setStartTime(1707528960000L)
                .setEndTime(1707543360000L),
            new GraphAnomaly()
                .setType(INCREASE)
                .setStartTime(1708310160000L)
                .setEndTime(1708310160000L),
            new GraphAnomaly()
                .setType(INCREASE)
                .setStartTime(1708515360000L)
                .setEndTime(1708518960000L),
            new GraphAnomaly()
                .setType(INCREASE)
                .setStartTime(1708526160000L)
                .setEndTime(1708529760000L));
  }

  @Test
  public void testSlowIncrease2Weeks() {
    GraphData graphData = loadGraph("anomaly/slow_increase_2_weeks.csv");
    List<GraphAnomaly> anomalies =
        anomalyDetectionService.getAnomalies(INCREASE, graphData, settings);
    assertThat(anomalies)
        .containsExactly(
            new GraphAnomaly()
                .setType(INCREASE)
                .setStartTime(1708151760000L)
                .setEndTime(1708529760000L));
  }

  @Test
  public void testSlowDecrease2Weeks() {
    GraphData graphData = loadGraph("anomaly/slow_decrease_2_weeks.csv");
    List<GraphAnomaly> anomalies =
        anomalyDetectionService.getAnomalies(INCREASE, graphData, settings);
    assertThat(anomalies)
        .containsExactly(
            new GraphAnomaly()
                .setType(INCREASE)
                .setStartTime(1707320160000L)
                .setEndTime(1707698160000L));
  }

  @Test
  public void testIncreaseInTheMiddle2Weeks() {
    GraphData graphData = loadGraph("anomaly/increase_in_the_middle_2_weeks.csv");
    List<GraphAnomaly> anomalies =
        anomalyDetectionService.getAnomalies(INCREASE, graphData, settings);
    assertThat(anomalies)
        .containsExactly(
            new GraphAnomaly()
                .setType(INCREASE)
                .setStartTime(1707924960000L)
                .setEndTime(1708529760000L));
  }

  @Test
  public void testDecreaseInTheMiddle2Weeks() {
    GraphData graphData = loadGraph("anomaly/decrease_in_the_middle_2_weeks.csv");
    List<GraphAnomaly> anomalies =
        anomalyDetectionService.getAnomalies(INCREASE, graphData, settings);
    assertThat(anomalies)
        .containsExactly(
            new GraphAnomaly()
                .setType(INCREASE)
                .setStartTime(1707320160000L)
                .setEndTime(1707921360000L));
  }

  @Test
  public void testNoAnomalies1Day() {
    GraphData graphData = loadGraph("anomaly/no_anomalies_1_day.csv");
    List<GraphAnomaly> anomalies =
        anomalyDetectionService.getAnomalies(INCREASE, graphData, settings);
    assertThat(anomalies).isEmpty();
  }

  @Test
  public void testFluctuation1Day() {
    GraphData graphData = loadGraph("anomaly/fluctuation_1_day.csv");
    List<GraphAnomaly> anomalies =
        anomalyDetectionService.getAnomalies(INCREASE, graphData, settings);
    assertThat(anomalies).isEmpty();
  }

  @Test
  public void testSpike1DayAnomalies() {
    GraphData graphData = loadGraph("anomaly/20_min_to_1_hour_increase_1_day.csv");
    List<GraphAnomaly> anomalies =
        anomalyDetectionService.getAnomalies(INCREASE, graphData, settings);
    assertThat(anomalies)
        .containsExactly(
            new GraphAnomaly()
                .setType(INCREASE)
                .setStartTime(1708450500000L)
                .setEndTime(1708451040000L),
            new GraphAnomaly()
                .setType(INCREASE)
                .setStartTime(1708457040000L)
                .setEndTime(1708458780000L),
            new GraphAnomaly()
                .setType(INCREASE)
                .setStartTime(1708469160000L)
                .setEndTime(1708470540000L),
            new GraphAnomaly()
                .setType(INCREASE)
                .setStartTime(1708471800000L)
                .setEndTime(1708472640000L));
  }

  @Test
  public void testSlowIncrease1Day() {
    GraphData graphData = loadGraph("anomaly/slow_increase_1_day.csv");
    List<GraphAnomaly> anomalies =
        anomalyDetectionService.getAnomalies(INCREASE, graphData, settings);
    assertThat(anomalies)
        .containsExactly(
            new GraphAnomaly()
                .setType(INCREASE)
                .setStartTime(1708503300000L)
                .setEndTime(1708529760000L));
  }

  @Test
  public void testSlowDecrease1Day() {
    GraphData graphData = loadGraph("anomaly/slow_decrease_1_day.csv");
    List<GraphAnomaly> anomalies =
        anomalyDetectionService.getAnomalies(INCREASE, graphData, settings);
    assertThat(anomalies)
        .containsExactly(
            new GraphAnomaly()
                .setType(INCREASE)
                .setStartTime(1708443360000L)
                .setEndTime(1708469280000L));
  }

  @Test
  public void testIncreaseInTheMiddle1Day() {
    GraphData graphData = loadGraph("anomaly/increase_in_the_middle_1_day.csv");
    List<GraphAnomaly> anomalies =
        anomalyDetectionService.getAnomalies(INCREASE, graphData, settings);
    assertThat(anomalies)
        .containsExactly(
            new GraphAnomaly()
                .setType(INCREASE)
                .setStartTime(1708486560000L)
                .setEndTime(1708529760000L));
  }

  @Test
  public void testDecreaseInTheMiddle1Day() {
    GraphData graphData = loadGraph("anomaly/decrease_in_the_middle_1_day.csv");
    List<GraphAnomaly> anomalies =
        anomalyDetectionService.getAnomalies(INCREASE, graphData, settings);
    assertThat(anomalies)
        .containsExactly(
            new GraphAnomaly()
                .setType(INCREASE)
                .setStartTime(1708443360000L)
                .setEndTime(1708486500000L));
  }

  @Test
  public void testUnevenDistributionSpikes() {
    GraphData plain = loadGraph("anomaly/cpu_usage_plain.csv").setName("node1");
    GraphData plain2 = loadGraph("anomaly/cpu_usage_plain_2.csv").setName("node2");
    GraphData spikes = loadGraph("anomaly/cpu_usage_spikes.csv").setName("node3");
    List<GraphAnomaly> anomalies =
        anomalyDetectionService.getAnomalies(
            UNEVEN_DISTRIBUTION, ImmutableList.of(plain, plain2, spikes), settings);
    assertThat(anomalies)
        .containsExactly(
            new GraphAnomaly()
                .setType(UNEVEN_DISTRIBUTION)
                .setStartTime(1708445700000L)
                .setEndTime(1708448100000L)
                .setLabels(ImmutableMap.of(LINE_NAME, Collections.singleton("node3"))),
            new GraphAnomaly()
                .setType(UNEVEN_DISTRIBUTION)
                .setStartTime(1708500300000L)
                .setEndTime(1708507440000L)
                .setLabels(ImmutableMap.of(LINE_NAME, Collections.singleton("node3"))));
  }

  @Test
  public void testUnevenDistributionAlwaysHigh() {
    GraphData plain = loadGraph("anomaly/cpu_usage_plain.csv").setName("node1");
    GraphData plain2 = loadGraph("anomaly/cpu_usage_plain_2.csv").setName("node2");
    GraphData spikes = loadGraph("anomaly/cpu_usage_high.csv").setName("node3");
    List<GraphAnomaly> anomalies =
        anomalyDetectionService.getAnomalies(
            UNEVEN_DISTRIBUTION, ImmutableList.of(plain, plain2, spikes), settings);
    assertThat(anomalies)
        .containsExactly(
            new GraphAnomaly()
                .setType(UNEVEN_DISTRIBUTION)
                .setStartTime(1708443360000L)
                .setEndTime(1708529760000L)
                .setLabels(ImmutableMap.of(LINE_NAME, Collections.singleton("node3"))));
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
