package com.yugabyte.yw.common.supportbundle;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.controllers.handlers.UniverseInfoHandler;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.text.ParseException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.List;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Singleton
class UniverseLogsComponent implements SupportBundleComponent {

  private final UniverseInfoHandler universeInfoHandler;
  private final NodeUniverseManager nodeUniverseManager;
  protected final Config config;
  private final SupportBundleUtil supportBundleUtil;
  public final String NODE_UTILS_SCRIPT = "bin/node_utils.sh";

  @Inject
  UniverseLogsComponent(
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
  public void downloadComponent(Customer customer, Universe universe, Path bundlePath)
      throws IOException {
    List<NodeDetails> nodes = universe.getNodes().stream().collect(Collectors.toList());

    String destDir = bundlePath.toString() + "/" + "universe_logs";
    Path destPath = Paths.get(destDir);
    Files.createDirectories(destPath);

    // Downloads the /mnt/d0/yb-data/master/logs and /mnt/d0/yb-data/tserver/logs from each node
    // in the universe into the bundle path
    for (NodeDetails node : nodes) {
      // Get source file path prefix
      String mountPath =
          supportBundleUtil.getDataDirPath(universe, node, nodeUniverseManager, config);
      String nodeHomeDir = mountPath + "/yb-data";

      // Get target file path
      String nodeName = node.getNodeName();
      Path nodeTargetFile = Paths.get(destDir, nodeName + ".tar.gz");

      log.debug(
          "Gathering universe logs for node: {}, source path: {}, target path: {}",
          nodeName,
          nodeHomeDir,
          nodeTargetFile.toString());

      Path targetFile =
          universeInfoHandler.downloadNodeFile(
              customer, universe, node, nodeHomeDir, "master/logs;tserver/logs", nodeTargetFile);
    }
  }

  @Override
  public void downloadComponentBetweenDates(
      Customer customer, Universe universe, Path bundlePath, Date startDate, Date endDate)
      throws IOException, ParseException {
    List<NodeDetails> nodes = universe.getNodes().stream().collect(Collectors.toList());

    String destDir = bundlePath.toString() + "/" + "universe_logs";
    Path destPath = Paths.get(destDir);
    Files.createDirectories(destPath);

    // Downloads the /mnt/d0/yb-data/master/logs and /mnt/d0/yb-data/tserver/logs from each node
    // in the universe into the bundle path
    for (NodeDetails node : nodes) {
      // Get source file path prefix
      String mountPath =
          supportBundleUtil.getDataDirPath(universe, node, nodeUniverseManager, config);
      String nodeHomeDir = mountPath + "/yb-data";

      // Get target file path
      String nodeName = node.getNodeName();
      Path nodeTargetFile = Paths.get(destDir, nodeName + ".tar.gz");

      log.debug(
          "Gathering universe logs for node: {}, source path: {}, target path: {}, "
              + "between start date: {}, end date: {}",
          nodeName,
          nodeHomeDir,
          nodeTargetFile.toString(),
          startDate,
          endDate);

      String universeLogsRegexPattern =
          config.getString("yb.support_bundle.universe_logs_regex_pattern");

      // Get and filter master log files that fall within given dates
      String masterLogsPath = nodeHomeDir + "/master/logs";
      List<String> masterLogFilePaths = new ArrayList<>();
      if (checkNodeIfFileExists(node, universe, masterLogsPath)) {
        masterLogFilePaths =
            getNodeFilePaths(node, universe, masterLogsPath, /*maxDepth*/ 1, /*fileType*/ "f");
        masterLogFilePaths =
            supportBundleUtil.filterFilePathsBetweenDates(
                masterLogFilePaths, universeLogsRegexPattern, startDate, endDate, false);
      }

      // Get and filter tserver log files that fall within given dates
      String tserverLogsPath = nodeHomeDir + "/tserver/logs";
      List<String> tserverLogFilePaths = new ArrayList<>();
      if (checkNodeIfFileExists(node, universe, tserverLogsPath)) {
        tserverLogFilePaths =
            getNodeFilePaths(node, universe, tserverLogsPath, /*maxDepth*/ 1, /*fileType*/ "f");
        tserverLogFilePaths =
            supportBundleUtil.filterFilePathsBetweenDates(
                tserverLogFilePaths, universeLogsRegexPattern, startDate, endDate, false);
      }

      // Combine both master and tserver files to download all the files together
      List<String> allLogFilePaths =
          Stream.concat(masterLogFilePaths.stream(), tserverLogFilePaths.stream())
              .collect(Collectors.toList());

      if (allLogFilePaths.size() > 0) {
        Path targetFile =
            universeInfoHandler.downloadNodeFile(
                customer,
                universe,
                node,
                nodeHomeDir,
                String.join(";", allLogFilePaths),
                nodeTargetFile);
      } else {
        log.debug(
            "Found no matching universe logs for node: {}, source path: {}, target path: {}, "
                + "between start date: {}, end date: {}",
            nodeName,
            nodeHomeDir,
            nodeTargetFile.toString(),
            startDate,
            endDate);
      }
    }
  }

  /**
   * Checks if a file or directory exists on the node in the universe
   *
   * @param node
   * @param universe
   * @param remotePath
   * @return true if file/directory exists, else false
   */
  public boolean checkNodeIfFileExists(NodeDetails node, Universe universe, String remotePath) {
    List<String> params = new ArrayList<>();
    params.add("check_file_exists");
    params.add(remotePath);

    ShellResponse scriptOutput =
        this.nodeUniverseManager.runScript(node, universe, NODE_UTILS_SCRIPT, params);

    if (scriptOutput.extractRunCommandOutput().trim().equals("1")) {
      return true;
    } else {
      return false;
    }
  }

  /**
   * Gets a list of all the absolute file paths at a given remote directory
   *
   * @param node
   * @param universe
   * @param remoteDirPath
   * @param maxDepth
   * @param fileType
   * @return list of strings of all the absolute file paths
   */
  public List<String> getNodeFilePaths(
      NodeDetails node, Universe universe, String remoteDirPath, int maxDepth, String fileType) {
    List<String> command = new ArrayList<>();
    command.add("find");
    command.add(remoteDirPath);
    command.add("-maxdepth");
    command.add(String.valueOf(maxDepth));
    command.add("-type");
    command.add(fileType);

    ShellResponse shellOutput = this.nodeUniverseManager.runCommand(node, universe, command);
    return Arrays.asList(shellOutput.extractRunCommandOutput().trim().split("\n", 0));
  }
}
