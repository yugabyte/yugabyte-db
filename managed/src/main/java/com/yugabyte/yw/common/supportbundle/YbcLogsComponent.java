package com.yugabyte.yw.common.supportbundle;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.KubernetesTaskBase;
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
import java.util.Arrays;
import java.util.Collections;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Singleton
class YbcLogsComponent implements SupportBundleComponent {

  private final UniverseInfoHandler universeInfoHandler;
  private final NodeUniverseManager nodeUniverseManager;
  protected final Config config;
  private final SupportBundleUtil supportBundleUtil;
  public final String NODE_UTILS_SCRIPT = "bin/node_utils.sh";
  public final String YBC_CONF_PATH = "controller/conf/server.conf";

  @Inject
  YbcLogsComponent(
      UniverseInfoHandler universeInfoHandler,
      NodeUniverseManager nodeUniverseManager,
      Config config,
      SupportBundleUtil supportBundleUtil) {
    this.universeInfoHandler = universeInfoHandler;
    this.nodeUniverseManager = nodeUniverseManager;
    this.config = config;
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
    String errMsg =
        String.format(
            "downloadComponent() method not applicable "
                + "for 'YbcLogsComponent' without start and end date, on universe = '%s'",
            universe.getName());
    throw new RuntimeException(errMsg);
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
    // Downloads the /mnt/d0/ybc-data/controller/logs from each node
    // in the universe into the bundle path
    // Get source file path prefix
    String mountPath =
        supportBundleUtil.getDataDirPath(universe, node, nodeUniverseManager, config);
    String nodeHomeDir = mountPath + "/ybc-data";

    // Get target file path
    String nodeName = node.getNodeName();
    Path nodeTargetFile =
        Paths.get(bundlePath.toString(), this.getClass().getSimpleName() + ".tar.gz");

    log.debug(
        "Gathering YB-Controller logs for node: {}, source path: {}, target path: {}, "
            + "between start date: {}, end date: {}",
        nodeName,
        nodeHomeDir,
        nodeTargetFile.toString(),
        startDate,
        endDate);

    List<String> filteredPaths =
        getFilesListWithSizes(customer, null, universe, startDate, endDate, node).keySet().stream()
            .collect(Collectors.toList());

    if (filteredPaths.size() > 0) {

      filteredPaths =
          filteredPaths.stream()
              .peek(s -> log.error("file path before relativing = {}", s))
              .map(filePath -> Paths.get(nodeHomeDir).relativize(Paths.get(filePath)))
              .map(Path::toString)
              .peek(s -> log.error("file path after relativing = {}", s))
              .collect(Collectors.toList());

      // Download all logs batch wise
      supportBundleUtil.batchWiseDownload(
          universeInfoHandler,
          customer,
          universe,
          bundlePath,
          node,
          nodeTargetFile,
          nodeHomeDir,
          filteredPaths,
          this.getClass().getSimpleName(),
          false);

      // Collect YBC server.conf file from the node home directory. Directory to get the server.conf
      // can vary between VM and k8s based universes.
      CloudType cloudType =
          universe.getUniverseDetails().getPrimaryCluster().userIntent.providerType;
      String goToPathOnNode;
      if (CloudType.kubernetes.equals(cloudType)) {
        goToPathOnNode = KubernetesTaskBase.K8S_NODE_YW_DATA_DIR;
      } else {
        goToPathOnNode = nodeUniverseManager.getYbHomeDir(node, universe);
      }
      supportBundleUtil.batchWiseDownload(
          universeInfoHandler,
          customer,
          universe,
          bundlePath,
          node,
          nodeTargetFile,
          goToPathOnNode,
          Collections.singletonList(YBC_CONF_PATH),
          this.getClass().getSimpleName(),
          false);
    } else {
      log.debug(
          "Found no matching YB-Controller logs for node: {}, source path: {}, target path: {}, "
              + "between start date: {}, end date: {}",
          nodeName,
          nodeHomeDir,
          nodeTargetFile.toString(),
          startDate,
          endDate);
    }
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

    String mountPath =
        supportBundleUtil.getDataDirPath(universe, node, nodeUniverseManager, config);
    String nodeHomeDir = mountPath + "/ybc-data";
    String ybcLogsRegexPattern = config.getString("yb.support_bundle.ybc_logs_regex_pattern");

    // Get and filter YB-Controller log files that fall within given dates
    String ybcLogsPath = nodeHomeDir + "/controller/logs";
    if (nodeUniverseManager.checkNodeIfFileExists(node, universe, ybcLogsPath)) {
      // Gets the absolute paths and sizes of the ybc logs
      Map<String, Long> ybcLogPathSizeMap =
          nodeUniverseManager.getNodeFilePathAndSizes(
              node, universe, ybcLogsPath, /* maxDepth */ 1, /* fileType */ "f");
      List<String> filteredPaths =
          supportBundleUtil.filterFilePathsBetweenDates(
              ybcLogPathSizeMap.keySet().stream().collect(Collectors.toList()),
              Arrays.asList(ybcLogsRegexPattern),
              startDate,
              endDate);
      for (String path : filteredPaths) {
        res.put(path, ybcLogPathSizeMap.get(path));
      }
    }
    return res;
  }
}
