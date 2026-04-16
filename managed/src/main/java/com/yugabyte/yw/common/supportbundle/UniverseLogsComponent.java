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
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.File;
import java.nio.file.Files;
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
  private final String YNP_LOG_DIR = "%s/node-agent/logs";
  private final String YNP_LOG_FILE = "app.log";
  private final String PG_LOG_FILE_PREFIX = "postgresql-";
  private final String ZIPPED_LOG_FILE_SUFFIX = ".log.gz";

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

    List<String> pgUpgradeLogs = new ArrayList<>();
    allFilesMap
        .keySet()
        .removeIf(
            filePath -> {
              if (filePath.contains("/pg_upgrade_data/")
                  || filePath.contains("/pg_upgrade_data_")) {
                pgUpgradeLogs.add(filePath);
                return true;
              }
              return false;
            });

    if (!pgUpgradeLogs.isEmpty()) {
      List<String> relativePaths =
          pgUpgradeLogs.stream()
              .map(filePath -> Paths.get(mountPath).relativize(Paths.get(filePath)).toString())
              .collect(Collectors.toList());
      supportBundleUtil.downloadNodeLevelComponent(
          universeInfoHandler,
          customer,
          universe,
          bundlePath,
          node,
          mountPath,
          relativePaths,
          this.getClass().getSimpleName(),
          false);
    }

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

      if (supportBundleTaskParams.bundleData.filterPgAuditLogs) {
        var tserverLogDir = Paths.get(bundlePath.toString(), "tserver/logs").toFile();
        if (tserverLogDir.exists()) {
          for (var file : tserverLogDir.listFiles()) {
            String fileName = file.toPath().getFileName().toString();
            if (file.isFile() && fileName.startsWith(PG_LOG_FILE_PREFIX)) {
              var unzippedFile = file;
              if (fileName.endsWith(ZIPPED_LOG_FILE_SUFFIX)) {
                unzippedFile = com.yugabyte.yw.common.utils.FileUtils.unGzip(file, tserverLogDir);
              }
              filterPgAuditLog(unzippedFile);
              // Delete unfiltered zipped and unzipped file.
              file.delete();
              if (!unzippedFile.equals(file)) {
                unzippedFile.delete();
              }
            }
          }
        }
      }
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

  // Creates a new log file called filtered_<logFile> which doesn't have PgAudit logs.
  private void filterPgAuditLog(File logFile) {
    String baseFileName = logFile.toPath().getFileName().toString();
    try {
      String outputFileName = "filtered_" + baseFileName;
      ProcessBuilder grepProcess =
          new ProcessBuilder("grep", "-v", "AUDIT:", logFile.getAbsolutePath());
      grepProcess.redirectOutput(new File(logFile.getParent(), outputFileName)).start().waitFor();
    } catch (Exception e) {
      log.error("Error while filtering pg log: {}", baseFileName);
      e.printStackTrace();
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
    if (ynpLogDir != null) {
      Map<String, Long> ynpLogDirFiles =
          nodeUniverseManager.getNodeFilePathAndSizes(
              node, universe, ynpLogDir, /* maxDepth */ 1, /* fileType */ "f");
      String ynpLogPath = ynpLogDir + "/" + YNP_LOG_FILE;
      if (ynpLogDirFiles.containsKey(ynpLogPath)) {
        finalMap.put(ynpLogPath, ynpLogDirFiles.get(ynpLogPath));
      }
    } else {
      log.warn(
          "Skipping YNP logs collection on node {} as location cannot be determined.",
          node.nodeName);
    }

    String masterInitdbPath = nodeHomeDir + "/master/logs/initdb.log";
    if (nodeUniverseManager.checkNodeIfFileExists(node, universe, masterInitdbPath)) {
      Map<String, Long> masterFiles =
          nodeUniverseManager.getNodeFilePathAndSizes(
              node, universe, nodeHomeDir + "/master/logs", 1, "f");
      if (masterFiles.containsKey(masterInitdbPath)) {
        finalMap.put(masterInitdbPath, masterFiles.get(masterInitdbPath));
      }
    }

    String tserverInitdbPath = nodeHomeDir + "/tserver/logs/initdb.log";
    if (nodeUniverseManager.checkNodeIfFileExists(node, universe, tserverInitdbPath)) {
      Map<String, Long> tserverFiles =
          nodeUniverseManager.getNodeFilePathAndSizes(
              node, universe, nodeHomeDir + "/tserver/logs", 1, "f");
      if (tserverFiles.containsKey(tserverInitdbPath)) {
        finalMap.put(tserverInitdbPath, tserverFiles.get(tserverInitdbPath));
      }
    }

    if (nodeUniverseManager.checkNodeIfFileExists(node, universe, mountPath)) {
      Map<String, Long> mountPathDirs =
          nodeUniverseManager.getNodeFilePathAndSizes(node, universe, mountPath, 1, "d");

      List<String> pgUpgradeBasePaths = new ArrayList<>();
      for (String dirPath : mountPathDirs.keySet()) {
        String dirName =
            dirPath.startsWith("/") ? dirPath : Paths.get(mountPath, dirPath).toString();
        if (dirName.contains("pg_upgrade_data")) {
          String outputDir = dirName + "/pg_upgrade_output.d";
          if (nodeUniverseManager.checkNodeIfFileExists(node, universe, outputDir)) {
            pgUpgradeBasePaths.add(outputDir);
          }
        }
      }

      for (String pgUpgradeBasePath : pgUpgradeBasePaths) {
        Map<String, Long> allEntriesInPgUpgrade =
            nodeUniverseManager.getNodeFilePathAndSizes(node, universe, pgUpgradeBasePath, 1, "d");

        List<String> timestampDirs = new ArrayList<>();
        for (String dirName : allEntriesInPgUpgrade.keySet()) {
          if (!dirName.equals(pgUpgradeBasePath)) {
            String fullPath = dirName.startsWith("/") ? dirName : pgUpgradeBasePath + "/" + dirName;
            timestampDirs.add(fullPath.replaceAll("/+$", ""));
          }
        }

        for (String timestampDirPath : timestampDirs) {
          String logPath = timestampDirPath + "/log";
          if (nodeUniverseManager.checkNodeIfFileExists(node, universe, logPath)) {
            Map<String, Long> logFiles =
                nodeUniverseManager.getNodeFilePathAndSizes(node, universe, logPath, 1, "f");
            finalMap.putAll(logFiles);
          }
        }
      }
    }

    return finalMap;
  }

  private String getYnpLogDir(String nodeIp) {
    return String.format(
        YNP_LOG_DIR, confGetter.getGlobalConf(GlobalConfKeys.nodeAgentInstallPath));
  }
}
