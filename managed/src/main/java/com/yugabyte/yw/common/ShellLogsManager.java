/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.utils.FileUtils.getOrCreateTmpDirectory;

import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import java.io.File;
import java.io.IOException;
import java.nio.file.Path;
import java.time.Duration;
import java.time.temporal.ChronoUnit;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.Date;
import java.util.Iterator;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.TimeUnit;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.FileUtils;
import org.apache.commons.lang3.tuple.Pair;

@Singleton
@Slf4j
public class ShellLogsManager {
  private final PlatformScheduler platformScheduler;
  private final String customLogsDir;
  private final Config config;
  private final RuntimeConfGetter confGetter;
  static final String YB_LOGS_SHELL_OUTPUT_DIR_KEY = "yb.logs.shell.output_dir"; // Optional
  static final String YB_LOGS_SHELL_OUTPUT_RETENTION_HOURS_KEY =
      "yb.logs.shell.output_retention_hours";
  static final String YB_LOGS_SHELL_OUTPUT_DIR_MAX_SIZE_KEY = "yb.logs.shell.output_dir_max_size";
  static final String COMMAND_OUTPUT_LOGS_DELETE_KEY = "yb.logs.cmdOutputDelete";
  private static final int SHELL_LOGS_GC_INTERVAL_HOURS = 1;
  private static final String STDERR_PREFIX = "shell_process_err";
  private static final String STDOUT_PREFIX = "shell_process_out";

  protected final Map<UUID, Pair<File, File>> currentProcesses = new ConcurrentHashMap<>();

  @Inject
  public ShellLogsManager(
      PlatformScheduler platformScheduler,
      RuntimeConfigFactory runtimeConfigFactory,
      RuntimeConfGetter confGetter) {
    this.platformScheduler = platformScheduler;
    this.config = runtimeConfigFactory.globalRuntimeConf();
    this.customLogsDir =
        config.hasPath(YB_LOGS_SHELL_OUTPUT_DIR_KEY)
            ? config.getString(YB_LOGS_SHELL_OUTPUT_DIR_KEY)
            : null;
    this.confGetter = confGetter;
  }

  public void startLogsGC() {
    if (isRetainLogs()) {
      platformScheduler.schedule(
          getClass().getSimpleName(),
          Duration.ZERO,
          Duration.of(SHELL_LOGS_GC_INTERVAL_HOURS, ChronoUnit.HOURS),
          () -> {
            this.rotateLogs(getMaxCustomDirSizeBytes(), getLogsRetentionHours());
          });
    } else {
      log.debug("Not starting logs gc (no retention policy configured)");
    }
  }

  private File getParentDir() {
    return customLogsDir == null
        ? new File(System.getProperty("java.io.tmpdir"))
        : new File(customLogsDir);
  }

  protected Integer getLogsRetentionHours() {
    return config.hasPath(YB_LOGS_SHELL_OUTPUT_RETENTION_HOURS_KEY)
        ? config.getInt(YB_LOGS_SHELL_OUTPUT_RETENTION_HOURS_KEY)
        : null;
  }

  protected Long getMaxCustomDirSizeBytes() {
    return config.hasPath(YB_LOGS_SHELL_OUTPUT_DIR_MAX_SIZE_KEY)
        ? config.getBytes(YB_LOGS_SHELL_OUTPUT_DIR_MAX_SIZE_KEY)
        : null;
  }

  protected List<File> listLogFiles() {
    File parentDir = getParentDir();
    if (!parentDir.exists()) {
      return Collections.emptyList();
    }
    File[] logFiles =
        parentDir.listFiles(
            (file) ->
                file.getName().startsWith(STDERR_PREFIX)
                    || file.getName().startsWith(STDOUT_PREFIX));
    return Arrays.asList(logFiles);
  }

  protected Long getBaseTimestamp(long retentionHours) {
    return System.currentTimeMillis() - TimeUnit.HOURS.toMillis(retentionHours);
  }

