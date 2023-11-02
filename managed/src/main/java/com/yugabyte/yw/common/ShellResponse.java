// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common;

import java.util.List;
import java.util.concurrent.CancellationException;
import lombok.Data;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Data
@Slf4j
public class ShellResponse {
  // Some known error codes for shell process.
  private static final String RUN_COMMAND_OUTPUT_PREFIX = "Command output:";
  public static final int ERROR_CODE_SUCCESS = 0;
  public static final String ERROR_OUTPUT_PREFIX = "ERROR:";
  public static final int ERROR_CODE_GENERIC_ERROR = -1;
  public static final int ERROR_CODE_EXECUTION_CANCELLED = -2;

  public int code = ERROR_CODE_SUCCESS;
  public String message = null;
  public long durationMs = 0;
  public String description = null;

  public static ShellResponse create(int code, String message) {
    ShellResponse sr = new ShellResponse();
    sr.code = code;
    sr.message = message;
    return sr;
  }

  public void setDescription(List<String> command) {
    description = StringUtils.abbreviateMiddle(String.join(" ", command), " ... ", 140);
  }

  public boolean isSuccess() {
    return code == ERROR_CODE_SUCCESS;
  }

  // Call this method to process or validate the exit code if required.
  public ShellResponse processErrors() {
    return processErrors(null);
  }

  // Call this method to process or validate the exit code with custom error message if required.
  public ShellResponse processErrors(String errorMessage) {
    if (code != ERROR_CODE_SUCCESS) {
      String formatted = StringUtils.isBlank(errorMessage) ? "Error occurred" : errorMessage;
      try {
        switch (code) {
          case ERROR_CODE_EXECUTION_CANCELLED:
            formatted = String.format("%s. Command is cancelled.", formatted);
            throw new CancellationException(formatted);
          default:
            formatted = String.format("%s. Output: %s", formatted, message);
            throw new RuntimeException(formatted);
        }
      } finally {
        log.error("{}, {}", formatted, toString());
      }
    }
    return this;
  }

  public static String cleanedUpErrorMessage(String message) {
    return message.substring(Math.max(message.indexOf(ERROR_OUTPUT_PREFIX), 0));
  }

  public String extractRunCommandOutput() {
    int prefixIndex = message.indexOf(RUN_COMMAND_OUTPUT_PREFIX);
    if (prefixIndex < 0) {
      throw new RuntimeException("Invalid command output");
    }
    return message.substring(prefixIndex + RUN_COMMAND_OUTPUT_PREFIX.length()).trim();
  }
}
