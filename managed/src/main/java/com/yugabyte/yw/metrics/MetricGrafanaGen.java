package com.yugabyte.yw.metrics;

import static com.yugabyte.yw.models.MetricConfig.METRICS_CONFIG_PATH;

import com.fasterxml.jackson.core.util.DefaultPrettyPrinter;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.ObjectWriter;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.models.MetricConfig;
import com.yugabyte.yw.models.MetricConfigDefinition;
import com.yugabyte.yw.models.MetricConfigDefinition.Layout;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.SortedMap;
import java.util.TreeMap;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.yaml.snakeyaml.LoaderOptions;
import org.yaml.snakeyaml.Yaml;
import org.yaml.snakeyaml.constructor.CustomClassLoaderConstructor;
import play.Environment;
import play.libs.Json;

@Slf4j
public class MetricGrafanaGen {

  private final Environment environment;

  public static final String DASHBOARD_TEMPLATE_PATH = "metric/grafana_dashboard_template.json";
  public static final String PANEL_TEMPLATE_PATH = "metric/grafana_panel_template.json";
  public static final String MASTER_STATUS_PANEL_TEMPLATE_PATH =
      "metric/grafana_master_status_template.json";
  public static final String TSERVER_STATUS_PANEL_TEMPLATE_PATH =
      "metric/grafana_tserver_status_template.json";
  public static final String GROUP_HEADER_TEMPLATE = "metric/group_header_template.json";
  public static int DEFAULT_RANGE_SECS = 300;

  public MetricGrafanaGen(Environment environment) {
    this.environment = environment;
  }

  public static void writeJSON(ObjectNode grafanaJson, String dashboardGenPath) {
    ObjectMapper mapper = new ObjectMapper();
    try {
      File grafanaFile = new File(dashboardGenPath);
      grafanaFile.createNewFile();
      ObjectWriter fileWriter = mapper.writer(new DefaultPrettyPrinter("\n"));
      fileWriter.writeValue(grafanaFile, grafanaJson);
    } catch (IOException e) {
      log.error("Error in writing to Dashboards file: {}", e);
      throw new RuntimeException(e);
    }
  }

  public void loadMetricConfigs() {
    LoaderOptions loaderOptions = new LoaderOptions();

    try (InputStream is = environment.classLoader().getResourceAsStream(METRICS_CONFIG_PATH)) {
      Yaml yaml =
          new Yaml(new CustomClassLoaderConstructor(environment.classLoader(), loaderOptions));

      Map<String, Object> configs = (HashMap<String, Object>) yaml.load(is);
      MetricConfig.loadConfig(configs);
    } catch (Exception exception) {
      throw new RuntimeException(exception);
    }
  }

  /**
   * This method constructs the grafana dashboard for all metrics in metrics.yml. It uses a template
   * for creating a dashboard and adds panels for each metric to it.
   */
  public ObjectNode createDashboard() {
    ObjectNode grafanaJson = createTemplate(DASHBOARD_TEMPLATE_PATH);
    ArrayNode panels = Json.newArray();
    ObjectMapper mapper = new ObjectMapper();
    ObjectNode panelTemplate = createTemplate(PANEL_TEMPLATE_PATH);

    // Load all configs
    loadMetricConfigs();

    // Stores panels groupwise sorted
    SortedMap<String, ArrayList<ObjectNode>> groupedPanels =
        new TreeMap<String, ArrayList<ObjectNode>>();

    List<String> allMetrics =
        MetricConfig.find.all().stream()
            .map(MetricConfig::getKey)
            .sorted()
            .collect(Collectors.toList());

    int id = 0;
    for (String metric_key : allMetrics) {
      MetricConfig metricConfig = MetricConfig.get(metric_key);
      MetricConfigDefinition.Layout layout = metricConfig.getConfig().getLayout();
      String title = layout.getTitle();
      String panel_group = metricConfig.getConfig().getPanelGroup();
      if (panel_group == null) {
        panel_group = "Misc";
      }
      if (title == null) {
        title = metric_key;
      }
      String ticksuffix = "";
      if (layout.getYaxis() != null && layout.getYaxis().getTicksuffix() != null) {
        ticksuffix = layout.getYaxis().getTicksuffix();
        String[] ticksplit = ticksuffix.split(";");
        if (ticksplit.length > 1) {
          ticksuffix = ticksplit[1];
        }
        // avoid micro character linting errors
        if (ticksuffix.charAt(0) == 181) {
          ticksuffix = "us";
        }
      }
      ObjectNode panel = panelTemplate.deepCopy();
      ArrayNode targets = createTargetsVariable(mapper, metricConfig);
      panel.put("title", title);
      panel.put("id", ++id);
      panel.put("targets", targets);
      ((ObjectNode) panel.path("yaxes").get(0)).put("format", ticksuffix);
      groupedPanels.computeIfAbsent(panel_group, group -> new ArrayList<ObjectNode>()).add(panel);
    }

    ObjectNode masterStatusPanel = createTemplate(MASTER_STATUS_PANEL_TEMPLATE_PATH);
    ObjectNode tserverStatusPanel = createTemplate(TSERVER_STATUS_PANEL_TEMPLATE_PATH);
    masterStatusPanel.put("id", ++id);
    tserverStatusPanel.put("id", ++id);
    panels.add(masterStatusPanel);
    panels.add(tserverStatusPanel);

    // Add panels according to groups and set their gridPos
    Map<String, Integer> gridPos;
    int x = 12, y = 36;
    for (String panel_group : groupedPanels.keySet()) {
      // add group header
      id += 1;
      gridPos = getNextGridPos(x, y, "header");
      x = gridPos.get("x");
      y = gridPos.get("y");
      ObjectNode header = createTemplate(GROUP_HEADER_TEMPLATE);
      header.put("title", panel_group);
      header.put("id", id);
      header.put("gridPos", mapper.valueToTree(gridPos));
      panels.add(header);
      // add group panels
      for (ObjectNode panel : groupedPanels.get(panel_group)) {
        gridPos = getNextGridPos(x, y, "panel");
        x = gridPos.get("x");
        y = gridPos.get("y");
        panel.put("gridPos", mapper.valueToTree(gridPos));
        panels.add(panel);
      }
    }

    grafanaJson.put("panels", panels);
    return grafanaJson;
  }

