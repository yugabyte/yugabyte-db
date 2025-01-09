/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.ShellResponse.ERROR_CODE_EXECUTION_CANCELLED;
import static com.yugabyte.yw.common.ShellResponse.ERROR_CODE_GENERIC_ERROR;
import static com.yugabyte.yw.common.ShellResponse.ERROR_CODE_SUCCESS;

import com.fasterxml.jackson.core.type.TypeReference;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.base.Joiner;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.RedactingService.RedactionTarget;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStreamReader;
import java.time.Duration;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.MapUtils;
import org.apache.commons.lang3.StringUtils;
import org.apache.commons.lang3.tuple.Pair;
import org.slf4j.Marker;
import org.slf4j.MarkerFactory;
import play.libs.Json;

@Singleton
@Slf4j
public class ShellProcessHandler {

  private static final Duration DESTROY_GRACE_TIMEOUT = Duration.ofMinutes(5);

  private final Config appConfig;
  private final boolean cloudLoggingEnabled;
  private final ShellLogsManager shellLogsManager;

  static final Pattern ANSIBLE_FAIL_PAT =
      Pattern.compile(
          "(ybops\\.common\\.exceptions\\.YB[^\\s]+Error:.*? Playbook run.*?)with args.* (failed"
              + " with.*? [0-9]+)");
  static final Pattern ANSIBLE_FAILED_TASK_PAT =
      Pattern.compile("TASK\\s+\\[.+\\].*?(fatal:.*?FAILED.*|failed: (?!false).*)", Pattern.DOTALL);
  static final Pattern PYTHON_ERROR_PAT =
      Pattern.compile("(<yb-python-error>)(.*?)(</yb-python-error>)", Pattern.DOTALL);
  static final String ANSIBLE_IGNORING = "ignoring";
  static final String YB_LOGS_MAX_MSG_SIZE = "yb.logs.max_msg_size";

  @Inject
  public ShellProcessHandler(Config appConfig, ShellLogsManager shellLogsManager) {
    this.appConfig = appConfig;
    this.cloudLoggingEnabled = appConfig.getBoolean("yb.cloud.enabled");
    this.shellLogsManager = shellLogsManager;
  }

  public ShellResponse run(
      List<String> command, Map<String, String> extraEnvVars, boolean logCmdOutput) {
    return run(command, extraEnvVars, logCmdOutput, null /*description*/);
  }

  public ShellResponse run(
      List<String> command,
      Map<String, String> extraEnvVars,
      boolean logCmdOutput,
      String description) {
    return run(
        command,
        extraEnvVars,
        logCmdOutput,
        description,
        RedactionTarget.QUERY_PARAMS /*shellOutputRedactionTarget*/);
  }

  public ShellResponse run(
      List<String> command,
      Map<String, String> extraEnvVars,
      boolean logCmdOutput,
      String description,
      RedactionTarget shellOutputRedactionTarget) {
    return run(
        command,
        ShellProcessContext.builder()
            .extraEnvVars(extraEnvVars)
            .logCmdOutput(logCmdOutput)
            .description(description)
            .shellOutputRedactionTarget(shellOutputRedactionTarget)
            .build());
  }

