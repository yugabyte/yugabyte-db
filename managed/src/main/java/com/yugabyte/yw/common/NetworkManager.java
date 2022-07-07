// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.JsonNode;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.UUID;
import javax.inject.Singleton;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Singleton
public class NetworkManager extends DevopsBase {
  public static final Logger LOG = LoggerFactory.getLogger(NetworkManager.class);

  private static final String YB_CLOUD_COMMAND_TYPE = "network";

  @Override
  protected String getCommandType() {
    return YB_CLOUD_COMMAND_TYPE;
  }

  public JsonNode bootstrap(UUID regionUUID, UUID providerUUID, String customPayload) {
    List<String> commandArgs = new ArrayList<>();
    commandArgs.add("--custom_payload");
    commandArgs.add(customPayload);

    if (regionUUID != null) {
      return execAndParseCommandRegion(regionUUID, "bootstrap", commandArgs);
    } else {
      return execAndParseCommandCloud(providerUUID, "bootstrap", commandArgs);
    }
  }

  public JsonNode query(UUID regionUUID, String customPayload) {
    List<String> commandArgs = new ArrayList<>();
    commandArgs.add("--custom_payload");
    commandArgs.add(customPayload);
    return execAndParseCommandRegion(regionUUID, "query", commandArgs);
  }

  public JsonNode cleanupOrFail(UUID regionUUID) {
    JsonNode response = execAndParseCommandRegion(regionUUID, "cleanup", Collections.emptyList());
    if (response.has("error")) {
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, response.get("error").asText());
    }
    return response;
  }
}
