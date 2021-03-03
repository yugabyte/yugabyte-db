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

import com.google.inject.Inject;
import com.google.common.base.Joiner;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import javax.inject.Singleton;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;
import java.util.concurrent.TimeUnit;

import com.yugabyte.yw.common.ShellResponse;

@Singleton
public class ShellProcessHandler {
    public static final Logger LOG = LoggerFactory.getLogger(ShellProcessHandler.class);


    @Inject
    play.Configuration appConfig;

    public ShellResponse run(
        List<String> command,
        Map<String, String> extraEnvVars,
        boolean logCmdOutput) {
            return run(command, extraEnvVars, logCmdOutput, null /*description*/);
        }

    public ShellResponse run(
        List<String> command,
        Map<String, String> extraEnvVars,
        boolean logCmdOutput,
        String description) {
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
                fullCommand = Joiner.on(" ").withKeyValueSeparator("=").join(extraEnvVars) +
                                fullCommand;
            }
            LOG.debug("Starting proc (full cmd) - {} - logging stdout={}, stderr={}",
                fullCommand, tempOutputFile.getAbsolutePath(),
                tempErrorFile.getAbsolutePath());

            Process process = pb.start();
            waitForProcessExit(process, tempOutputFile, tempErrorFile);
            try (
              FileInputStream outputInputStream = new FileInputStream(tempOutputFile);
              InputStreamReader outputReader = new InputStreamReader(outputInputStream);
              BufferedReader outputStream = new BufferedReader(outputReader);
              FileInputStream errorInputStream = new FileInputStream(tempErrorFile);
              InputStreamReader errorReader = new InputStreamReader(errorInputStream);
              BufferedReader errorStream = new BufferedReader(errorReader)
              ) {
              String processOutput = outputStream.lines().collect(Collectors.joining("\n")).trim();
              String processError = errorStream.lines().collect(Collectors.joining("\n")).trim();
              if (logCmdOutput) {
                LOG.debug("Proc stdout for '{}' | {}", response.description, processOutput);
                LOG.debug("Proc stderr for '{}' | {}", response.description, processError);
              }
              response.code = process.exitValue();
              response.message = (response.code == 0) ? processOutput : processError;
            }
        } catch (IOException | InterruptedException e) {
            response.code = -1;
            LOG.error("Exception running command", e);
            response.message = e.getMessage();
        } finally {
            if (startMs > 0) {
                response.durationMs = System.currentTimeMillis() - startMs;
            }
            String status = (0 == response.code) ? "success" :
                            ("failure code=" + Integer.toString(response.code));
            LOG.info("Completed proc '{}' status={} [ {} ms ]",
                    response.description, status, response.durationMs);
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

    public ShellResponse run(
        List<String> command,
        Map<String, String> extraEnvVars,
        String description) {
        return run(command, extraEnvVars, true /*logCommandOutput*/, description);
    }

    private static void waitForProcessExit(
      Process process,
      File outFile,
      File errFile
    ) throws IOException, InterruptedException {
      try (
        FileInputStream outputInputStream = new FileInputStream(outFile);
        InputStreamReader outputReader = new InputStreamReader(outputInputStream);
        FileInputStream errInputStream = new FileInputStream(errFile);
        InputStreamReader errReader = new InputStreamReader(errInputStream);
        BufferedReader outputStream = new BufferedReader(outputReader);
        BufferedReader errorStream = new BufferedReader(errReader)
      ) {
        while (!process.waitFor(1, TimeUnit.SECONDS)) {
          tailStream(outputStream);
          tailStream(errorStream);
        }
        // check for any remaining lines
        tailStream(outputStream);
        tailStream(errorStream);
      }
    }

    private static void tailStream(
        BufferedReader br) throws IOException {

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
}
