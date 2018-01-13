// Copyright (c) YugaByte, Inc.

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


    public ShellResponse run(List<String> command, Map<String, String> extraEnvVars) {
        ProcessBuilder pb = new ProcessBuilder(command);
        Map envVars = pb.environment();
        if (!extraEnvVars.isEmpty()) {
            envVars.putAll(extraEnvVars);
        }

        pb.directory(new File(appConfig.getString("yb.devops.home")));
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
            String processOutput = fetchStream(new FileInputStream(tempOutputFile));
            String processError = fetchStream(new FileInputStream(tempErrorFile));
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

    private static String fetchStream(InputStream inputStream) throws IOException {
        StringBuilder sb = new StringBuilder();
        BufferedReader br = null;
        try {
            br = new BufferedReader(new InputStreamReader(inputStream));
            String line = null;
            while ((line = br.readLine()) != null) {
                sb.append(line + System.lineSeparator());
                LOG.info(line);
            }
        } finally {
            br.close();
            inputStream.close();
        }
        return sb.toString().trim();
    }
}
