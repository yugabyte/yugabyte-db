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
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Slf4j
public abstract class DevopsBase {
  public static final String YBCLOUD_SCRIPT = "bin/ybcloud.sh";
  public static final String PY_WRAPPER = "bin/py_wrapper";

  // Command that we would need to execute eg: instance, network, access.
  protected abstract String getCommandType();

  @Inject ShellProcessHandler shellProcessHandler;

  @Inject RuntimeConfigFactory runtimeConfigFactory;

  @Inject play.Configuration appConfig;

  protected JsonNode parseShellResponse(ShellResponse response, String command) {
    if (response.code == 0) {
      return Json.parse(response.message);
    } else {
      String errorMsg =
          String.format(
              "YBCloud command %s (%s) failed to execute. %s",
              getCommandType(), command, response.message);
      log.error(errorMsg);
      return ApiResponse.errorJSON(errorMsg);
    }
  }

  protected JsonNode execAndParseCommandCloud(
      UUID providerUUID, String command, List<String> commandArgs) {
    ShellResponse response =
        execCommand(null, providerUUID, null, command, commandArgs, Collections.emptyList());
    return parseShellResponse(response, command);
  }

  protected JsonNode execAndParseCommandRegion(
      UUID regionUUID, String command, List<String> commandArgs) {
    ShellResponse response =
        execCommand(regionUUID, null, null, command, commandArgs, Collections.emptyList());
    return parseShellResponse(response, command);
  }

  protected ShellResponse execCommand(
      UUID regionUUID,
      UUID providerUUID,
      String command,
      List<String> commandArgs,
      List<String> cloudArgs) {
    return execCommand(
        regionUUID, providerUUID, null /*cloudType*/, command, commandArgs, cloudArgs);
  }

  protected ShellResponse execCommand(
      UUID regionUUID,
      UUID providerUUID,
      Common.CloudType cloudType,
      String command,
      List<String> commandArgs,
      List<String> cloudArgs) {
    return execCommand(regionUUID, providerUUID, cloudType, command, commandArgs, cloudArgs, null);
  }

  protected ShellResponse execCommand(
      UUID regionUUID,
      UUID providerUUID,
      Common.CloudType cloudType,
      String command,
      List<String> commandArgs,
      List<String> cloudArgs,
      Map<String, String> envVars) {
    return execCommand(
        regionUUID, providerUUID, cloudType, command, commandArgs, cloudArgs, envVars, null);
  }

  protected ShellResponse execCommand(
      UUID regionUUID,
      UUID providerUUID,
      Common.CloudType cloudType,
      String command,
      List<String> commandArgs,
      List<String> cloudArgs,
      Map<String, String> envVars,
      Map<String, String> sensitiveData) {
    List<String> commandList = new ArrayList<>();
    commandList.add(YBCLOUD_SCRIPT);
    Map<String, String> extraVars = new HashMap<>();
    if (envVars != null) {
      extraVars.putAll(envVars);
    }
    Region region = null;
    if (regionUUID != null) {
      region = Region.get(regionUUID);
    }

    if (runtimeConfigFactory.globalRuntimeConf().getBoolean("yb.security.ssh2_enabled")) {
      commandArgs.add("--ssh2_enabled");
    }

    Provider provider = null;
    if (region != null) {
      commandList.add(region.provider.code);
      commandList.add("--region");
      commandList.add(region.code);
      extraVars.putAll(region.provider.getUnmaskedConfig());
    } else if (providerUUID != null) {
      provider = Provider.get(providerUUID);
      commandList.add(provider.code);
      extraVars.putAll(provider.getUnmaskedConfig());
    } else if (cloudType != null) {
      commandList.add(cloudType.toString());
    } else {
      throw new RuntimeException(
          "Invalid args provided for execCommand: region, provider or cloudType required!");
    }

    String description = String.join(" ", commandList);
    description += (" " + getCommandType().toLowerCase() + " " + command);
    if (commandArgs.size() >= 1) {
      description += (" " + commandArgs.get(commandArgs.size() - 1));
    }
    commandList.addAll(cloudArgs);
    commandList.add(getCommandType().toLowerCase());
    commandList.add(command);
    commandList.addAll(commandArgs);
    return (sensitiveData != null && !sensitiveData.isEmpty())
        ? shellProcessHandler.run(commandList, extraVars, description, sensitiveData)
        : shellProcessHandler.run(commandList, extraVars, description);
  }
}
