package com.yugabyte.troubleshoot.ts.mock;

import com.fasterxml.jackson.annotation.JsonInclude;
import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.ObjectReader;
import com.fasterxml.jackson.databind.SerializationFeature;
import com.yugabyte.troubleshoot.ts.CommonUtils;
import com.yugabyte.troubleshoot.ts.models.GraphResponse;
import java.io.File;
import java.io.IOException;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;
import org.apache.commons.math3.util.Precision;

public class MockedGraphsGenerator {

  public static void main(String[] args) {
    handleLatencyIncreaseGraph();
    handleCpuDistributionGraph();
  }

  public static void handleLatencyIncreaseGraph() {
    ObjectMapper objectMapper = new ObjectMapper();
    ObjectReader graphsReader = objectMapper.readerFor(new TypeReference<List<GraphResponse>>() {});
    String responseStr = CommonUtils.readResource("mocks/graphs_latency_increase.json");
    long anomalyPeriodStart = Instant.parse("2024-01-18T15:00:00Z").toEpochMilli();
    long anomalyPeriodEnd = Instant.parse("2024-01-18T15:30:00Z").toEpochMilli();
    long anomalyDuration = anomalyPeriodEnd - anomalyPeriodStart;
    long periodStart = anomalyPeriodStart - anomalyDuration * 2;
    long periodEnd = anomalyPeriodEnd + anomalyDuration;
    long step = (periodEnd - periodStart) / 100;
    try {
      List<GraphResponse> graphs = graphsReader.readValue(responseStr);
      for (GraphResponse response : graphs) {
        String name = response.getName();
        double normalValue = 0.0;
        double anomalyValue = 0.0;
        for (GraphResponse.GraphData graphData : response.getData()) {
          switch (name) {
            case "query_latency":
              switch (graphData.getName()) {
                case "Average" -> {
                  normalValue = 50.0;
                  anomalyValue = 150.0;
                }
                case "Mean" -> {
                  normalValue = 40.0;
                  anomalyValue = 130.0;
                }
                case "P90" -> {
                  normalValue = 100.0;
                  anomalyValue = 200.0;
                }
                case "P99" -> {
                  normalValue = 200.0;
                  anomalyValue = 500.0;
                }
              }
              break;
            case "query_rps":
              normalValue = 40.0;
              anomalyValue = 120.0;
              break;
            case "query_rows_avg":
              normalValue = 30.0;
              anomalyValue = 30.0;
              break;
            case "ysql_server_rpc_per_second":
              if (graphData.getName().equals("Select")) {
                normalValue = 200.0;
                anomalyValue = 500.0;
              } else if (graphData.getName().equals("Insert")) {
                normalValue = 50;
                anomalyValue = 50;
              }
              break;
            case "cpu_usage":
              if (graphData.getName().equals("User")) {
                normalValue = 30;
                anomalyValue = 40;
              } else if (graphData.getName().equals("System")) {
                normalValue = 20;
                anomalyValue = 35;
              }
              break;
            case "disk_iops":
              if (graphData.getName().equals("Read")) {
                normalValue = 1000;
                anomalyValue = 1200;
              } else if (graphData.getName().equals("Write")) {
                normalValue = 400;
                anomalyValue = 400;
              }
              break;
            case "disk_bytes_per_second_per_node":
              if (graphData.getName().equals("Read")) {
                normalValue = 1000000;
                anomalyValue = 1200000;
              } else if (graphData.getName().equals("Write")) {
                normalValue = 400000;
                anomalyValue = 400000;
              }
              break;
            case "node_clock_skew":
            case "tserver_rpc_queue_size_tserver":
              normalValue = 1;
              anomalyValue = 1;
              break;
          }
          long point = periodStart;
          graphData.x = new ArrayList<>();
          graphData.y = new ArrayList<>();
          while (point < periodEnd) {
            graphData.x.add(point);
            double lowerBound = normalValue - normalValue * 0.1;
            double upperBound = normalValue + normalValue * 0.1;
            if (point > anomalyPeriodStart && point < anomalyPeriodEnd) {
              lowerBound = anomalyValue - anomalyValue * 0.1;
              upperBound = anomalyValue + anomalyValue * 0.1;
            }
            double value =
                upperBound > lowerBound
                    ? new Random().nextDouble(lowerBound, upperBound)
                    : upperBound;

            graphData.y.add(String.valueOf(Precision.round(value, 2)));
            point += step;
          }
        }
      }
      File file = new File("src/main/resources/mocks/graphs_latency_increase.gen.json");
      objectMapper.setSerializationInclusion(JsonInclude.Include.NON_NULL);
      objectMapper.enable(SerializationFeature.INDENT_OUTPUT);
      objectMapper.writeValue(file, graphs);
    } catch (JsonProcessingException e) {
      throw new RuntimeException("Failed to parse mocked response", e);
    } catch (IOException e) {
      throw new RuntimeException("Failed to write mocked response", e);
    }
  }

