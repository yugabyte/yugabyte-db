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

@Singleton
public class ShellProcessHandler {
    public static final Logger LOG = LoggerFactory.getLogger(ShellProcessHandler.class);

    public static class ShellResponse {
        public int code;
        public String message;

        public static ShellResponse create(int code, String message) {
            ShellResponse response = new ShellResponse();
            response.code = code;
            response.message = message;
            return response;
        }
    }

    @Inject
    play.Configuration appConfig;


    public ShellResponse run(
        List<String> command,
        Map<String, String> extraEnvVars,
        boolean logCmdOutput) {
        ProcessBuilder pb = new ProcessBuilder(command);
        Map envVars = pb.environment();
        if (!extraEnvVars.isEmpty()) {
            envVars.putAll(extraEnvVars);
        }
        String devopsHome = appConfig.getString("yb.devops.home");
        if (devopsHome != null) {
            pb.directory(new File(devopsHome));
        }

        ShellResponse response = new ShellResponse();
        response.code = -1;

        File tempOutputFile = null;
        File tempErrorFile = null;
        try {
            tempOutputFile = File.createTempFile("shell_process_out", "tmp");
            tempErrorFile = File.createTempFile("shell_process_err", "tmp");
            pb.redirectOutput(tempOutputFile);
            pb.redirectError(tempErrorFile);
            Process process = pb.start();
            response.code = process.waitFor();
            String processOutput = fetchStream(new FileInputStream(tempOutputFile), logCmdOutput);
            String processError = fetchStream(new FileInputStream(tempErrorFile), logCmdOutput);
            response.message = (response.code == 0) ? processOutput : processError;
        } catch (IOException | InterruptedException e) {
            LOG.error(e.getMessage());
            response.message = e.getMessage();
        } finally {
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

    private static String fetchStream(
        InputStream inputStream,
        boolean logCmdOutput) throws IOException {
        StringBuilder sb = new StringBuilder();
        BufferedReader br = null;
        try {
            br = new BufferedReader(new InputStreamReader(inputStream));
            String line = null;
            while ((line = br.readLine()) != null) {
                sb.append(line + System.lineSeparator());
                if (logCmdOutput) {
                    LOG.info(line);
                }
            }
        } finally {
            br.close();
            inputStream.close();
        }
        return sb.toString().trim();
    }
}
