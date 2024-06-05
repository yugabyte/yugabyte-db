package com.yugabyte.yw.common.supportbundle;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.controllers.handlers.UniverseInfoHandler;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.file.Path;
import java.nio.file.Paths;
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
  private final RuntimeConfGetter confGetter;

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

    // Get the regex patterns used to filter file names
    String universeLogsRegexPattern =
        confGetter.getConfForScope(universe, UniverseConfKeys.universeLogsRegexPattern);
    String postgresLogsRegexPattern =
        confGetter.getConfForScope(universe, UniverseConfKeys.postgresLogsRegexPattern);
    List<String> fileRegexList = Arrays.asList(universeLogsRegexPattern, postgresLogsRegexPattern);

    // Get and filter master log files that fall within given dates
    String masterLogsPath = nodeHomeDir + "/master/logs";
    List<Path> masterLogFilePaths = new ArrayList<>();
    if (nodeUniverseManager.checkNodeIfFileExists(node, universe, masterLogsPath)) {
      masterLogFilePaths =
          nodeUniverseManager.getNodeFilePaths(
              node, universe, masterLogsPath, /*maxDepth*/ 1, /*fileType*/ "f");
      masterLogFilePaths =
          supportBundleUtil.filterFilePathsBetweenDates(
              masterLogFilePaths, fileRegexList, startDate, endDate);
    }

    // Get and filter tserver log files that fall within given dates
    String tserverLogsPath = nodeHomeDir + "/tserver/logs";
    List<Path> tserverLogFilePaths = new ArrayList<>();
    if (nodeUniverseManager.checkNodeIfFileExists(node, universe, tserverLogsPath)) {
      tserverLogFilePaths =
          nodeUniverseManager.getNodeFilePaths(
              node, universe, tserverLogsPath, /*maxDepth*/ 1, /*fileType*/ "f");
      tserverLogFilePaths =
          supportBundleUtil.filterFilePathsBetweenDates(
              tserverLogFilePaths, fileRegexList, startDate, endDate);
    }

    // Combine both master and tserver files to download all the files together
    List<String> allLogFilePaths =
        Stream.concat(masterLogFilePaths.stream(), tserverLogFilePaths.stream())
            .map(filePath -> Paths.get(nodeHomeDir).relativize(filePath))
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
}
