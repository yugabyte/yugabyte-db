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

import com.google.common.base.Joiner;
import com.google.inject.Inject;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import javax.inject.Singleton;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Singleton
public class ShellProcessHandler {
  public static final Logger LOG = LoggerFactory.getLogger(ShellProcessHandler.class);

  @Inject play.Configuration appConfig;

  static final Pattern ANSIBLE_FAIL_PAT =
      Pattern.compile(
          "(ybops.common.exceptions.YBOpsRuntimeError: Runtime error: "
              + "Playbook run.* )with args.* (failed with.*) ");
  static final Pattern ANSIBLE_FAILED_TASK_PAT =
      Pattern.compile("TASK.*?fatal.*?FAILED.*", Pattern.DOTALL);
  static final String ANSIBLE_IGNORING = "ignoring";

  public ShellResponse run(
      List<String> command, Map<String, String> extraEnvVars, boolean logCmdOutput) {
    return run(command, extraEnvVars, logCmdOutput, null /*description*/);
  }

  public ShellResponse run(
      List<String> command,
      Map<String, String> extraEnvVars,
      boolean logCmdOutput,
      String description) {
    return run(command, extraEnvVars, logCmdOutput, description, null);
  }

  public ShellResponse run(
      List<String> command,
      Map<String, String> extraEnvVars,
      boolean logCmdOutput,
      String description,
      UUID uuid) {
    ProcessBuilder pb = new ProcessBuilder(command);
    Map envVars = pb.environment();
    if (extraEnvVars != null && !extraEnvVars.isEmpty()) {
      envVars.putAll(extraEnvVars);
    }
    String devopsHome = appConfig.getString("yb.devops.home");
    if (devopsHome != null) {
      pb.directory(new File(devopsHome));
    }

    ShellResponse response = new ShellResponse();
    response.code = -1;
    if (description == null) {
      response.setDescription(command);
    } else {
      response.description = description;
    }

    File tempOutputFile = null;
    File tempErrorFile = null;
    long startMs = 0;
    try {
      tempOutputFile = File.createTempFile("shell_process_out", "tmp");
      tempErrorFile = File.createTempFile("shell_process_err", "tmp");
      pb.redirectOutput(tempOutputFile);
      pb.redirectError(tempErrorFile);
      startMs = System.currentTimeMillis();
      LOG.info("Starting proc (abbrev cmd) - {}", response.description);
      String fullCommand = "'" + String.join("' '", command) + "'";
      if (appConfig.getBoolean("yb.log.logEnvVars", false) && extraEnvVars != null) {
        fullCommand = Joiner.on(" ").withKeyValueSeparator("=").join(extraEnvVars) + fullCommand;
      }
      LOG.debug(
          "Starting proc (full cmd) - {} - logging stdout={}, stderr={}",
          fullCommand,
          tempOutputFile.getAbsolutePath(),
          tempErrorFile.getAbsolutePath());

      Process process = pb.start();
      if (uuid != null) {
        Util.setPID(uuid, process);
      }
      // TimeUnit.MINUTES.sleep(5);
      waitForProcessExit(process, tempOutputFile, tempErrorFile);
      try (FileInputStream outputInputStream = new FileInputStream(tempOutputFile);
          InputStreamReader outputReader = new InputStreamReader(outputInputStream);
          BufferedReader outputStream = new BufferedReader(outputReader);
          FileInputStream errorInputStream = new FileInputStream(tempErrorFile);
          InputStreamReader errorReader = new InputStreamReader(errorInputStream);
          BufferedReader errorStream = new BufferedReader(errorReader)) {
        if (logCmdOutput) {
          LOG.debug("Proc stdout for '{}' :", response.description);
        }
        StringBuilder processOutput = new StringBuilder();
        outputStream
            .lines()
            .forEach(
                line -> {
                  processOutput.append(line).append("\n");
                  if (logCmdOutput) {
                    LOG.debug(line);
                  }
                });
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
                    LOG.debug(line);
                  }
                });
        response.code = process.exitValue();
        response.message =
            (response.code == 0) ? processOutput.toString().trim() : processError.toString().trim();
        String ansibleErrMsg =
            getAnsibleErrMsg(response.code, processOutput.toString(), processError.toString());
        if (ansibleErrMsg != null) {
          response.message = ansibleErrMsg;
        }
      }
    } catch (IOException | InterruptedException e) {
      response.code = -1;
      LOG.error("Exception running command '{}'", response.description, e);
      response.message = e.getMessage();
    } finally {
      if (startMs > 0) {
        response.durationMs = System.currentTimeMillis() - startMs;
      }
      String status =
          (0 == response.code) ? "success" : ("failure code=" + Integer.toString(response.code));
      LOG.info(
          "Completed proc '{}' status={} [ {} ms ]",
          response.description,
          status,
          response.durationMs);
      if (tempOutputFile != null && tempOutputFile.exists()) {
        tempOutputFile.delete();
      }
      if (tempErrorFile != null && tempErrorFile.exists()) {
        tempErrorFile.delete();
      }
    }

    return response;
  }

  public ShellResponse run(List<String> command, Map<String, String> extraEnvVars) {
    return run(command, extraEnvVars, true /*logCommandOutput*/);
  }

  public ShellResponse run(List<String> command, Map<String, String> extraEnvVars, UUID uuid) {
    return run(command, extraEnvVars, true /*logCommandOutput*/, null, uuid);
  }

  public ShellResponse run(
      List<String> command, Map<String, String> extraEnvVars, String description) {
    return run(command, extraEnvVars, true /*logCommandOutput*/, description);
  }

  private static void waitForProcessExit(Process process, File outFile, File errFile)
      throws IOException, InterruptedException {
    try (FileInputStream outputInputStream = new FileInputStream(outFile);
        InputStreamReader outputReader = new InputStreamReader(outputInputStream);
        FileInputStream errInputStream = new FileInputStream(errFile);
        InputStreamReader errReader = new InputStreamReader(errInputStream);
        BufferedReader outputStream = new BufferedReader(outputReader);
        BufferedReader errorStream = new BufferedReader(errReader)) {
      while (!process.waitFor(1, TimeUnit.SECONDS)) {
        tailStream(outputStream);
        tailStream(errorStream);
      }
      // check for any remaining lines
      tailStream(outputStream);
      tailStream(errorStream);
    }
  }

  private static void tailStream(BufferedReader br) throws IOException {

    String line = null;
    // Note: technically, this readLine can pick up incomplete lines as we race
    // with the process output being appended to this file but for the purposes
    // of logging, it is ok to log partial lines.
    while ((line = br.readLine()) != null) {
      if (line.contains("[app]")) {
        LOG.info(line);
      }
    }
  }

  private static String getAnsibleErrMsg(int code, String stdout, String stderr) {

    if (stderr == null || code == 0) return null;

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
          result = ((result != null) ? (result + "\n") : "") + m.group(0);
        }
      }
    }
    return result;
  }
}