  void rotateLogs(Long maxDirSize, Integer retentionHours) {
    log.debug(
        "Starting rotating logs, max dir size {} retention hours {}", maxDirSize, retentionHours);
    if (maxDirSize == null && retentionHours == null) {
      log.error("No logs retention policy provided!");
      return;
    }
    try {
      List<File> logFiles = new LinkedList<>(listLogFiles());
      long totalSize = 0;
      for (File logFile : logFiles) {
        totalSize += logFile.length();
      }
      final long initialTotalSize = totalSize;
      final int originalSize = logFiles.size();
      log.debug(
          "Found {} log files {} total size",
          originalSize,
          FileUtils.byteCountToDisplaySize(totalSize));
      logFiles.sort(
          Comparator.comparingLong(File::lastModified)
              .thenComparing(Comparator.comparingLong(File::length).reversed()));

      Set<String> currentlyUsedLogs =
          currentProcesses.values().stream()
              .flatMap(p -> Stream.of(p.getLeft(), p.getRight()))
              .map(f -> f.getName())
              .collect(Collectors.toSet());

      if (retentionHours != null) {
        Long baseTimestamp = getBaseTimestamp(retentionHours);
        Iterator<File> fileIterator = logFiles.iterator();
        File file;
        while (fileIterator.hasNext()
            && (file = fileIterator.next()).lastModified() <= baseTimestamp) {
          if (!currentlyUsedLogs.contains(file.getName())) {
            log.debug("Deleting {} modified {} ", file.getName(), new Date(file.lastModified()));
            totalSize -= file.length();
            file.delete();
            fileIterator.remove();
          }
        }
        log.debug(
            "Removed {} files older than {} with total size {}",
            originalSize - logFiles.size(),
            new Date(baseTimestamp),
            FileUtils.byteCountToDisplaySize(initialTotalSize - totalSize));
      }
      if (maxDirSize != null) {
        Iterator<File> fileIterator = logFiles.iterator();
        while (totalSize > maxDirSize && fileIterator.hasNext()) {
          File file = fileIterator.next();
          if (!currentlyUsedLogs.contains(file.getName())) {
            totalSize -= file.length();
            file.delete();
            fileIterator.remove();
          }
        }
      }
      log.debug(
          "{} Logs after cleaning, total size {}",
          logFiles.size(),
          FileUtils.byteCountToDisplaySize(totalSize));
    } catch (Exception e) {
      log.error("Failed to rotate shell logs", e);
    }
  }

  private boolean isRetainLogs() {
    return getLogsRetentionHours() != null || getMaxCustomDirSizeBytes() != null;
  }

  public Pair<File, File> createFilesForProcess(UUID processUUID) throws IOException {
    Pair<File, File> result = Pair.of(createLogsFile(false), createLogsFile(true));
    currentProcesses.put(processUUID, result);
    return result;
  }

  public void onProcessStop(UUID processUUID) {
    Pair<File, File> pair = currentProcesses.remove(processUUID);
    if (pair != null) {
      if (config.getBoolean(COMMAND_OUTPUT_LOGS_DELETE_KEY) && !isRetainLogs()) {
        if (pair.getLeft().exists()) {
          pair.getLeft().delete();
        }
        if (pair.getRight().exists()) {
          pair.getRight().delete();
        }
      }
    }
  }

  protected File createLogsFile(boolean stderr) throws IOException {
    String prefix = stderr ? STDERR_PREFIX : STDOUT_PREFIX;
    File parentDir = null;
    if (customLogsDir != null) {
      parentDir = new File(customLogsDir);
    } else {
      Path tmpDirectoryPath =
          getOrCreateTmpDirectory(confGetter.getGlobalConf(GlobalConfKeys.ybTmpDirectoryPath));
      parentDir = tmpDirectoryPath.toFile();
    }
    if (!parentDir.exists()) {
      parentDir.mkdirs();
    }

    return File.createTempFile(prefix, "tmp", parentDir);
  }
}
