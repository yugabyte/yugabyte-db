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

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import java.util.*;
import lombok.Builder;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Slf4j
public abstract class DevopsBase {
  public static final String YBCLOUD_SCRIPT = "bin/ybcloud.sh";
  public static final String PY_WRAPPER = "bin/py_wrapper";

  // Command that we would need to execute eg: instance, network, access.
  protected abstract String getCommandType();

  @Inject ShellProcessHandler shellProcessHandler;

  @Inject RuntimeConfGetter confGetter;

  @Inject NodeAgentClient nodeAgentClient;

  @Inject FileHelperService fileHelperService;

  protected NodeAgentClient getNodeAgentClient() {
    return nodeAgentClient;
  }

  protected JsonNode execAndParseShellResponse(DevopsCommand devopsCommand) {
    ShellResponse response = execCommand(devopsCommand);
    if (response.code == 0) {
      return Json.parse(response.message);
    } else {
      String errorMsg =
          String.format(
              "YBCloud command %s (%s) failed to execute. %s",
              getCommandType(), devopsCommand.command, response.message);
      log.error(errorMsg);
      return ApiResponse.errorJSON(errorMsg);
    }
  }

  protected ShellResponse execCommand(DevopsCommand devopsCommand) {
    List<String> commandList = new ArrayList<>();
    commandList.add(YBCLOUD_SCRIPT);
    Map<String, String> extraVars = new HashMap<>();
    if (devopsCommand.envVars != null) {
      extraVars.putAll(devopsCommand.envVars);
    }
    Region region = null;
    if (devopsCommand.regionUUID != null) {
      region = Region.get(devopsCommand.regionUUID);
    }

    List<String> commandArgs = devopsCommand.commandArgs;

    if (confGetter.getGlobalConf(GlobalConfKeys.ssh2Enabled)) {
      commandArgs.add("--ssh2_enabled");
    }

    Provider provider = null;
    if (region != null) {
      commandList.add(region.getProviderCloudCode().toString());
      commandList.add("--region");
      commandList.add(region.getCode());
      try {
        Map<String, String> envConfig = CloudInfoInterface.fetchEnvVars(region.getProvider());
        extraVars.putAll(envConfig);
      } catch (Exception e) {
        throw new RuntimeException("Failed to retrieve env variables for the provider!", e);
      }
    } else if (devopsCommand.providerUUID != null) {
      provider = Provider.get(devopsCommand.providerUUID);
      commandList.add(provider.getCode());
      try {
        Map<String, String> envConfig = CloudInfoInterface.fetchEnvVars(provider);
        extraVars.putAll(envConfig);
      } catch (Exception e) {
        throw new RuntimeException("Failed to retrieve env variables for the provider!", e);
      }
    } else if (devopsCommand.cloudType != null) {
      commandList.add(devopsCommand.cloudType.toString());
    } else {
      throw new RuntimeException(
          "Invalid args provided for execCommand: region, provider or cloudType required!");
    }

    String description = String.join(" ", commandList);
    description += (" " + getCommandType().toLowerCase() + " " + devopsCommand.command);
    if (commandArgs.size() >= 1) {
      description += (" " + commandArgs.get(commandArgs.size() - 1));
    }
    commandList.addAll(devopsCommand.cloudArgs);
    commandList.add(getCommandType().toLowerCase());
    commandList.add(devopsCommand.command);
    commandList.addAll(commandArgs);
    return shellProcessHandler.run(
        commandList,
        ShellProcessContext.builder()
            .logCmdOutput(true)
            .description(description)
            .extraEnvVars(extraVars)
            .redactedVals(devopsCommand.redactedVals)
            .sensitiveData(devopsCommand.sensitiveData)
            .timeoutSecs(confGetter.getGlobalConf(GlobalConfKeys.devopsCommandTimeout).toSeconds())
            .build());
  }

  @Builder
  public static class DevopsCommand {
    UUID regionUUID;
    UUID providerUUID;
    Common.CloudType cloudType;
    String command;
    List<String> commandArgs;
    @Builder.Default List<String> cloudArgs = Collections.emptyList();
    // Env vars for this command.
    Map<String, String> envVars;
    // Args that are in the cmd, but need to be redacted.
    Map<String, String> redactedVals;
    // Args that will be added to the cmd but will be redacted in logs.
    Map<String, String> sensitiveData;
  }
}
