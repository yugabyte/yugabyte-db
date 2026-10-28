// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundle;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.controllers.handlers.UniverseInfoHandler;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

/**
 * Collects the node health check's log under {@code <yb_home>/metrics/logs} - the only record of
 * why a check failed or was skipped - along with the rest of {@code <yb_home>/metrics}, which is
 * the node_exporter textfiles the check writes its metrics into.
 */
@Slf4j
@Singleton
public class NodeHealthLogsComponent implements SupportBundleComponent {
  private static final String METRICS_DIR = "metrics";
  private static final String LOGS_DIR = "logs";

  /**
   * Matches the health log names node_health.py writes - node_health-YYYYMMDD.log, and the .gz the
   * log rotation leaves behind. Group 1 is the file type and group 2 the date, which is the shape
   * SupportBundleUtil.filterFilePathsBetweenDates expects. Not a runtime config like the universe
   * log patterns, because this name is set by our own script rather than by the operator.
   */
  private static final String HEALTH_LOG_REGEX = "((?:.*)(?:node_health)-)(\\d{8})(?:\\.log.*)?";

  private static final int MAX_DEPTH = 1;

  private final UniverseInfoHandler universeInfoHandler;
  private final NodeUniverseManager nodeUniverseManager;
  private final SupportBundleUtil supportBundleUtil;

  @Inject
  public NodeHealthLogsComponent(
      UniverseInfoHandler universeInfoHandler,
      NodeUniverseManager nodeUniverseManager,
      SupportBundleUtil supportBundleUtil) {
    this.universeInfoHandler = universeInfoHandler;
    this.nodeUniverseManager = nodeUniverseManager;
    this.supportBundleUtil = supportBundleUtil;
  }

  @Override
  public void downloadComponent(
      SupportBundleTaskParams supportBundleTaskParams,
      Customer customer,
      Universe universe,
      Path bundlePath,
      NodeDetails node)
      throws Exception {
    downloadComponentBetweenDates(
        supportBundleTaskParams, customer, universe, bundlePath, null, null, node);
  }

  @Override
  public void downloadComponentBetweenDates(
      SupportBundleTaskParams supportBundleTaskParams,
      Customer customer,
      Universe universe,
      Path bundlePath,
      Date startDate,
      Date endDate,
      NodeDetails node)
      throws Exception {
    Map<String, Long> filePathSizeMap =
        getFilesListWithSizes(customer, null, universe, startDate, endDate, node);
    if (filePathSizeMap.isEmpty()) {
      return;
    }

    Path metricsDir = Paths.get(nodeUniverseManager.getYbHomeDir(node, universe), METRICS_DIR);
    Path nodeTargetFile = Paths.get(bundlePath.toString(), getClass().getSimpleName() + ".tar.gz");
    log.debug(
        "Gathering metrics files for node: {}, source path: {}, target path: {}",
        node.getNodeName(),
        metricsDir,
        nodeTargetFile);

    // Relativized from the parent so the archive carries the metrics/ directory itself.
    Path metricsDirParent = metricsDir.getParent();
    List<String> relativeFilePaths =
        filePathSizeMap.keySet().stream()
            .map(Paths::get)
            .map(filePath -> metricsDirParent.relativize(filePath))
            .map(Path::toString)
            .collect(Collectors.toList());

    supportBundleUtil.batchWiseDownload(
        universeInfoHandler,
        customer,
        universe,
        bundlePath,
        node,
        nodeTargetFile,
        metricsDirParent.toString(),
        relativeFilePaths,
        getClass().getSimpleName(),
        false);
  }

  @Override
  public Map<String, Long> getFilesListWithSizes(
      Customer customer,
      SupportBundleFormData bundleData,
      Universe universe,
      Date startDate,
      Date endDate,
      NodeDetails node)
      throws Exception {
    Map<String, Long> res = new HashMap<>();
    if (node.cloudInfo == null || StringUtils.isBlank(node.cloudInfo.private_ip)) {
      log.info("Skipping metrics collection as node IP is not available");
      return res;
    }

    Path metricsDir = Paths.get(nodeUniverseManager.getYbHomeDir(node, universe), METRICS_DIR);
    if (nodeUniverseManager.checkNodeIfFileExists(node, universe, metricsDir.toString())) {
      log.info("Collecting files from metrics path {}", metricsDir);
      res.putAll(
          nodeUniverseManager.getNodeFilePathAndSizes(
              node, universe, metricsDir.toString(), MAX_DEPTH, /* fileType */ "f"));
    } else {
      log.info("Skipping non-existing metrics path {}", metricsDir);
    }

    // Listed separately, with the trailing slash: metrics/logs is a symlink onto the data
    // volume and node_utils.sh runs plain find, which does not descend into a symlink it is
    // not handed as a starting point. Without this the health logs are silently left behind.
    String healthLogsDir = metricsDir.resolve(LOGS_DIR).toString() + "/";
    if (nodeUniverseManager.checkNodeIfFileExists(node, universe, healthLogsDir)) {
      log.info("Collecting files from health logs path {}", healthLogsDir);
      Map<String, Long> healthLogSizes =
          nodeUniverseManager.getNodeFilePathAndSizes(
              node, universe, healthLogsDir, MAX_DEPTH, /* fileType */ "f");
      if (startDate != null && endDate != null) {
        // One log per day, so a bundle asking for a week should carry a week of them and not
        // however many days of history the node happens to be keeping.
        List<String> inRange =
            supportBundleUtil.filterFilePathsBetweenDates(
                new ArrayList<>(healthLogSizes.keySet()),
                Arrays.asList(HEALTH_LOG_REGEX),
                startDate,
                endDate);
        log.info(
            "Kept {} of {} health log file(s) between {} and {}",
            inRange.size(),
            healthLogSizes.size(),
            startDate,
            endDate);
        healthLogSizes.keySet().retainAll(inRange);
      }
      res.putAll(healthLogSizes);
    } else {
      log.info("Skipping non-existing health logs path {}", healthLogsDir);
    }
    return res;
  }
}
