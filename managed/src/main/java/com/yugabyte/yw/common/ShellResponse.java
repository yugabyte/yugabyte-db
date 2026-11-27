// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.common;

import com.cronutils.utils.VisibleForTesting;
import java.util.concurrent.CancellationException;
import java.util.function.Supplier;
import lombok.Data;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Data
@Slf4j
public class ShellResponse {
  // Some known error codes for shell process.
  @VisibleForTesting public static final String RUN_COMMAND_OUTPUT_PREFIX = "Command output:";
  private static final String ERROR_OUTPUT_PREFIX = "ERROR:";
  @VisibleForTesting static final String OUTPUT_NOT_LOGGED = "<not logged>";
  public static final int ERROR_CODE_SUCCESS = 0;
  public static final int ERROR_CODE_GENERIC_ERROR = -1;
  public static final int ERROR_CODE_EXECUTION_CANCELLED = -2;
  public static final int ERROR_CODE_RECOVERABLE_ERROR = 3;

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

  public boolean isSuccess() {
    return code == ERROR_CODE_SUCCESS;
  }

  // Call this method to process or validate the exit code if required.
  public ShellResponse processErrors() {
    return processErrors(() -> null);
  }

  // Call this method to process or validate the exit code with custom error message if required.
  public ShellResponse processErrors(String errorMessage) {
    return processErrors(() -> errorMessage);
  }

  // Same as processErrors(errorMessage), but with logOutput false the logged error leaves out the
  // output, for commands run with logCmdOutput false. The thrown exception still has the output.
  public ShellResponse processErrors(String errorMessage, boolean logOutput) {
    return processErrors(() -> errorMessage, logOutput);
  }

  // Call this method to process or validate the exit code with custom error message supplier if
  // required. It performs delayed evaluation of the error message only when the code is not success
  // and avoids unnecessary computation of the error message when the code is success.
  public ShellResponse processErrors(Supplier<String> errorMessageSupplier) {
    return processErrors(errorMessageSupplier, true);
  }

  private ShellResponse processErrors(Supplier<String> errorMessageSupplier, boolean logOutput) {
    if (code != ERROR_CODE_SUCCESS) {
      String loggedOutput = logOutput ? message : OUTPUT_NOT_LOGGED;
      String errorMessage = errorMessageSupplier.get();
      String formatted = StringUtils.isBlank(errorMessage) ? "Error occurred" : errorMessage;
      String logged = formatted;
      try {
        switch (code) {
          case ERROR_CODE_EXECUTION_CANCELLED:
            formatted = String.format("%s. Command is cancelled.", formatted);
            logged = formatted;
            throw new CancellationException(formatted);
          case ERROR_CODE_RECOVERABLE_ERROR:
            logged = withOutput(formatted, loggedOutput);
            formatted = withOutput(formatted, message);
            throw new RecoverableException(formatted);
          default:
            logged = withOutput(formatted, loggedOutput);
            formatted = withOutput(formatted, message);
            throw new RuntimeException(formatted);
        }
      } finally {
        log.error(logged);
      }
    }
    return this;
  }

  private String withOutput(String errorMessage, String output) {
    return String.format("%s. Code: %d. Output: %s", errorMessage, code, output);
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