  /**
   * *
   *
   * @param command - command to run with list of args
   * @param context - command context
   * @return shell response
   */
  public ShellResponse run(List<String> command, ShellProcessContext context) {
    List<String> redactedCommand = context.redactCommand(command);
    ProcessBuilder pb = new ProcessBuilder(command);
    Map<String, String> envVars = pb.environment();
    Map<String, String> extraEnvVars = context.getExtraEnvVars();
    if (MapUtils.isNotEmpty(extraEnvVars)) {
      envVars.putAll(extraEnvVars);
    }
    String devopsHome = appConfig.getString("yb.devops.home");
    if (devopsHome != null) {
      pb.directory(new File(devopsHome));
    }

    ShellResponse response = new ShellResponse();
    response.code = ERROR_CODE_GENERIC_ERROR;
    String description =
        context.getDescription() == null
            ? StringUtils.abbreviateMiddle(String.join(" ", redactedCommand), " ... ", 140)
            : context.getDescription();
    response.description = description;

    File tempOutputFile = null;
    File tempErrorFile = null;
    long startMs = 0;
    Process process = null;
    UUID processUUID = context.getUuid() != null ? context.getUuid() : UUID.randomUUID();
    try {
      Pair<File, File> logFiles = shellLogsManager.createFilesForProcess(processUUID);
      tempOutputFile = logFiles.getLeft();
      tempErrorFile = logFiles.getRight();
      pb.redirectOutput(tempOutputFile);
      pb.redirectError(tempErrorFile);
      startMs = System.currentTimeMillis();
      String logMsg = String.format("Starting proc (abbrev cmd) - %s", response.description);
      if (context.isTraceLogging()) {
        log.trace(logMsg);
      } else {
        log.info(logMsg);
      }
      String fullCommand = "'" + String.join("' '", redactedCommand) + "'";
      if (appConfig.getBoolean("yb.log.logEnvVars") && extraEnvVars != null) {
        fullCommand = Joiner.on(" ").withKeyValueSeparator("=").join(extraEnvVars) + fullCommand;
      }
      logMsg =
          String.format(
              "Starting proc (full cmd) - %s - logging stdout=%s, stderr=%s",
              fullCommand, tempOutputFile.getAbsolutePath(), tempErrorFile.getAbsolutePath());
      if (context.isTraceLogging()) {
        log.trace(logMsg);
      } else {
        log.info(logMsg);
      }

      long endTimeMs = 0;
      if (context.getTimeoutSecs() > 0) {
        endTimeMs = System.currentTimeMillis() + context.getTimeoutSecs() * 1000;
      }
      process = pb.start();
      if (context.getUuid() != null) {
        Util.setPID(context.getUuid(), process);
      }
      waitForProcessExit(process, description, tempOutputFile, tempErrorFile, endTimeMs);
      // We will only read last 20MB of process stderr file.
      // stdout has `data` so we wont limit that.
      boolean logCmdOutput = context.isLogCmdOutput();
      try (BufferedReader outputStream = getLastNReader(tempOutputFile, Long.MAX_VALUE);
          BufferedReader errorStream = getLastNReader(tempErrorFile, getMaxLogMsgSize())) {
        if (logCmdOutput) {
          log.debug("Proc stdout for '{}' :", response.description);
        }
        String processOutput =
            getOutputLines(outputStream, logCmdOutput, context.getShellOutputRedactionTarget());
        String processError =
            getOutputLines(errorStream, logCmdOutput, context.getShellOutputRedactionTarget());
        try {
          response.code = process.exitValue();
        } catch (IllegalThreadStateException itse) {
          response.code = ERROR_CODE_GENERIC_ERROR;
          log.warn(
              "Expected process to be shut down, marking this process as failed '{}'",
              response.description,
              itse);
        }
        response.message = (response.code == ERROR_CODE_SUCCESS) ? processOutput : processError;
        String specificErrMsg = getPythonErrMsg(response.code, processOutput);
        if (specificErrMsg != null) {
          String ansibleErrMsg = getAnsibleErrMsg(response.code, specificErrMsg, processError);
          if (ansibleErrMsg != null) {
            specificErrMsg = ansibleErrMsg;
          }
          response.message = specificErrMsg;
        }
      }
    } catch (IOException | InterruptedException e) {
      response.code = ERROR_CODE_GENERIC_ERROR;
      if (e instanceof InterruptedException) {
        response.code = ERROR_CODE_EXECUTION_CANCELLED;
      }
      log.error("Exception running command '{}'", response.description, e);
      response.message = e.getMessage();
      // Send a kill signal to ensure process is cleaned up in case of any failure.
      if (process != null && process.isAlive()) {
        // Only destroy sends SIGTERM to the process.
        process.destroy();
        try {
          process.waitFor(DESTROY_GRACE_TIMEOUT.getSeconds(), TimeUnit.SECONDS);
        } catch (InterruptedException e1) {
          log.error(
              "Process could not be destroyed gracefully within the specified time '{}'",
              response.description);
          destroyForcibly(process, response.description);
        }
      }
    } finally {
      if (startMs > 0) {
        response.durationMs = System.currentTimeMillis() - startMs;
      }
      String status =
          (ERROR_CODE_SUCCESS == response.code) ? "success" : ("failure code=" + response.code);
      String logMsg =
          String.format(
              "Completed proc '%s' status=%s [ %d ms ]",
              response.description, status, response.durationMs);
      if (context.isTraceLogging()) {
        log.trace(logMsg);
      } else {
        log.info(logMsg);
      }
      shellLogsManager.onProcessStop(processUUID);
      if (context.getUuid() != null) {
        // TODO revisit this leak fix for backup for a cleaner approach.
        Util.removeProcess(context.getUuid());
      }
    }
    return response;
  }

