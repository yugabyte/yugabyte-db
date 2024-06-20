// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.RedactingService.RedactionTarget;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import lombok.Builder;
import lombok.Value;
import org.apache.commons.collections4.MapUtils;
import org.apache.commons.lang3.StringUtils;
import play.libs.Json;

@Value
@Builder(toBuilder = true)
public class ShellProcessContext {
  public static final String DEFAULT_REMOTE_USER = "yugabyte";
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

  public Duration getTimeout() {
    return timeoutSecs > 0L ? Duration.ofSeconds(timeoutSecs) : Duration.ZERO;
  }

  public String getSshUserOrDefault() {
    return StringUtils.isEmpty(sshUser) ? DEFAULT_REMOTE_USER : sshUser;
  }

  public List<String> redactCommand(List<String> command) {
    List<String> redactedCommand = new ArrayList<>(command);
    // Redacting the sensitive data in the command which is used for logging.
    if (getSensitiveData() != null) {
      getSensitiveData()
          .forEach(
              (key, value) -> {
                redactedCommand.add(key);
                command.add(key);
                command.add(value);

                try {
                  JsonNode valueJson = Json.mapper().readTree(value);
                  redactedCommand.add(
                      RedactingService.filterSecretFields(valueJson, RedactionTarget.LOGS)
                          .toString());

                } catch (JsonProcessingException e) {
                  redactedCommand.add(RedactingService.redactString(value));
                }
              });
    }
    // If there are entries with redacted values, update them.
    if (MapUtils.isNotEmpty(getRedactedVals())) {
      redactedCommand.replaceAll(
          entry -> getRedactedVals().containsKey(entry) ? getRedactedVals().get(entry) : entry);
    }
    return redactedCommand;
  }
}
