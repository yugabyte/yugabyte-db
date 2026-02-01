// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.common.supportbundle;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardCopyOption;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.FileUtils;
import org.apache.commons.lang3.StringUtils;

@Slf4j
@Singleton
public class ApplicationLogsComponent implements SupportBundleComponent {

  protected final Config config;
  private final RuntimeConfigFactory runtimeConfigFactory;
  private final SupportBundleUtil supportBundleUtil;

  @Inject
  public ApplicationLogsComponent(
      BaseTaskDependencies baseTaskDependencies, SupportBundleUtil supportBundleUtil) {
    this.config = baseTaskDependencies.getConfig();
    this.runtimeConfigFactory = baseTaskDependencies.getRuntimeConfigFactory();
    this.supportBundleUtil = supportBundleUtil;
  }

  @Override
  public void downloadComponent(
      SupportBundleTaskParams supportBundleTaskParams,
      Customer customer,
      Universe universe,
      Path bundlePath,
      NodeDetails node)
      throws IOException {
    String logDir = config.getString("log.override.path");
    String destDir = bundlePath.toString() + "/application_logs";
    Path destPath = Paths.get(destDir);
    Files.createDirectories(destPath);
    File source = new File(logDir);
    File dest = new File(destDir);
    FileUtils.copyDirectory(source, dest);
    log.debug("Downloaded application logs to {}", destDir);
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

    // Create "application_logs" folder inside the support bundle folder
    String destDir = bundlePath.toString() + "/application_logs";
    Files.createDirectories(Paths.get(destDir));
    File dest = new File(destDir);

    // Set of absolute paths to be copied to the support bundle directory
    Set<String> filteredLogFiles =
        getFilesListWithSizes(customer, null, universe, startDate, endDate, node).keySet();

    // Copy individual files from source directory to the support bundle folder
    for (String filteredLogFile : filteredLogFiles) {
      Path sourceFilePath = Paths.get(filteredLogFile);
      Path destFilePath =
          Paths.get(dest.toString(), Paths.get(filteredLogFile).getFileName().toString());
      Files.copy(sourceFilePath, destFilePath, StandardCopyOption.REPLACE_EXISTING);
    }

    log.debug("Downloaded application logs to {}, between {} and {}", destDir, startDate, endDate);
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
    // Get application configured locations
    String appHomeDir = config.getString("application.home");
    log.info("[ApplicationLogsComponent] appHomeDir = '{}'", appHomeDir);
    String logDir = config.getString("log.override.path");
    Path logPath = Paths.get(logDir);
    String logDirAbsolute = logPath.toAbsolutePath().toString();
    log.info("[ApplicationLogsComponent] logDir = '{}'", logDir);
    log.info("[ApplicationLogsComponent] logDirAbsolute = '{}'", logDirAbsolute);

    File source = new File(logDirAbsolute);

    // Get all the log file names present in source directory
    List<String> logFiles = new ArrayList<>();
    File[] sourceFiles = source.listFiles();
    if (sourceFiles == null) {
      log.info("[ApplicationLogsComponent] sourceFiles = null");
    } else {
      log.info(
          "[ApplicationLogsComponent] sourceFiles.length = '{}'",
          String.valueOf(sourceFiles.length));
      for (File sourceFile : sourceFiles) {
        if (sourceFile.isFile()) {
          logFiles.add(sourceFile.getName());
        }
      }
    }

    // All the logs file names that we want to keep after filtering within start and end date
    List<String> filteredLogFiles = new ArrayList<>();

    // "application.log" is the latest log file that is being updated at the moment
    Date dateToday = supportBundleUtil.getTodaysDate();
    if (logFiles.contains("application.log")
        && supportBundleUtil.checkDateBetweenDates(dateToday, startDate, endDate)) {
      filteredLogFiles.add("application.log");
    }

    // Filter the log files by a preliminary check of the name format
    String applicationLogsRegexPattern =
        runtimeConfigFactory
            .globalRuntimeConf()
            .getString("yb.support_bundle.application_logs_regex_pattern");
    logFiles =
        supportBundleUtil.filterList(logFiles, Arrays.asList(applicationLogsRegexPattern)).stream()
            .collect(Collectors.toList());

    String applicationLogsSdfPattern =
        runtimeConfigFactory
            .globalRuntimeConf()
            .getString("yb.support_bundle.application_logs_sdf_pattern");
    SimpleDateFormat sdf = new SimpleDateFormat(applicationLogsSdfPattern);
    // Filters the log files whether it is between startDate and endDate
    for (String logFile : logFiles) {
      // Sometimes the files might be uncompressed already, check for those files too by stripping
      // the extension from all files.
      String logFileWithoutType = StringUtils.stripEnd(logFile, ".gz");
      Date fileDate = sdf.parse(logFileWithoutType);
      if (supportBundleUtil.checkDateBetweenDates(fileDate, startDate, endDate)) {
        filteredLogFiles.add(logFile);
      }
    }

    for (String logFile : filteredLogFiles) {
      Path absolutePath = Paths.get(source.toString(), logFile);
      res.put(absolutePath.toString(), absolutePath.toFile().length());
    }
    return res;
  }
}
