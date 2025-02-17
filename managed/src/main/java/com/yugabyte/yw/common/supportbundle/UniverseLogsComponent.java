package com.yugabyte.yw.common.supportbundle;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.controllers.handlers.UniverseInfoHandler;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.file.Files;
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
import org.apache.commons.io.FileUtils;

@Slf4j
@Singleton
class UniverseLogsComponent implements SupportBundleComponent {

  private final UniverseInfoHandler universeInfoHandler;
  private final NodeUniverseManager nodeUniverseManager;
  protected final Config config;
  private final SupportBundleUtil supportBundleUtil;
  private final RuntimeConfGetter confGetter;
  private final String LOG_DIR_GFLAG = "log_dir";
  private final String YNP_LOG_DIR = "%s/node-agent/release/%s/scripts/logs";
  private final String YNP_LOG_FILE = "app.log";

  @Inject
  UniverseLogsComponent(
      UniverseInfoHandler universeInfoHandler,
      NodeUniverseManager nodeUniverseManager,
      Config config,
      SupportBundleUtil supportBundleUtil,
      RuntimeConfGetter confGetter) {
    this.universeInfoHandler = universeInfoHandler;
    this.nodeUniverseManager = nodeUniverseManager;
    this.config = config;
    this.supportBundleUtil = supportBundleUtil;
    this.confGetter = confGetter;
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
                + "for 'UniverseLogsComponent' without start and end date, on universe = '%s'",
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
    // Downloads the /mnt/d0/yb-data/master/logs and /mnt/d0/yb-data/tserver/logs from each node
    // in the universe into the bundle path
    // Get source file path prefix
    String mountPath =
        supportBundleUtil.getDataDirPath(universe, node, nodeUniverseManager, config);
    String nodeHomeDir = mountPath + "/yb-data";

    // Get target file path
    String nodeName = node.getNodeName();
    Path nodeTargetFile =
        Paths.get(bundlePath.toString(), this.getClass().getSimpleName() + ".tar.gz");

    log.debug(
        "Gathering universe logs for node: {}, source path: {}, target path: {}, "
            + "between start date: {}, end date: {}",
        nodeName,
        nodeHomeDir,
        nodeTargetFile.toString(),
        startDate,
        endDate);

    String ynpLogDir = getYnpLogDir(node.cloudInfo.private_ip);
    Map<String, Long> allFilesMap =
        getFilesListWithSizes(customer, null, universe, startDate, endDate, node);
    // Collect YNP log file if present.
    if (ynpLogDir != null) {
      String ynpLogPath = ynpLogDir + "/" + YNP_LOG_FILE;
      if (allFilesMap.containsKey(ynpLogPath)) {
        allFilesMap.remove(ynpLogPath);
        supportBundleUtil.downloadNodeLevelComponent(
            universeInfoHandler,
            customer,
            universe,
            bundlePath,
            node,
            "/",
            Arrays.asList(ynpLogPath),
            this.getClass().getSimpleName(),
            false);
        Path filePath = Paths.get(bundlePath.toString(), ynpLogPath);
        // Move log to YNP dir.
        if (Files.exists(filePath)) {
          FileUtils.moveFileToDirectory(
              filePath.toFile(), Paths.get(bundlePath.toString(), "ynp").toFile(), true);
          Path pathToDelete =
              Paths.get(bundlePath.toString(), Paths.get(ynpLogDir).getName(0).toString());
          FileUtils.deleteDirectory(pathToDelete.toFile());
        }
      }
    }
    // Combine both master and tserver files to download all the files together
    List<String> allLogFilePaths =
        allFilesMap.keySet().stream()
            .map(filePath -> Paths.get(nodeHomeDir).relativize(Paths.get(filePath)))
            .map(Path::toString)
            .collect(Collectors.toList());

    if (allLogFilePaths.size() > 0) {
      // Download all logs batch wise
      supportBundleUtil.batchWiseDownload(
          universeInfoHandler,
          customer,
          universe,
          bundlePath,
          node,
          nodeTargetFile,
          nodeHomeDir,
          allLogFilePaths,
          this.getClass().getSimpleName(),
          false);
    } else {
      log.debug(
          "Found no matching universe logs for node: {}, source path: {}, target path: {}, "
              + "between start date: {}, end date: {}.",
          nodeName,
          nodeHomeDir,
          nodeTargetFile.toString(),
          startDate,
          endDate);
    }
  }

  private String getOverridenGflagValue(Universe universe, ServerType serverType, String gflag) {
    String ret = null;
    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;

    if (userIntent.specificGFlags == null
        || userIntent.specificGFlags.getPerProcessFlags() == null) {
      return ret;
    }

    Map<UniverseTaskBase.ServerType, Map<String, String>> specificGflags =
        userIntent.specificGFlags.getPerProcessFlags().value;
    if (specificGflags != null && specificGflags.containsKey(serverType)) {
      Map<String, String> mp = specificGflags.get(serverType);
      if (mp.containsKey(gflag)) {
        ret = mp.get(gflag);
      }
    }
    return ret;
  }

