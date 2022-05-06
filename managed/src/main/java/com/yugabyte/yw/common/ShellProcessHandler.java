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

import com.google.common.base.Joiner;
import com.google.inject.Inject;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStreamReader;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import javax.inject.Singleton;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.slf4j.Marker;
import org.slf4j.MarkerFactory;

@Singleton
public class ShellProcessHandler {
  public static final Logger LOG = LoggerFactory.getLogger(ShellProcessHandler.class);

  private static final Duration DESTROY_GRACE_TIMEOUT = Duration.ofMinutes(5);

  private final play.Configuration appConfig;
  private final boolean cloudLoggingEnabled;

  @Inject private RuntimeConfigFactory runtimeConfigFactory;

  static final Pattern ANSIBLE_FAIL_PAT =
      Pattern.compile(
          "(ybops.common.exceptions.YBOpsRuntimeError: Runtime error: "
              + "Playbook run.* )with args.* (failed with.*) ");
  static final Pattern ANSIBLE_FAILED_TASK_PAT =
      Pattern.compile("TASK.*?fatal.*?FAILED.*", Pattern.DOTALL);
  static final String ANSIBLE_IGNORING = "ignoring";
  static final String COMMAND_OUTPUT_LOGS_DELETE = "yb.logs.cmdOutputDelete";
  static final String YB_LOGS_MAX_MSG_SIZE = "yb.logs.max_msg_size";

  @Inject
  public ShellProcessHandler(play.Configuration appConfig) {
    this.appConfig = appConfig;
    this.cloudLoggingEnabled = appConfig.getBoolean("yb.cloud.enabled");
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
    return run(command, extraEnvVars, logCmdOutput, description, null, null, 0 /*timeoutSecs*/);
  }

  /**
   * *
   *
   * @param command - command to run with list of args
   * @param extraEnvVars - env vars for this command
   * @param logCmdOutput - whether to log stdout&stderr to application.log or not
   * @param description - human readable description for logging
   * @param uuid - used to track this execution, can be null
   * @param sensitiveData - Args that will be added to the cmd but will be redacted in logs
   * @param timeoutSecs - Abort the command forcibly if it takes longer than this
   * @return
   */
  public ShellResponse run(
      List<String> command,
      Map<String, String> extraEnvVars,
      boolean logCmdOutput,
      String description,
      UUID uuid,
      Map<String, String> sensitiveData,
      int timeoutSecs) {

    List<String> redactedCommand = new ArrayList<>(command);

    // Redacting the sensitive data in the command which is used for logging.
    if (sensitiveData != null) {
      sensitiveData.forEach(
          (key, value) -> {
            redactedCommand.add(key);
            command.add(key);
            command.add(value);
            redactedCommand.add(Util.redactString(value));
          });
    }

    ProcessBuilder pb = new ProcessBuilder(command);
    Map<String, String> envVars = pb.environment();
    if (extraEnvVars != null && !extraEnvVars.isEmpty()) {
      envVars.putAll(extraEnvVars);
    }
    String devopsHome = appConfig.getString("yb.devops.home");
    if (devopsHome != null) {
      pb.directory(new File(devopsHome));
    }

    ShellResponse response = new ShellResponse();
    response.code = ERROR_CODE_GENERIC_ERROR;
    if (description == null) {
      response.setDescription(redactedCommand);
    } else {
      response.description = description;
    }

    File tempOutputFile = null;
    File tempErrorFile = null;
    long startMs = 0;
    Process process = null;
    try {
      tempOutputFile = File.createTempFile("shell_process_out", "tmp");
      tempErrorFile = File.createTempFile("shell_process_err", "tmp");
      pb.redirectOutput(tempOutputFile);
      pb.redirectError(tempErrorFile);
      startMs = System.currentTimeMillis();
      LOG.info("Starting proc (abbrev cmd) - {}", response.description);
      String fullCommand = "'" + String.join("' '", redactedCommand) + "'";
      if (appConfig.getBoolean("yb.log.logEnvVars", false) && extraEnvVars != null) {
        fullCommand = Joiner.on(" ").withKeyValueSeparator("=").join(extraEnvVars) + fullCommand;
      }
      LOG.debug(
          "Starting proc (full cmd) - {} - logging stdout={}, stderr={}",
          fullCommand,
          tempOutputFile.getAbsolutePath(),
          tempErrorFile.getAbsolutePath());

      long endTimeSecs = 0;
      if (timeoutSecs > 0) {
        endTimeSecs = (System.currentTimeMillis() / 1000) + timeoutSecs;
      }
      process = pb.start();
      if (uuid != null) {
        Util.setPID(uuid, process);
      }
      waitForProcessExit(process, description, tempOutputFile, tempErrorFile, endTimeSecs);
      // We will only read last 20MB of process stderr file.
      // stdout has `data` so we wont limit that.
      try (BufferedReader outputStream = getLastNReader(tempOutputFile, Long.MAX_VALUE);
          BufferedReader errorStream = getLastNReader(tempErrorFile, getMaxLogMsgSize())) {
        if (logCmdOutput) {
          LOG.debug("Proc stdout for '{}' :", response.description);
        }
        StringBuilder processOutput = new StringBuilder();
        Marker fileOnly = MarkerFactory.getMarker("fileOnly");
        Marker consoleOnly = MarkerFactory.getMarker("consoleOnly");

        outputStream
            .lines()
            .forEach(
                line -> {
                  processOutput.append(line).append("\n");
                  if (logCmdOutput) {
                    LOG.debug(fileOnly, line);
                  }
                });

        if (logCmdOutput && cloudLoggingEnabled && processOutput.length() > 0) {
          LOG.debug(consoleOnly, processOutput.toString());
        }

        if (logCmdOutput) {
          LOG.debug("Proc stderr for '{}' :", response.description);
        }
        StringBuilder processError = new StringBuilder();
        errorStream
            .lines()
            .forEach(
                line -> {
                  processError.append(line).append("\n");
                  if (logCmdOutput) {
                    LOG.debug(fileOnly, line);
                  }
                });

        if (logCmdOutput && cloudLoggingEnabled && processError.length() > 0) {
          LOG.debug(consoleOnly, processError.toString());
        }

        try {
          response.code = process.exitValue();
        } catch (IllegalThreadStateException itse) {
          response.code = ERROR_CODE_GENERIC_ERROR;
          LOG.warn(
              "Expected process to be shut down, marking this process as failed '{}'",
              response.description,
              itse);
        }
        response.message =
            (response.code == ERROR_CODE_SUCCESS)
                ? processOutput.toString().trim()
                : processError.toString().trim();
        String ansibleErrMsg =
            getAnsibleErrMsg(response.code, processOutput.toString(), processError.toString());
        if (ansibleErrMsg != null) {
          response.message = ansibleErrMsg;
        }
      }
    } catch (IOException | InterruptedException e) {
      response.code = ERROR_CODE_GENERIC_ERROR;
      if (e instanceof InterruptedException) {
        response.code = ERROR_CODE_EXECUTION_CANCELLED;
      }
      LOG.error("Exception running command '{}'", response.description, e);
      response.message = e.getMessage();
      // Send a kill signal to ensure process is cleaned up in case of any failure.
      if (process != null && process.isAlive()) {
        // Only destroy sends SIGTERM to the process.
        process.destroy();
        try {
          process.waitFor(DESTROY_GRACE_TIMEOUT.getSeconds(), TimeUnit.SECONDS);
        } catch (InterruptedException e1) {
          LOG.error(
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
      LOG.info(
          "Completed proc '{}' status={} [ {} ms ]",
          response.description,
          status,
          response.durationMs);
      if (runtimeConfigFactory.globalRuntimeConf().getBoolean(COMMAND_OUTPUT_LOGS_DELETE)) {
        if (tempOutputFile != null && tempOutputFile.exists()) {
          tempOutputFile.delete();
        }
        if (tempErrorFile != null && tempErrorFile.exists()) {
          tempErrorFile.delete();
        }
      }
    }

    return response;
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
        LOG.warn("Skipped first {} bytes because max_msg_size= {}", reader.skip(skip), lastNBytes);
      } catch (IOException e) {
        LOG.warn("Unexpected exception when skipping large file", e);
      }
    }
    return reader;
  }

  public ShellResponse run(List<String> command, Map<String, String> extraEnvVars) {
    return run(command, extraEnvVars, true /*logCommandOutput*/);
  }

  public ShellResponse run(List<String> command, Map<String, String> extraEnvVars, UUID uuid) {
    return run(
        command, extraEnvVars, true /*logCommandOutput*/, null, uuid, null, 0 /*timeoutSecs*/);
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
        extraEnvVars,
        true /*logCommandOutput*/,
        description,
        null,
        sensitiveData,
        0 /*timeoutSecs*/);
  }

