// Copyright (c) YugabyteDB, Inc.

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
import java.nio.file.NoSuchFileException;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardCopyOption;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.FileUtils;
import org.apache.commons.lang3.StringUtils;

@Slf4j
@Singleton
public class ApplicationLogsComponent implements SupportBundleComponent {

  // Matches the rollover index that logback's %i appends to an archive name, for example the
  // "12" in application-log-2026-08-19.12.gz.
  private static final Pattern ROLLOVER_INDEX_PATTERN = Pattern.compile("\\.(\\d+)(\\.gz)?$");

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
      try {
        Files.copy(sourceFilePath, destFilePath, StandardCopyOption.REPLACE_EXISTING);
      } catch (NoSuchFileException e) {
        // Retention can delete an archive between the listing above and this copy. Rollover is
        // size driven now, so this is frequent enough that it must not fail the whole component.
        log.warn("Log file {} was removed before it could be collected, skipping", sourceFilePath);
      }
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

    // "application.log" is the latest log file that is being updated at the moment. It holds the
    // logs from the last rollover till now, so it is required whenever the requested window
    // extends into the current day.
    Date dateToday = supportBundleUtil.getTodaysDate();
    if (logFiles.contains("application.log") && !endDate.before(dateToday)) {
      filteredLogFiles.add("application.log");
    }

    // Filter the log files by a preliminary check of the name format
    String applicationLogsRegexPattern =
        runtimeConfigFactory
            .globalRuntimeConf()
            .getString("yb.support_bundle.application_logs_regex_pattern");
    List<String> rolledLogFiles =
        supportBundleUtil.filterList(logFiles, Arrays.asList(applicationLogsRegexPattern));

    String applicationLogsSdfPattern =
        runtimeConfigFactory
            .globalRuntimeConf()
            .getString("yb.support_bundle.application_logs_sdf_pattern");
    SimpleDateFormat sdf = new SimpleDateFormat(applicationLogsSdfPattern);
    Map<String, Date> logFileDates = new HashMap<>();
    Map<String, Integer> logFileIndexes = new HashMap<>();
    for (String logFile : rolledLogFiles) {
      // Sometimes the files might be uncompressed already, check for those files too by stripping
      // the extension from all files. stripEnd only removes trailing '.', 'g' and 'z' characters,
      // so it stops at the rollover index and leaves the date for SimpleDateFormat to parse.
      logFileDates.put(logFile, sdf.parse(StringUtils.stripEnd(logFile, ".gz")));
      logFileIndexes.put(logFile, getRolloverIndex(logFile));
    }

    // Sort by date and then by rollover index, both descending, so that the first file found
    // before the start date is the newest archive of the newest date before it. Note the
    // .reversed() applies to the whole key: reversing only the date would leave the index
    // ascending and pick the oldest archive of that date.
    rolledLogFiles.sort(
        Comparator.comparing((String logFile) -> logFileDates.get(logFile))
            .thenComparing((String logFile) -> logFileIndexes.get(logFile))
            .reversed());

    // File names carry a date but no time, so compare against the day of startDate. Otherwise
    // every archive of the start day counts as being before startDate and the boundary branch
    // below would collapse that whole day into a single file.
    Date startDateDay = supportBundleUtil.getDateWithoutTime(startDate);

    // Filters the log files whether it is between startDate and endDate
    for (String logFile : rolledLogFiles) {
      Date fileDate = logFileDates.get(logFile);
      if (supportBundleUtil.checkDateBetweenDates(fileDate, startDateDay, endDate)) {
        filteredLogFiles.add(logFile);
      }
      // Collect one archive from the newest date before the window as a margin against skew
      // between the requested dates and the server-local dates in the file names. The sort
      // above makes this the newest archive of that date, which is the one adjacent to the
      // window, and the break keeps it to a single file.
      else if (fileDate.before(startDateDay)) {
        filteredLogFiles.add(logFile);
        break;
      }
    }

    for (String logFile : filteredLogFiles) {
      Path absolutePath = Paths.get(source.toString(), logFile);
      res.put(absolutePath.toString(), absolutePath.toFile().length());
    }
    return res;
  }

  /**
   * Returns the rollover index logback appended to an archive name, or -1 for a name written before
   * the indexed pattern was introduced. Legacy names therefore order after the indexed archives of
   * the same date, keeping the ordering deterministic while an upgraded node holds both.
   */
  private static int getRolloverIndex(String logFile) {
    Matcher matcher = ROLLOVER_INDEX_PATTERN.matcher(logFile);
    return matcher.find() ? Integer.parseInt(matcher.group(1)) : -1;
  }
}
