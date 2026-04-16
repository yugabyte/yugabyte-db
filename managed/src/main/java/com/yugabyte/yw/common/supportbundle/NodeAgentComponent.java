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
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
@Singleton
public class NodeAgentComponent implements SupportBundleComponent {
  private static final List<String> SOURCE_NODE_FILES = Arrays.asList("logs", "config");
  private final UniverseInfoHandler universeInfoHandler;
  private final NodeUniverseManager nodeUniverseManager;
  private final SupportBundleUtil supportBundleUtil;

  @Inject
  public NodeAgentComponent(
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
    Map<String, Long> logsPathSizeMap =
        getFilesListWithSizes(customer, null, universe, null, null, node);
    if (logsPathSizeMap.isEmpty()) {
      return;
    }
    Optional<NodeAgent> optional = NodeAgent.maybeGetByIp(node.cloudInfo.private_ip);
    Path nodeAgentHome = Paths.get(optional.get().getHome());
    // Get target file path
    String nodeName = node.getNodeName();
    Path nodeTargetFile = Paths.get(bundlePath.toString(), getClass().getSimpleName() + ".tar.gz");
    log.debug(
        "Gathering node agent logs for node: {}, source path: {}, target path: {} ",
        nodeName,
        nodeAgentHome,
        nodeTargetFile);
    List<Path> nodeAgentLogFilePaths =
        logsPathSizeMap.keySet().stream().map(Paths::get).collect(Collectors.toList());
    // Relativize from the parent to include node-agent folder in the tgz.
    Path nodeAgentHomeParent = nodeAgentHome.getParent();
    List<String> relativeLogFilePaths =
        nodeAgentLogFilePaths.stream()
            .map(filePath -> nodeAgentHomeParent.relativize(filePath))
            .map(Path::toString)
            .collect(Collectors.toList());

    supportBundleUtil.batchWiseDownload(
        universeInfoHandler,
        customer,
        universe,
        bundlePath,
        node,
        nodeTargetFile,
        nodeAgentHomeParent.toString(),
        relativeLogFilePaths,
        getClass().getSimpleName(),
        false);
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
    // Simply return all the logs files as this method is just an overkill for now.
    downloadComponent(supportBundleTaskParams, customer, universe, bundlePath, node);
  }

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
      log.info("Skipping node-agent support-bundle download as node IP is not available");
      return res;
    }
    Optional<NodeAgent> optional = NodeAgent.maybeGetByIp(node.cloudInfo.private_ip);
    if (!optional.isPresent()) {
      log.info("Skipping node-agent support-bundle download as node-agent is not installed");
      return res;
    }
    Path nodeAgentHome = Paths.get(optional.get().getHome());
    for (String dir : SOURCE_NODE_FILES) {
      Path dirPath = nodeAgentHome.resolve(dir);
      if (!nodeUniverseManager.checkNodeIfFileExists(node, universe, dirPath.toString())) {
        log.info("Skipping non-existing node-agent path {}", dirPath);
        continue;
      }
      log.info("Collecting files from node-agent path {}", dirPath);
      res.putAll(
          nodeUniverseManager.getNodeFilePathAndSizes(
              node, universe, dirPath.toString(), /* maxDepth */ 1, /* fileType */ "f"));
    }
    return res;
  }
}