  private static void waitForProcessExit(
      Process process, String description, File outFile, File errFile, long endTimeSecs)
      throws IOException, InterruptedException {
    try (FileInputStream outputInputStream = new FileInputStream(outFile);
        InputStreamReader outputReader = new InputStreamReader(outputInputStream);
        FileInputStream errInputStream = new FileInputStream(errFile);
        InputStreamReader errReader = new InputStreamReader(errInputStream);
        BufferedReader outputStream = new BufferedReader(outputReader);
        BufferedReader errorStream = new BufferedReader(errReader)) {
      while (!process.waitFor(1, TimeUnit.SECONDS)) {
        // read a limited number of lines so that we don't
        // get stuck infinitely without getting to the time check
        tailStream(outputStream, 10000 /*maxLines*/);
        tailStream(errorStream, 10000 /*maxLines*/);
        if (endTimeSecs > 0 && ((System.currentTimeMillis() / 1000) >= endTimeSecs)) {
          LOG.warn("Aborting command {} forcibly because it took too long", description);
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
      if (line.contains("[app]")) {
        LOG.info(line);
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
      LOG.info("Process was succesfully forcibly terminated '{}'", description);
    } catch (InterruptedException ie) {
      LOG.warn("Ignoring problem with forcible process termination '{}'", description, ie);
    }
  }

  private static String getAnsibleErrMsg(int code, String stdout, String stderr) {

    if (stderr == null || code == ERROR_CODE_SUCCESS) return null;

    String result = null;

    Matcher ansibleFailMatch = ANSIBLE_FAIL_PAT.matcher(stderr);
    if (ansibleFailMatch.find()) {
      result = ansibleFailMatch.group(1) + ansibleFailMatch.group(2);

      // Attempt to find a line in ansible stdout for the failed task.
      // Logs for each task are separated by empty lines.
      // Some fatal failures are ignored by ansible, so skip them
      for (String s : stdout.split("\\R\\R")) {
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
}
