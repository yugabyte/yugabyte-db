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
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeSet;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import org.apache.commons.io.IOUtils;
import org.junit.Assume;
import org.junit.Before;
import org.junit.Test;
import org.yaml.snakeyaml.Yaml;
import play.Environment;
import play.libs.Json;

public class NodeMaintenanceServiceAlertTest extends FakeDBApplication {

  private static final Pattern WINDOW =
      Pattern.compile("yb_node_maintenance_service_last_run_seconds\\{[^)]*\\) > (\\S+)\\)");
  private static final Pattern NODE_LIST_QUERY = Pattern.compile("query \"(.*?)\" }}");

  private Customer customer;
  private Universe universe;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse(customer.getId());
  }

  // The alert fired on a 300s window but listed nodes on an 1800s one, so it paged with no nodes.
  @Test
  public void testAlertAndNodeListUseTheSameWindow() throws Exception {
    Map<String, Object> rule = renderRule();
    assertEquals(Set.of("1800"), windows(rule.get("expr")));
    assertEquals(Set.of("1800"), windows(nodeListLabel(rule)));
  }

  @Test
  public void testHealthyNodeBetweenSamplesDoesNotFire() throws Exception {
    try {
      new ProcessBuilder("promtool", "--version").start().waitFor();
    } catch (IOException e) {
      Assume.assumeTrue("promtool is not on PATH", false);
    }
    Map<String, Object> rule = renderRule();
    String uuid = universe.getUniverseUUID().toString();
    Matcher nodeList = NODE_LIST_QUERY.matcher(nodeListLabel(rule));
    assertTrue(nodeList.find());
    String nodeListQuery = nodeList.group(1).replace("{{ $labels.universe_uuid }}", uuid);

    ObjectNode test = Json.newObject().put("interval", "1m");
    ArrayNode series = test.putArray("input_series");
    // At 40m, n1 last ran 600s ago (healthy, just not sampled lately) and n2 2300s ago.
    node(series, uuid, "n1", "yb-clean_cores.service", 1800);
    node(series, uuid, "n2", "yb-zip_purge_yb_logs.service", 100);
    ArrayNode exprs = test.putArray("promql_expr_test");
    ObjectNode alert = exprs.addObject().put("expr", (String) rule.get("expr"));
    alert.put("eval_time", "40m");
    alert
        .putArray("exp_samples")
        .addObject()
        .put("labels", "{universe_uuid=\"" + uuid + "\"}")
        .put("value", 1);
    ObjectNode nodes = exprs.addObject().put("expr", nodeListQuery).put("eval_time", "40m");
    nodes
        .putArray("exp_samples")
        .addObject()
        .put(
            "labels",
            "{node_name=\"n2\",service=\"yb-zip_purge_yb_logs.service\",universe_uuid=\""
                + uuid
                + "\"}")
        .put("value", 1);

    ObjectNode doc = Json.newObject();
    doc.putArray("rule_files");
    doc.put("evaluation_interval", "1m");
    doc.putArray("tests").add(test);
    File dir = Files.createTempDirectory("node-maintenance-alert").toFile();
    File testFile = new File(dir, "node_maintenance_alert_test.yml");
    Files.writeString(testFile.toPath(), Json.prettyPrint(doc), StandardCharsets.UTF_8);
    Process process =
        new ProcessBuilder("promtool", "test", "rules", testFile.getName())
            .directory(dir)
            .redirectErrorStream(true)
            .start();
    String output = IOUtils.toString(process.getInputStream(), StandardCharsets.UTF_8);
    assertEquals(output, 0, process.waitFor());
  }

  private static void node(ArrayNode series, String uuid, String node, String service, int run) {
    String labels =
        "universe_uuid=\"" + uuid + "\",node_name=\"" + node + "\",service=\"" + service + "\"";
    series
        .addObject()
        .put("series", "yb_node_maintenance_service_status{saved_name=\"status\"," + labels + "}")
        .put("values", "1x45");
    series
        .addObject()
        .put(
            "series",
            "yb_node_maintenance_service_last_run_seconds{saved_name=\"last_run\"," + labels + "}")
        .put("values", run + "x45");
  }

  // Renders the rule file the way SwamperHelper.writeAlertDefinition does.
  @SuppressWarnings("unchecked")
  private Map<String, Object> renderRule() throws IOException {
    AlertTemplate template = AlertTemplate.NODE_MAINTENANCE_SERVICE_NOT_RUN;
    AlertConfiguration configuration =
        ModelFactory.createAlertConfiguration(customer, universe, c -> c.setTemplate(template));
    AlertDefinition definition =
        ModelFactory.createAlertDefinition(customer, universe, configuration);
    AlertRuleTemplateSubstitutor substitutor =
        new AlertRuleTemplateSubstitutor(
            app.injector().instanceOf(RuntimeConfGetter.class),
            app.injector().instanceOf(AlertTemplateService.class).getTemplateDescription(template),
            configuration,
            definition,
            Severity.SEVERE,
            null);
    String file = resource("alert/alert_definition_header.yml");
    file += substitutor.replace(resource("alert/alert_definition_rule.yml"));
    Map<String, Object> doc = new Yaml().load(file);
    Map<String, Object> group = ((List<Map<String, Object>>) doc.get("groups")).get(0);
    return ((List<Map<String, Object>>) group.get("rules")).get(0);
  }

  private String resource(String path) throws IOException {
    try (InputStream in = app.injector().instanceOf(Environment.class).resourceAsStream(path)) {
      return IOUtils.toString(in, StandardCharsets.UTF_8);
    }
  }

  @SuppressWarnings("unchecked")
  private static String nodeListLabel(Map<String, Object> rule) {
    return ((Map<String, String>) rule.get("labels")).get("affected_node_names");
  }

  private static Set<String> windows(Object query) {
    Set<String> result = new TreeSet<>();
    Matcher matcher = WINDOW.matcher((String) query);
    while (matcher.find()) {
      result.add(matcher.group(1));
    }
    return result;
  }
}
