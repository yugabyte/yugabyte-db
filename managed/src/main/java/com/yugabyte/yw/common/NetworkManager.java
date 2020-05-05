// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.Common;

import javax.inject.Singleton;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.UUID;

@Singleton
public class NetworkManager extends DevopsBase {
  public static final Logger LOG = LoggerFactory.getLogger(NetworkManager.class);

  private static final String YB_CLOUD_COMMAND_TYPE = "network";

  @Override
  protected String getCommandType() { return YB_CLOUD_COMMAND_TYPE; }

  public JsonNode bootstrap(UUID regionUUID, UUID providerUUID, String customPayload) {
    List<String> commandArgs = new ArrayList();
    commandArgs.add("--custom_payload");
    commandArgs.add(customPayload);

    if (regionUUID != null) {
      return execAndParseCommandRegion(regionUUID, "bootstrap", commandArgs);
    } else {
      return execAndParseCommandCloud(providerUUID, "bootstrap", commandArgs);
    }
  }

  public JsonNode query(UUID regionUUID, String customPayload) {
    List<String> commandArgs = new ArrayList();
    commandArgs.add("--custom_payload");
    commandArgs.add(customPayload);
    return execAndParseCommandRegion(regionUUID, "query", commandArgs);
  }

  public JsonNode cleanup(UUID regionUUID) {
    JsonNode response = execAndParseCommandRegion(regionUUID, "cleanup", Collections.emptyList());
    if (response.has("error")) {
      throw new RuntimeException(response.get("error").asText());
    }
    return response;
  }
}