  public Map<String, Long> getFilesListWithSizes(
      Customer customer,
      SupportBundleFormData bundleData,
      Universe universe,
      Date startDate,
      Date endDate,
      NodeDetails node)
      throws Exception {
    // Get source file path prefix
    String mountPath =
        supportBundleUtil.getDataDirPath(universe, node, nodeUniverseManager, config);
    String nodeHomeDir = mountPath + "/yb-data";

    // Get the regex patterns used to filter file names
    String universeLogsRegexPattern =
        confGetter.getConfForScope(universe, UniverseConfKeys.universeLogsRegexPattern);
    String postgresLogsRegexPattern =
        confGetter.getConfForScope(universe, UniverseConfKeys.postgresLogsRegexPattern);
    String connectionPoolingLogsRegexPattern =
        confGetter.getConfForScope(universe, UniverseConfKeys.connectionPoolingLogsRegexPattern);
    List<String> fileRegexList =
        Arrays.asList(
            universeLogsRegexPattern, postgresLogsRegexPattern, connectionPoolingLogsRegexPattern);

    // Get and filter master log files that fall within given dates
    String masterLogsPath = nodeHomeDir + "/master/logs";
    // Update logs path if overriden via Gflag.
    String master_log_dir = getOverridenGflagValue(universe, ServerType.MASTER, LOG_DIR_GFLAG);
    if (master_log_dir != null) {
      masterLogsPath = master_log_dir;
    }
    Map<String, Long> finalMap = new HashMap<>();
    if (nodeUniverseManager.checkNodeIfFileExists(node, universe, masterLogsPath)) {
      Map<String, Long> masterLogsPathSizeMap =
          nodeUniverseManager.getNodeFilePathAndSizes(
              node, universe, masterLogsPath, /*maxDepth*/ 1, /*fileType*/ "f");
      List<String> filteredMasterLogs =
          supportBundleUtil.filterFilePathsBetweenDates(
              masterLogsPathSizeMap.keySet().stream().collect(Collectors.toList()),
              fileRegexList,
              startDate,
              endDate);
      // Add filtered paths to the final map.
      for (String path : filteredMasterLogs) {
        finalMap.put(path, masterLogsPathSizeMap.get(path));
      }
    }

    // Get and filter tserver log files that fall within given dates
    String tserverLogsPath = nodeHomeDir + "/tserver/logs";
    // Update logs path if overriden via Gflag.
    String ts_log_dir = getOverridenGflagValue(universe, ServerType.TSERVER, LOG_DIR_GFLAG);
    if (ts_log_dir != null) {
      tserverLogsPath = ts_log_dir;
    }

    if (nodeUniverseManager.checkNodeIfFileExists(node, universe, tserverLogsPath)) {
      Map<String, Long> tserverLogsPathSizeMap =
          nodeUniverseManager.getNodeFilePathAndSizes(
              node, universe, tserverLogsPath, /* maxDepth */ 1, /* fileType */ "f");
      List<String> filteredTserverLogs =
          supportBundleUtil.filterFilePathsBetweenDates(
              tserverLogsPathSizeMap.keySet().stream().collect(Collectors.toList()),
              fileRegexList,
              startDate,
              endDate);
      // Add filtered paths to the final map.
      for (String path : filteredTserverLogs) {
        finalMap.put(path, tserverLogsPathSizeMap.get(path));
      }
    }

    // Collect YNP logs as part of this component.
    String ynpLogDir = getYnpLogDir(node.cloudInfo.private_ip);
    if (ynpLogDir == null) {
      log.warn(
          "Skipping YNP logs collection on node {} as location cannot be determined.",
          node.nodeName);
      return finalMap;
    }

    Map<String, Long> ynpLogDirFiles =
        nodeUniverseManager.getNodeFilePathAndSizes(
            node, universe, ynpLogDir, /* maxDepth */ 1, /* fileType */ "f");
    String ynpLogPath = ynpLogDir + "/" + YNP_LOG_FILE;
    if (ynpLogDirFiles.containsKey(ynpLogPath)) {
      finalMap.put(ynpLogPath, ynpLogDirFiles.get(ynpLogPath));
    }
    return finalMap;
  }

  private String getYnpLogDir(String nodeIp) {
    Optional<NodeAgent> nodeAgent = NodeAgent.maybeGetByIp(nodeIp);
    if (nodeAgent.isPresent()) {
      return String.format(
          YNP_LOG_DIR,
          confGetter.getGlobalConf(GlobalConfKeys.nodeAgentInstallPath),
          nodeAgent.get().getVersion());
    }
    return null;
  }
}