  public ObjectNode createTemplate(String templatePath) {
    ObjectNode template;
    ObjectMapper mapper = new ObjectMapper();
    try (InputStream templateStream = environment.classLoader().getResourceAsStream(templatePath)) {
      template = mapper.readValue(templateStream, ObjectNode.class);
    } catch (Exception e) {
      log.error("Error in reading template file: {}", e);
      throw new RuntimeException(e);
    }
    return template;
  }

  /**
   * This method creates the target variable which has the query expressions and the legend format
   * variable. It uses the MetricConfig class to generate queries for a metric.
   */
  public ArrayNode createTargetsVariable(ObjectMapper mapper, MetricConfig metricConfig) {

    ArrayNode targets = Json.newArray();
    Map<String, String> queries = new HashMap<>();
    Map<String, String> additionalFilters = new HashMap<>();
    additionalFilters.put("node_prefix", "$dbcluster");
    MetricConfigDefinition configDefinition = metricConfig.getConfig();
    Layout layout = configDefinition.getLayout();
    String group_by_filter = configDefinition.getGroupBy();

    // Case 1: Queries created for each "|" separated filter value
    if (group_by_filter != null
        && !group_by_filter.contains(",")
        && configDefinition.getFilters().containsKey(group_by_filter)) {
      Map<String, String> filters = configDefinition.getFilters();
      String groupedFilters = filters.get(group_by_filter);
      String[] filterValues = new String[] {};
      if (groupedFilters != null) {
        filterValues = groupedFilters.split("\\|");
      }
      // get query for each filterValue
      for (String filterValue : filterValues) {
        additionalFilters.put(group_by_filter, filterValue);
        Map<String, String> filterQueries =
            configDefinition.getQueries(additionalFilters, DEFAULT_RANGE_SECS);
        for (String key : filterQueries.keySet()) {
          queries.put(key + "|" + filterValue, filterQueries.get(key));
        }
      }
      // set values in target variable for each query
      char refID = 'A';
      for (String queryKey : queries.keySet()) {
        String query = queries.get(queryKey);
        String filterValue = queryKey.split("\\|")[1];
        String legendFormat = "";
        Map<String, Object> targetVar = new HashMap<String, Object>();
        targetVar.put("expr", query);
        if (refID <= 'Z') {
          targetVar.put("refId", refID);
          refID += 1;
        }
        targetVar.put("hide", false);
        legendFormat = "";

        if (layout.getYaxis() != null && layout.getYaxis().getAlias() != null) {
          legendFormat = layout.getYaxis().getAlias().get(filterValue);
        }
        if (StringUtils.isBlank(legendFormat)) {
          legendFormat = filterValue;
        }
        targetVar.put("legendFormat", legendFormat);
        targets.add(mapper.valueToTree(targetVar));
      }
    } else {
      // Case 2: No "|" separated filters present
      queries = configDefinition.getQueries(additionalFilters, DEFAULT_RANGE_SECS);
      char refID = 'A';
      for (String metric : queries.keySet()) {
        String query = queries.get(metric);
        String legendFormat = "";
        Map<String, Object> targetVar = new HashMap<String, Object>();
        targetVar.put("expr", query);
        if (refID <= 'Z') {
          targetVar.put("refId", refID);
          refID += 1;
        }
        targetVar.put("hide", false);
        if (layout.getYaxis() != null && layout.getYaxis().getAlias() != null) {
          legendFormat = layout.getYaxis().getAlias().get(metric);
        }
        targetVar.put("legendFormat", legendFormat);
        targets.add(mapper.valueToTree(targetVar));
      }
    }
    return targets;
  }

  Map<String, Integer> getNextGridPos(int x, int y, String type) {
    Map<String, Integer> gridPos = new HashMap<String, Integer>();
    int h = 9, w = 12;
    if (type.equals("panel")) {
      if (x == 0) {
        x = 12;
      } else {
        x = 0;
        y += 9;
      }
    } else if (type.equals("header")) {
      x = 0;
      y += 1;
      h = 1;
      w = 24;
    }
    gridPos.put("h", h);
    gridPos.put("w", w);
    gridPos.put("x", x);
    gridPos.put("y", y);
    return gridPos;
  }
}
