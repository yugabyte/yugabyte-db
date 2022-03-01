package com.yugabyte.yw.common.supportbundle;

import com.typesafe.config.Config;
import com.yugabyte.yw.controllers.handlers.UniverseInfoHandler;
import com.yugabyte.yw.models.Customer;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.InstanceType;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Collections;
import java.util.Date;
import java.util.List;
import java.util.Arrays;
import java.util.ArrayList;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import java.util.UUID;
import java.io.IOException;
import java.text.SimpleDateFormat;
import java.text.ParseException;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Singleton
class UniverseLogsComponent implements SupportBundleComponent {

  private final UniverseInfoHandler universeInfoHandler;
  private final NodeUniverseManager nodeUniverseManager;
  protected final Config config;

  @Inject
  UniverseLogsComponent(
      UniverseInfoHandler universeInfoHandler,
      NodeUniverseManager nodeUniverseManager,
      Config config) {
    this.universeInfoHandler = universeInfoHandler;
    this.nodeUniverseManager = nodeUniverseManager;
    this.config = config;
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
          SupportBundleUtil.getDataDirPath(universe, node, nodeUniverseManager, config);
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
          SupportBundleUtil.getDataDirPath(universe, node, nodeUniverseManager, config);
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
          String.format(
              config.getString("yb.support_bundle.universe_logs_regex_pattern"), node.nodeName);

      // Get and filter master log files that fall within given dates
      List<String> masterLogFilePaths =
          getNodeFilePaths(node, universe, nodeHomeDir + "/master/logs", 1, "f");
      masterLogFilePaths =
          filterFilePathsBetweenDates(
              masterLogFilePaths, universeLogsRegexPattern, startDate, endDate);

      // Get and filter tserver log files that fall within given dates
      List<String> tserverLogFilePaths =
          getNodeFilePaths(node, universe, nodeHomeDir + "/tserver/logs", 1, "f");
      tserverLogFilePaths =
          filterFilePathsBetweenDates(
              tserverLogFilePaths, universeLogsRegexPattern, startDate, endDate);

      // Combine both master and tserver files to download all the files together
      List<String> allLogFilePaths =
          Stream.concat(masterLogFilePaths.stream(), tserverLogFilePaths.stream())
              .collect(Collectors.toList());

      Path targetFile =
          universeInfoHandler.downloadNodeFile(
              customer,
              universe,
              node,
              nodeHomeDir,
              String.join(";", allLogFilePaths),
              nodeTargetFile);
    }
  }

  // Gets a list of all the absolute file paths at a given remote directory
  public List<String> getNodeFilePaths(
      NodeDetails node, Universe universe, String remoteDirPath, int maxDepth, String fileType) {
    List<String> command = new ArrayList<>();
    command.add("find");
    command.add(remoteDirPath);
    command.add("-maxdepth");
    command.add(String.valueOf(maxDepth));
    command.add("-type");
    command.add(fileType);
    String cmd = String.join(" ", command);

    ShellResponse shellOutput = this.nodeUniverseManager.runCommand(node, universe, cmd);
    String cmdOutput = shellOutput.message;
    List<String> cmdOutputList = Arrays.asList(cmdOutput.trim().split("\n", 0));
    // Removes all warnings before string "Command output:"
    int lastIndex = 0;
    for (int i = 0; i < cmdOutputList.size(); ++i) {
      if (cmdOutputList.get(i).contains("Command output:")) {
        lastIndex = i;
      }
    }
    return cmdOutputList.subList(lastIndex + 1, cmdOutputList.size());
  }

  // Filters a list of log file paths with a regex pattern and between given start and end dates
  public List<String> filterFilePathsBetweenDates(
      List<String> logFilePaths, String universeLogsRegexPattern, Date startDate, Date endDate)
      throws ParseException {
    // Filtering the file names based on regex
    logFilePaths = SupportBundleUtil.filterList(logFilePaths, universeLogsRegexPattern);

    // Sort the files in descending order of date (done implicitly as date format is yyyyMMdd)
    Collections.sort(logFilePaths, Collections.reverseOrder());

    // Core logic for a loose bound filtering based on dates (little bit tricky):
    // Gets all the files which have logs for requested time period,
    // even when partial log statements present in the file.
    // ----------------------------------------
    // Ex: Assume log files are as follows (d1 = day 1, d2 = day 2, ... in sorted order)
    // => d1.gz, d2.gz, d5.gz
    // => And user requested {startDate = d3, endDate = d6}
    // ----------------------------------------
    // => Output files will be: {d2.gz, d5.gz}
    // Due to d2.gz having all the logs from d2-d4, therefore overlapping with given startDate
    Date minDate = null;
    List<String> filteredLogFilePaths = new ArrayList<>();
    for (String filePath : logFilePaths) {
      String fileName =
          filePath.substring(filePath.lastIndexOf('/') + 1, filePath.lastIndexOf('-'));
      // Need trimmed file path starting from {./master or ./tserver} for above function
      String trimmedFilePath = filePath.split("yb-data/")[1];
      Matcher fileNameMatcher = Pattern.compile(universeLogsRegexPattern).matcher(filePath);
      if (fileNameMatcher.matches()) {
        String fileNameSdfPattern = "yyyyMMdd";
        // Uses capturing and non capturing groups in regex pattern for easier retrieval of
        // neccessary info. Group 3 = the "yyyyMMdd" format in the file name.
        Date fileDate = new SimpleDateFormat(fileNameSdfPattern).parse(fileNameMatcher.group(3));
        if (SupportBundleUtil.checkDateBetweenDates(fileDate, startDate, endDate)) {
          filteredLogFilePaths.add(trimmedFilePath);
        } else if ((minDate == null && fileDate.before(startDate))
            || (minDate != null && fileDate.equals(minDate))) {
          filteredLogFilePaths.add(trimmedFilePath);
          minDate = fileDate;
        }
      }
    }
    return filteredLogFilePaths;
  }
}
