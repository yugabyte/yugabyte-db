// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

import javax.inject.Singleton;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

@Singleton
public class NetworkManager extends DevopsBase {
  public static final Logger LOG = LoggerFactory.getLogger(NetworkManager.class);

  private static final String YB_CLOUD_COMMAND_TYPE = "network";

  @Override
  protected String getCommandType() { return YB_CLOUD_COMMAND_TYPE; }

  public JsonNode bootstrap(UUID regionUUID) {
    List<String> command = new ArrayList<String>();
    command.add("bootstrap");
    return executeCommand(regionUUID, command);
  }

  public JsonNode query(UUID regionUUID) {
    List<String> command = new ArrayList<String>();
    command.add("query");
    return executeCommand(regionUUID, command);
  }

  public JsonNode cleanup(UUID regionUUID) {
    List<String> command = new ArrayList<String>();
    command.add("cleanup");
    return executeCommand(regionUUID, command);
  }

  private JsonNode executeCommand(UUID regionUUID, List<String> commandArgs) {
    ShellProcessHandler.ShellResponse response = execCommand(regionUUID, commandArgs);
    if (response.code == 0) {
      return Json.parse(response.message);
    } else {
      LOG.error(response.message);
      return ApiResponse.errorJSON("NetworkManager failed to execute");
    }
  }
}