  private String getOutputLines(BufferedReader reader, boolean logOutput, RedactionTarget target) {
    Marker fileMarker = MarkerFactory.getMarker("fileOnly");
    Marker consoleMarker = MarkerFactory.getMarker("consoleOnly");
    String lines =
        reader
            .lines()
            .peek(
                line -> {
                  if (logOutput) {
                    log.debug(fileMarker, RedactingService.redactShellProcessOutput(line, target));
                  }
                })
            .collect(Collectors.joining("\n"))
            .trim();
    if (logOutput && cloudLoggingEnabled && lines.length() > 0) {
      log.debug(consoleMarker, RedactingService.redactShellProcessOutput(lines, target));
    }
    return lines;
  }

  private long getMaxLogMsgSize() {
    return appConfig.getBytes(YB_LOGS_MAX_MSG_SIZE);
  }

  /** For a given file return a bufferred reader that reads only last N bytes. */
  private static BufferedReader getLastNReader(File file, long lastNBytes)
      throws FileNotFoundException {
    final BufferedReader reader =
        new BufferedReader(new InputStreamReader(new FileInputStream(file)));
    long skip = file.length() - lastNBytes;
    if (skip > 0) {
      try {
        log.warn("Skipped first {} bytes because max_msg_size= {}", reader.skip(skip), lastNBytes);
      } catch (IOException e) {
        log.warn("Unexpected exception when skipping large file", e);
      }
    }
    return reader;
  }

  public ShellResponse run(List<String> command, Map<String, String> extraEnvVars) {
    return run(command, extraEnvVars, true /*logCommandOutput*/);
  }

  public ShellResponse run(List<String> command, Map<String, String> extraEnvVars, UUID uuid) {
    return run(
        command,
        ShellProcessContext.builder()
            .extraEnvVars(extraEnvVars)
            .logCmdOutput(true)
            .uuid(uuid)
            .build());
  }

  public ShellResponse run(
      List<String> command, Map<String, String> extraEnvVars, String description) {
    return run(command, extraEnvVars, true /*logCommandOutput*/, description);
  }

  public ShellResponse run(
      List<String> command,
      Map<String, String> extraEnvVars,
      String description,
      Map<String, String> sensitiveData) {
    return run(
        command,
        ShellProcessContext.builder()
            .extraEnvVars(extraEnvVars)
            .logCmdOutput(true)
            .description(description)
            .sensitiveData(sensitiveData)
            .build());
  }

  // This method is copied mostly from Process.waitFor(long, TimeUnit) to make poll interval longer
  // than the default 100ms.
  private static boolean waitFor(Process process, Duration timeout, Duration pollInterval)
      throws InterruptedException {
    long remMs = timeout.toMillis();
    long timeoutMs = timeout.toMillis();
    long pollIntervalMs = pollInterval.toMillis();
    long startTime = System.currentTimeMillis();

    while (true) {
      try {
        process.exitValue();
        return true;
      } catch (IllegalThreadStateException e) {
        if (remMs < 0) {
          // A last call to exitValue() has been made once before exiting.
          break;
        }
      }
      remMs = timeoutMs - (System.currentTimeMillis() - startTime);
      if (remMs > 0) {
        Thread.sleep(Math.min(remMs + 1, pollIntervalMs));
      }
    }
    return false;
  }

