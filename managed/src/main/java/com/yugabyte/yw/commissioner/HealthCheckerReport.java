/*
 * Copyright 2020 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.core.util.DefaultIndenter;
import com.fasterxml.jackson.core.util.DefaultPrettyPrinter;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Universe;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;
import lombok.AllArgsConstructor;
import lombok.Getter;
import lombok.Setter;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Singleton
public class HealthCheckerReport {

  public static final Logger LOG = LoggerFactory.getLogger(HealthCheckerReport.class);

  private static final String BADGE_TEMPLATE =
      "<span style=\"font-size:0.8em;"
          + "background-color:%s;color:%s;border-radius:2px;margin-right:8px;"
          + "padding:2px 4px;font-weight:400;\">\n%s</span>\n";

  private static final String ERROR_BADGE =
      String.format(BADGE_TEMPLATE, "#E8473F", "#ffffff", "Failed");

  private static final String WARNING_BADGE =
      String.format(BADGE_TEMPLATE, "#EBEB3E", "#000000", "Warning");

  private static final String STYLE_FONT =
      "font-family: SF Pro Display, SF Pro, Helvetica Neue, Helvetica, sans-serif;";

  private static final String H2_STYLE =
      String.format(
          "font-weight:700;line-height:1em;color:#202951;font-size:2.5em;margin:0;\n%s",
          STYLE_FONT);

  private static final String HEALTHY_NODE_FG_COLOR = "#289b42";
  private static final String HEALTHY_NODE_BG_COLOR = "#ffffff";

  private static final String ERROR_NODE_FG_COLOR = "#ffffff";
  private static final String ERROR_NODE_BG_COLOR = "#E8473F";

  private static final String WARNING_NODE_FG_COLOR = "#000000";
  private static final String WARNING_NODE_BG_COLOR = "#EBEB3E";

  @AllArgsConstructor
  private static class CheckItem {
    boolean hasError;
    String timestamp;
    JsonNode json;
  }

  @Getter
  @Setter
  private static class NodeReport {
    private boolean hasError;
    private boolean hasWarning;
    private String nodeIp;
    private List<CheckItem> checks;
  }

  private static final class NodesComparator implements Comparator<String> {
    private final Map<String, NodeReport> reports;

    private final Comparator<NodeReport> NODE_DATA_COMPARATOR =
        Comparator.comparing(NodeReport::isHasError)
            .thenComparing(NodeReport::isHasWarning)
            .reversed();

    private NodesComparator(Map<String, NodeReport> reports) {
      this.reports = reports;
    }

    @Override
    public int compare(String nodeName1, String nodeName2) {
      NodeReport data1 = reports.get(nodeName1);
      NodeReport data2 = reports.get(nodeName2);
      if (data1 == null || data2 == null) {
        return data1 == null ? (data2 == null ? 0 : -1) : 1;
      }

      int result = NODE_DATA_COMPARATOR.compare(data1, data2);
      return result == 0 ? Comparator.<String>naturalOrder().compare(nodeName1, nodeName2) : result;
    }
  }

  private Map<String, NodeReport> parseReport(JsonNode report) {
    Map<String, NodeReport> data = new HashMap<>();
    ArrayNode nodes = (ArrayNode) report.get("data");
    for (JsonNode jsonNode : nodes) {
      String nodeNameFromJson = jsonNode.get("node_name").asText();
      NodeReport nodeReport = data.get(nodeNameFromJson);
      if (nodeReport == null) {
        nodeReport = new NodeReport();
        nodeReport.checks = new ArrayList<>();
        nodeReport.nodeIp = jsonNode.get("node").asText();
        data.put(nodeNameFromJson, nodeReport);
      }

      boolean hasError = jsonNode.get("has_error").asBoolean();
      nodeReport.checks.add(
          new CheckItem(hasError, jsonNode.get("timestamp_iso").asText(), jsonNode));
      nodeReport.hasError |= hasError;
      nodeReport.hasWarning |= jsonNode.get("has_warning").asBoolean();
    }
    return data;
  }

  /**
   * Decorates the passed report into the HTML view.
   *
   * @param u Universe
   * @param report Source json object
   * @param reportOnlyErrors
   * @return Decorated report (String)
   */
  public String asHtml(Universe u, JsonNode report, boolean reportOnlyErrors) {

    Map<String, NodeReport> reports = parseReport(report);
    StringBuilder summary = new StringBuilder();

    StringBuilder content = new StringBuilder();
    htmlReportNodesPart(
        u,
        reportOnlyErrors,
        content,
        u.getUniverseDetails().getPrimaryCluster(),
        "Primary Cluster",
        reports);
    summary.append(
        getSummary(
            u, u.getUniverseDetails().getPrimaryCluster(), "Primary Cluster", reports, true));

    int readOnlyIndex = 0;
    for (Cluster cluster : u.getUniverseDetails().getReadOnlyClusters()) {
      String clusterName = "Read Replica" + (readOnlyIndex == 0 ? "" : " " + readOnlyIndex++);
      htmlReportNodesPart(u, reportOnlyErrors, content, cluster, clusterName, reports);
      summary.append(getSummary(u, cluster, clusterName, reports, false));
    }

    if (content.length() == 0) {
      content.append("<b>No errors to report.</b>");
    }

    String style =
        String.format(
            "%s font-size: 14px;background-color:#f7f7f7;padding:25px 30px 5px;", STYLE_FONT);
    // Add timestamp to avoid gmail collapsing.
    String timestamp =
        String.format(
            "<span style=\"color:black;font-size:10px;\">%s</span>",
            report.path("timestamp_iso").asText());

    String header =
        String.format(
            "<table width=\"100%%\">\n"
                + "    <tr>\n"
                + "        <td style=\"text-align:left\">%s</td>\n"
                + "        <td style=\"text-align:left\">%s</td>\n"
                + "        <td style=\"text-align:right\">%s</td>\n"
                + "    </tr>\n"
                + "    <tr>\n"
                + "        <td style=\"text-align:left\">%s</td>\n"
                + "        <td style=\"text-align:left\">%s</td>\n"
                + "    </tr>\n"
                + "</table>\n",
            makeHeaderLeft("Universe name", u.getName()),
            makeHeaderLeft("Universe version", report.path("yb_version").asText()),
            timestamp,
            makeHeaderLeft("YW host name", Util.getHostname()),
            makeHeaderLeft("YW host IP", Util.getHostIP()));

    return String.format(
        "<html><body><pre style=\"%s\">%s %s %s %s</pre></body></html>",
        style, header, summary, content.toString(), timestamp);
  }

  private static StringBuilder getSummary(
      Universe u,
      Cluster cluster,
      String clusterName,
      Map<String, NodeReport> reports,
      boolean needHeader) {

    StringBuilder content = new StringBuilder();
    List<String> nodeNames = getNodeNames(u, cluster, reports);
    if (nodeNames.isEmpty()) {
      return content;
    }

    if (needHeader) {
      content.append(trElement(String.format("<h2 style=\"%s\">Summary:</h2>", H2_STYLE)));
    }

    int nodesWithErrors = 0;
    int nodesWithWarnings = 0;
    int healthyNodes = 0;
    for (String node : nodeNames) {
      NodeReport nodeReport = reports.get(node);
      if (nodeReport == null) {
        // Node is stopped/released, no report available.
        continue;
      }
      if (nodeReport.hasError) {
        nodesWithErrors++;
      } else if (nodeReport.hasWarning) {
        nodesWithWarnings++;
      } else {
        healthyNodes++;
      }
    }

    List<String> labels = new ArrayList<>();
    labels.add(
        String.format(
            "<td><h3 style=\"color:#8D8F9D;font-weight:400;\">%s:</h3></td><td>", clusterName));
    if (nodesWithErrors > 0) {
      labels.add(
          createNodesCounter(nodesWithErrors, "failing", ERROR_NODE_FG_COLOR, ERROR_NODE_BG_COLOR));
    }
    if (nodesWithWarnings > 0) {
      labels.add(
          createNodesCounter(
              nodesWithWarnings, "warning", WARNING_NODE_FG_COLOR, WARNING_NODE_BG_COLOR));
    }
    if (healthyNodes > 0) {
      // Inverting colors for healthy nodes(!).
      labels.add(
          createNodesCounter(
              healthyNodes, "healthy", HEALTHY_NODE_BG_COLOR, HEALTHY_NODE_FG_COLOR));
    }

    String tableContainerStyle =
        "background-color:#ffffff;padding:5px 10px 5px;\n"
            + "border-radius:10px;margin-top:5px;margin-bottom:5px;";
    content.append(
        String.format(
            "<div style=\"%s\">%s</div>",
            tableContainerStyle,
            tableElement(
                String.format(
                    "<tr style=\"vertical-align:top\">%s</td></tr>",
                    String.join("&nbsp;&nbsp;", labels)))));

    return content;
  }

  private static String createNodesCounter(
      int count, String descriptor, String fgColor, String bgColor) {
    // TODO Auto-generated method stub
    return String.format(
        "<span style=\"border-radius:4px;color:%s;background-color:%s\">"
            + "&nbsp;%d %s %s&nbsp;</span>",
        fgColor, bgColor, count, descriptor, count == 1 ? "node" : "nodes");
  }

  private void htmlReportNodesPart(
      Universe u,
      boolean reportOnlyErrors,
      StringBuilder content,
      Cluster cluster,
      String clusterName,
      Map<String, NodeReport> reports) {

    List<String> nodeNames = getNodeNames(u, cluster, reports);
    boolean clusterNameAdded = false;
    for (String nodeName : nodeNames) {
      NodeReport nodeData = reports.get(nodeName);
      if (nodeData == null) {
        continue;
      }

      StringBuilder nodeContentData = new StringBuilder();
      boolean isFirstCheck = true;
      for (CheckItem check : nodeData.checks) {
        if (!check.hasError && reportOnlyErrors) {
          continue;
        }
        nodeContentData.append(assembleMailRow(check.json, isFirstCheck, check.timestamp));
        isFirstCheck = false;
      }

      if (reportOnlyErrors && !nodeData.hasError) {
        continue;
      }

      String nodeHeaderFgColor =
          nodeData.hasError
              ? ERROR_NODE_FG_COLOR
              : (nodeData.hasWarning ? WARNING_NODE_FG_COLOR : HEALTHY_NODE_FG_COLOR);
      String nodeHeaderBgColor =
          nodeData.hasError
              ? ERROR_NODE_BG_COLOR
              : (nodeData.hasWarning ? WARNING_NODE_BG_COLOR : HEALTHY_NODE_BG_COLOR);
      String nodeHeaderStyleColors =
          String.format("background-color:%s;color:%s", nodeHeaderBgColor, nodeHeaderFgColor);
      String badgeCaption =
          nodeData.hasError ? "Error" : (nodeData.hasWarning ? "Warning" : "Running fine");

      String badgeStyle =
          String.format(
              "style=\"font-weight:400;margin-left:10px;\n"
                  + "font-size:0.6em;vertical-align:middle;\n"
                  + "%s;\n"
                  + "border-radius:4px;padding:2px 6px;\"",
              nodeHeaderStyleColors);

      String nodeHeaderTitle =
          String.format(
              "%s<br>%s<span %s>%s</span>\n", nodeName, nodeData.nodeIp, badgeStyle, badgeCaption);

      if (!clusterNameAdded) {
        content.append(String.format("<h2>%s</h2>", makeSubtitle(clusterName)));
        clusterNameAdded = true;
      }

      String nodeHeader =
          String.format(
              "%s<h2 style=\"%s\">%s</h2>\n", makeSubtitle("Node"), H2_STYLE, nodeHeaderTitle);

      String tableContainerStyle =
          "background-color:#ffffff;padding:15px 20px 7px;\n"
              + "border-radius:10px;margin-top:30px;margin-bottom:50px;";

      content.append(
          String.format(
              "%s<div style=\"%s\">%s</div>",
              nodeHeader, tableContainerStyle, tableContainer(nodeContentData.toString())));
    }
  }

  private static List<String> getNodeNames(
      Universe u, Cluster cluster, Map<String, NodeReport> reports) {
    List<String> nodeNames =
        u.getNodesInCluster(cluster.uuid).stream()
            .map(node -> node.nodeName)
            .sorted(new NodesComparator(reports))
            .collect(Collectors.toList());
    return nodeNames;
  }

  private static String makeSubtitle(String content) {
    return String.format("<span style=\"color:#8D8F9D;font-weight:400;\">%s:</span>\n", content);
  }

  private static String thElement(String style, String content) {
    return String.format("<th style=\"%s\">%s</th>\n", style, content);
  }

  private static String trElement(String content) {
    return String.format(
        "<tr style=\"text-transform:uppercase;font-weight:500;\">%s</tr>\n", content);
  }

  private static String tableElement(String content) {
    return String.format(
        "<table cellspacing=\"0\" style=\"width:auto;text-align:left;"
            + "font-size:12px;\">%s</table>\n",
        content);
  }

  // @formatter:off
  private static String tableContainer(String content) {
    return tableElement(
        trElement(
                thElement("width:15%;padding:0 30px 10px 0;", "Check type")
                    + thElement("width:15%;white-space:nowrap;padding:0 30px 10px 0;", "Details")
                    + thElement("", ""))
            + content);
  }

  private static String makeHeaderLeft(String title, String content) {
    return String.format(
        "%s<h1 style=\"%sline-height:1em;color:#202951;font-weight:700;font-size:1.85em;\n"
            + "padding-bottom:15px;margin:0;\">%s</h1>",
        makeSubtitle(title), STYLE_FONT, content);
  }

  private static String tdElement(
      String border_color, String padding, String weight, String content) {
    return String.format(
        "<td style=\"border-top: 1px solid %s;padding:%s;%svertical-align:top;\">%s</td>\n",
        border_color, padding, weight, content);
  }

  private static String preElement(String content) {
    return String.format(
        "<pre style=\"background-color:#f7f7f7;border: 1px solid #e5e5e9;padding:10px;\n"
            + "white-space: pre-wrap;border-radius:5px;\">%s</pre>\n",
        content);
  }

  // @formatter:off

  private static String assembleMailRow(JsonNode rowData, boolean isFirstCcheck, String timestamp) {

    String tsSpan =
        isFirstCcheck
            ? String.format("<div style=\"color:#ffffff;font-size:5px;\">%s</div>", timestamp)
            : "";
    String process = rowData.has("process") ? rowData.get("process").asText() : "";
    String borderColor = isFirstCcheck ? "transparent" : "#e5e5e9";

    // @formatter:off
    String badge =
        rowData.get("has_error").asBoolean()
            ? ERROR_BADGE
            : (rowData.get("has_warning").asBoolean() ? WARNING_BADGE : "");
    // @formatter:on

    String detailsContentOk = "<div style=\"padding:11px 0;\">Ok</div>";
    ArrayNode dataDetails = (ArrayNode) rowData.get("details");
    List<String> dataDetailsStr = new ArrayList<>();
    dataDetails.forEach(value -> dataDetailsStr.add(value.asText()));

    String detailsContent =
        dataDetails.size() == 0
            ? detailsContentOk
            : preElement(
                String.join(
                    "Disk utilization".equals(rowData.get("message").asText()) ? "\n" : " ",
                    dataDetailsStr));

    return String.format(
        "<tr>%s %s %s</tr>\n",
        tdElement(
            borderColor,
            "10px 20px 10px 0",
            "font-weight:700;font-size:1.2em;",
            badge + rowData.get("message") + tsSpan),
        tdElement(borderColor, "10px 20px 10px 0", "", process),
        tdElement(borderColor, "0", "", detailsContent));
  }

  /**
   * Pretty prints the passed report excluding some nodes if needed.
   *
   * @param report Source json object
   * @param reportOnlyErrors
   * @return Pretty-printed object (String)
   */
  public String asPlainText(JsonNode report, boolean reportOnlyErrors) {
    if (!reportOnlyErrors) {
      return prettyPrint(report);
    }

    JsonNode localReport = report.deepCopy();
    ArrayNode data = (ArrayNode) localReport.path("data");

    for (int i = data.size() - 1; i >= 0; i--) {
      JsonNode item = data.get(i);
      if (!item.path("has_error").asBoolean() && !item.path("has_warning").asBoolean()) {
        data.remove(i);
      }
    }
    return prettyPrint(localReport);
  }

  private static String prettyPrint(JsonNode obj) {
    ObjectMapper mapper = new ObjectMapper();

    // Setup a pretty printer with an indenter (2 spaces)
    DefaultPrettyPrinter.Indenter indenter = new DefaultIndenter("  ", DefaultIndenter.SYS_LF);
    DefaultPrettyPrinter printer = new DefaultPrettyPrinter();
    printer.indentObjectsWith(indenter);
    printer.indentArraysWith(indenter);

    try {
      return mapper.writer(printer).writeValueAsString(obj);
    } catch (JsonProcessingException e) {
      throw new RuntimeException("Can not pretty print a Json object.");
    }
  }
}
