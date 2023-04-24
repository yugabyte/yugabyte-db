package com.yugabyte.yw.controllers;

import static junit.framework.TestCase.fail;

import com.fasterxml.jackson.core.util.DefaultPrettyPrinter;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.ObjectWriter;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.metrics.MetricGrafanaGen;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import lombok.extern.slf4j.Slf4j;
import org.junit.Test;
import play.Environment;
import play.Mode;
import play.libs.Json;

@Slf4j
public class GrafanaGenTest extends FakeDBApplication {

  public Environment environment;

  public GrafanaGenTest() {
    this.environment =
        new Environment(new File("."), Environment.class.getClassLoader(), Mode.TEST);
  }

  @Test
  public void grafanaGen() throws IOException {
    ObjectNode expectedDashboard = getExpectedDashboard();
    ObjectNode currentDashboard = getCurrentDashboard();
    if (currentDashboard == null || !currentDashboard.has("panels")) {
      failDashboardMatch("Grafana dashboard not found!", expectedDashboard);
    } else if (expectedDashboard.get("panels").size() != currentDashboard.get("panels").size()) {
      failDashboardMatch(
          "Number of panels mismatch with expected grafana dashboard!\n", expectedDashboard);
    } else {
      boolean match = expectedDashboard.equals(currentDashboard);
      if (!match) {
        failDashboardMatch(
            "Panels content mismatch with expected grafana dashboard!\n", expectedDashboard);
      }
    }
  }

  public void failDashboardMatch(String failMessage, ObjectNode expectedDashboard) {
    try {
      File outFile = File.createTempFile("Dashboards", "json");
      ObjectMapper mapper = new ObjectMapper();
      ObjectWriter fileWriter = mapper.writer(new DefaultPrettyPrinter());
      fileWriter.writeValue(outFile, expectedDashboard);
      fail(
          "\n============================================================\n"
              + failMessage
              + "Run $sbt grafanaGen to generate a new dashboard\n"
              + "The expected dashboard file written to temp file:\n"
              + outFile.getAbsolutePath()
              + "\n========================================================\n");
    } catch (IOException exception) {
      log.error("Error in writing to Dashboard.json temp file: " + exception);
      fail(
          "\n============================================================\n"
              + failMessage
              + "Run $sbt grafanaGen to generate a new dashboard\n"
              + "Error in writing to Dashboard.json temp file: "
              + exception
              + "\n========================================================\n");
      throw new RuntimeException(exception);
    }
  }

  public ObjectNode getExpectedDashboard() {
    MetricGrafanaGen metricGen = new MetricGrafanaGen(environment);
    ObjectNode expectedDashboard = metricGen.createDashboard();
    return expectedDashboard;
  }

  public ObjectNode getCurrentDashboard() {
    String resourcePath = "metric/Dashboard.json";
    ObjectNode currentDashboard = Json.newObject();
    ObjectMapper mapper = new ObjectMapper();
    try (InputStream is = environment.classLoader().getResourceAsStream(resourcePath)) {
      currentDashboard = mapper.readValue(is, ObjectNode.class);
    } catch (Exception e) {
      log.warn("Error in reading dashboard file: " + e);
    }
    return currentDashboard;
  }

  public static void main(String[] args) throws IOException {
    GrafanaGenTest metricGenTest = new GrafanaGenTest();
    String dashboardGenPath = args[0];
    try {
      metricGenTest.startPlay();
      ObjectNode expectedDashboard = metricGenTest.getExpectedDashboard();
      ObjectNode currentDashboard = metricGenTest.getCurrentDashboard();
      boolean match = expectedDashboard.equals(currentDashboard);
      if (match) {
        log.info("Dashboard has not changed");
        return;
      } else {
        log.info("Generating new dashboard at " + dashboardGenPath);
        MetricGrafanaGen.writeJSON(expectedDashboard, dashboardGenPath);
      }
    } finally {
      metricGenTest.stopPlay();
    }
  }
}