  private static void waitForProcessExit(
      Process process, String description, File outFile, File errFile, long endTimeMs)
      throws IOException, InterruptedException {
    try (FileInputStream outputInputStream = new FileInputStream(outFile);
        InputStreamReader outputReader = new InputStreamReader(outputInputStream);
        FileInputStream errInputStream = new FileInputStream(errFile);
        InputStreamReader errReader = new InputStreamReader(errInputStream);
        BufferedReader outputStream = new BufferedReader(outputReader);
        BufferedReader errorStream = new BufferedReader(errReader)) {
      while (!waitFor(process, Duration.ofSeconds(1), Duration.ofMillis(500))) {
        // read a limited number of lines so that we don't
        // get stuck infinitely without getting to the time check
        tailStream(outputStream, 10000 /*maxLines*/);
        tailStream(errorStream, 10000 /*maxLines*/);
        if (endTimeMs > 0 && (System.currentTimeMillis() >= endTimeMs)) {
          log.warn("Aborting command {} forcibly because it took too long", description);
          destroyForcibly(process, description);
          break;
        }
      }
      // check for any remaining lines
      tailStream(outputStream);
      tailStream(errorStream);
    }
  }

  private static void tailStream(BufferedReader br) throws IOException {
    tailStream(br, 0 /*maxLines*/);
  }

  private static void tailStream(BufferedReader br, long maxLines) throws IOException {

    String line;
    long count = 0;
    // Note: technically, this readLine can pick up incomplete lines as we race
    // with the process output being appended to this file but for the purposes
    // of logging, it is ok to log partial lines.
    while ((line = br.readLine()) != null) {
      line = line.trim();
      if (line.contains("[app]")) {
        log.info(line);
      }
      count++;
      if (maxLines > 0 && count >= maxLines) {
        return;
      }
    }
  }

  private static void destroyForcibly(Process process, String description) {
    process.destroyForcibly();
    try {
      process.waitFor(DESTROY_GRACE_TIMEOUT.getSeconds(), TimeUnit.SECONDS);
      log.info("Process was succesfully forcibly terminated '{}'", description);
    } catch (InterruptedException ie) {
      log.warn("Ignoring problem with forcible process termination '{}'", description, ie);
    }
  }

  private static String getAnsibleErrMsg(int code, String pythonErrMsg, String stderr) {

    if (pythonErrMsg == null || stderr == null || code == ERROR_CODE_SUCCESS) return null;

    String result = null;

    Matcher ansibleFailMatch = ANSIBLE_FAIL_PAT.matcher(pythonErrMsg);
    if (ansibleFailMatch.find()) {
      result = ansibleFailMatch.group(1) + ansibleFailMatch.group(2);

      // By default, python logging module writes to stderr.
      // Attempt to find a line in ansible stderr for the failed task.
      // Logs for each task are separated by empty lines.
      // Some fatal failures are ignored by ansible, so skip them.
      for (String s : stderr.split("\\R\\R")) {
        if (s.contains(ANSIBLE_IGNORING)) {
          continue;
        }
        Matcher m = ANSIBLE_FAILED_TASK_PAT.matcher(s);
        if (m.find()) {
          result += "\n" + m.group(0);
        }
      }
    }
    return result;
  }

  @VisibleForTesting
  static String getPythonErrMsg(int code, String stdout) {
    if (stdout == null || code == ERROR_CODE_SUCCESS) return null;

    try {
      Matcher matcher = PYTHON_ERROR_PAT.matcher(stdout);
      if (matcher.find()) {
        Map<String, String> values =
            Json.mapper()
                .readValue(matcher.group(2).trim(), new TypeReference<Map<String, String>>() {});
        return values.get("type") + ": " + values.get("message");
      }
    } catch (Exception e) {
      log.error("Error occurred in processing command output", e);
    }
    return null;
  }
}
