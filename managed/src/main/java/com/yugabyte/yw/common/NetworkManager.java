// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.JsonNode;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
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

    Map<String, String> sensitiveData = new HashMap<String, String>();
    sensitiveData.put("--custom_payload", customPayload);

    if (regionUUID != null) {
      return execAndParseShellResponse(
          DevopsCommand.builder()
              .regionUUID(regionUUID)
              .command("bootstrap")
              .commandArgs(commandArgs)
              .sensitiveData(sensitiveData)
              .build());
    } else {
      return execAndParseShellResponse(
          DevopsCommand.builder()
              .providerUUID(providerUUID)
              .command("bootstrap")
              .commandArgs(commandArgs)
              .sensitiveData(sensitiveData)
              .build());
    }
  }

  public JsonNode query(UUID regionUUID, String customPayload) {
    List<String> commandArgs = new ArrayList<>();
    commandArgs.add("--custom_payload");
    commandArgs.add(customPayload);
    return execAndParseShellResponse(
        DevopsCommand.builder()
            .regionUUID(regionUUID)
            .command("query")
            .commandArgs(commandArgs)
            .build());
  }

  public JsonNode cleanupOrFail(UUID regionUUID) {
    JsonNode response =
        execAndParseShellResponse(
            DevopsCommand.builder()
                .regionUUID(regionUUID)
                .command("cleanup")
                .commandArgs(Collections.emptyList())
                .build());
    if (response.has("error")) {
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, response.get("error").asText());
    }
    return response;
  }
}
