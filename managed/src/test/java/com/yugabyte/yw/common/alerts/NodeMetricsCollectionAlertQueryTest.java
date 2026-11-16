// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.alerts;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.AlertTemplate;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.alerts.impl.AlertTemplateService;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertConfiguration.Severity;
import com.yugabyte.yw.models.AlertConfigurationThreshold;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.common.Condition;
import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Map;
import org.apache.commons.io.IOUtils;
import org.junit.Assume;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;

public class NodeMetricsCollectionAlertQueryTest extends FakeDBApplication {

  private Customer customer;
  private Universe universe;
  private String query;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse("vm-alerts", customer.getId());
    AlertTemplate template = AlertTemplate.NODE_METRICS_COLLECTION_FAILURE;
    AlertConfiguration configuration =
        ModelFactory.createAlertConfiguration(
            customer,
            universe,
            c ->
                c.setTemplate(template)
                    .setThresholds(
                        Map.of(
                            Severity.SEVERE,
                            new AlertConfigurationThreshold()
                                .setCondition(Condition.GREATER_THAN)
                                .setThreshold(0D))));
    AlertDefinition definition =
        ModelFactory.createAlertDefinition(customer, universe, configuration);
    query =
        app.injector()
            .instanceOf(AlertTemplateService.class)
            .getTemplateDescription(template)
            .getQueryWithThreshold(
                configuration,
                definition,
                Severity.SEVERE,
                app.injector().instanceOf(RuntimeConfGetter.class));
  }

  @Test
  public void testQueryTargetsTheUniverse() {
    assertTrue(query, query.contains("universe_uuid=\"" + universe.getUniverseUUID() + "\""));
    assertTrue(query, query.endsWith("> 0"));
  }

  @Test
  public void testQueryCountsNodesWithBrokenCollection() throws Exception {
    try {
      new ProcessBuilder("promtool", "--version").start().waitFor();
    } catch (IOException e) {
      Assume.assumeTrue("promtool is not on PATH", false);
    }
    String uuid = universe.getUniverseUUID().toString();
    ObjectNode test = Json.newObject().put("interval", "1m");
    ArrayNode series = test.putArray("input_series");
    // n1: textfile can't be read. n2: file not rewritten since time 0. n3: tserver up, but its
    // metrics are missing from the file. n4: node_exporter has no textfile directory, so the
    // health check collects its metrics instead. n5: healthy.
    for (String node : new String[] {"n1", "n2", "n3", "n4", "n5"}) {
      String labels = "universe_uuid=\"" + uuid + "\",node_name=\"" + node + "\"";
      series(series, "up{export_type=\"node_export\"," + labels + "}", "1x20");
      series(series, "up{export_type=\"tserver_export\"," + labels + "}", "1x20");
      series(
          series,
          "node_textfile_scrape_error{export_type=\"node_export\"," + labels + "}",
          node.equals("n1") ? "1x20" : "0x20");
      series(
          series,
          "yb_node_custom_node_metrics{export_type=\"node_export\"," + labels + "}",
          node.equals("n4") ? "0x20" : "1x20");
      if (!node.equals("n4")) {
        series(
            series,
            "node_textfile_mtime_seconds{file=\"/home/yugabyte/metrics/node_metrics.prom\","
                + "export_type=\"node_export\","
                + labels
                + "}",
            node.equals("n2") ? "0x20" : "0+60x20");
      }
      if (!node.equals("n3") && !node.equals("n4")) {
        series(
            series,
            "yb_node_boot_time{process=\"tserver\",export_type=\"node_export\"," + labels + "}",
            "1x20");
      }
    }
    ObjectNode expr = test.putArray("promql_expr_test").addObject();
    expr.put("expr", query).put("eval_time", "15m");
    expr.putArray("exp_samples")
        .addObject()
        .put("labels", "{universe_uuid=\"" + uuid + "\"}")
        .put("value", 3);

    ObjectNode doc = Json.newObject();
    doc.putArray("rule_files");
    doc.put("evaluation_interval", "1m");
    doc.putArray("tests").add(test);
    File dir = Files.createTempDirectory("node-metrics-alert-query").toFile();
    File testFile = new File(dir, "alert_query_test.yml");
    Files.writeString(testFile.toPath(), Json.prettyPrint(doc), StandardCharsets.UTF_8);
    Process process =
        new ProcessBuilder("promtool", "test", "rules", testFile.getName())
            .directory(dir)
            .redirectErrorStream(true)
            .start();
    String output = IOUtils.toString(process.getInputStream(), StandardCharsets.UTF_8);
    assertEquals(output, 0, process.waitFor());
  }

  private static void series(ArrayNode series, String name, String values) {
    series.addObject().put("series", name).put("values", values);
  }
}