  public static void handleCpuDistributionGraph() {
    ObjectMapper objectMapper = new ObjectMapper();
    ObjectReader graphsReader = objectMapper.readerFor(new TypeReference<List<GraphResponse>>() {});
    String responseStr = CommonUtils.readResource("mocks/graphs_cpu_distribution.json");
    long anomalyPeriodStart = Instant.parse("2024-01-19T10:10:00Z").toEpochMilli();
    long anomalyPeriodEnd = Instant.parse("2024-01-19T13:07:18Z").toEpochMilli();
    long anomalyDuration = anomalyPeriodEnd - anomalyPeriodStart;
    long periodStart = anomalyPeriodStart - anomalyDuration * 2;
    long periodEnd = anomalyPeriodEnd;
    long step = (periodEnd - periodStart) / 100;
    try {
      List<GraphResponse> graphs = graphsReader.readValue(responseStr);
      for (GraphResponse response : graphs) {
        String name = response.getName();
        double normalValue = 0.0;
        double anomalyValue = 0.0;
        for (GraphResponse.GraphData graphData : response.getData()) {
          switch (name) {
            case "ysql_server_rpc_per_second":
              if (graphData.getName().equals("Select")) {
                normalValue = 200.0;
                anomalyValue = 200.0;
                if ("yb-15-troubleshooting-service-test-n2".equals(graphData.getInstanceName())) {
                  anomalyValue = 300.0;
                }
              } else if (graphData.getName().equals("Insert")) {
                normalValue = 50;
                anomalyValue = 50;
              }
              break;
            case "cpu_usage":
              if (graphData.getName().equals("User")) {
                normalValue = 30;
                anomalyValue = 30;
                if ("yb-15-troubleshooting-service-test-n2".equals(graphData.getInstanceName())) {
                  anomalyValue = 45;
                }
              } else if (graphData.getName().equals("System")) {
                normalValue = 20;
                anomalyValue = 20;
                if ("yb-15-troubleshooting-service-test-n2".equals(graphData.getInstanceName())) {
                  anomalyValue = 40;
                }
              }
              break;
            case "tserver_rpcs_per_sec_by_universe":
              normalValue = 1000;
              anomalyValue = 1000;
              break;
          }
          long point = periodStart;
          graphData.x = new ArrayList<>();
          graphData.y = new ArrayList<>();
          while (point < periodEnd) {
            graphData.x.add(point);
            double lowerBound = normalValue - normalValue * 0.1;
            double upperBound = normalValue + normalValue * 0.1;
            if (point > anomalyPeriodStart && point < anomalyPeriodEnd) {
              lowerBound = anomalyValue - anomalyValue * 0.1;
              upperBound = anomalyValue + anomalyValue * 0.1;
            }
            double value =
                upperBound > lowerBound
                    ? new Random().nextDouble(lowerBound, upperBound)
                    : upperBound;

            graphData.y.add(String.valueOf(Precision.round(value, 2)));
            point += step;
          }
        }
      }
      File file = new File("src/main/resources/mocks/graphs_cpu_distribution.gen.json");
      objectMapper.setSerializationInclusion(JsonInclude.Include.NON_NULL);
      objectMapper.enable(SerializationFeature.INDENT_OUTPUT);
      objectMapper.writeValue(file, graphs);
    } catch (JsonProcessingException e) {
      throw new RuntimeException("Failed to parse mocked response", e);
    } catch (IOException e) {
      throw new RuntimeException("Failed to write mocked response", e);
    }
  }
}
