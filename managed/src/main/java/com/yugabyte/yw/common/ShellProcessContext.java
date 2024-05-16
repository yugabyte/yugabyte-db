// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common;

import java.util.Map;
import java.util.UUID;
import lombok.Builder;
import lombok.Value;

@Value
@Builder(toBuilder = true)
public class ShellProcessContext {
  public static final ShellProcessContext DEFAULT = ShellProcessContext.builder().build();
  // specify the SSH user to connect with.
  String sshUser;
  // Whether to log stdout&stderr to application.log or not.
  boolean logCmdOutput;
  // Executed command is logged with trace level, in case it's set to true. Otherwise info.
  boolean traceLogging;
  // Human-readable description for logging.
  String description;
  // Used to track this execution, can be null.
  UUID uuid;
  // Abort the command forcibly if it takes longer than this.
  long timeoutSecs;
  // Env vars for this command.
  Map<String, String> extraEnvVars;
  // Args that are in the cmd, but need to be redacted.
  Map<String, String> redactedVals;
  // Args that will be added to the cmd but will be redacted in logs.
  Map<String, String> sensitiveData;
}
