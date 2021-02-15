/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.controllers.HAAuthenticator;
import com.yugabyte.yw.controllers.ReverseInternalHAController;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;
import play.mvc.Call;
import v1.RoutesPrefix;

import java.util.Map;
import java.util.UUID;
import static scala.compat.java8.JFunction.func;

public class PlatformInstanceClient {

  private static final Logger LOG = LoggerFactory.getLogger(PlatformInstanceClient.class);

  private final ApiHelper apiHelper;

  private final String remoteAddress;

  private final Map<String, String> requestHeader;

  private final ReverseInternalHAController controller;

  public PlatformInstanceClient(ApiHelper apiHelper, String clusterKey, String remoteAddress) {
    this.apiHelper = apiHelper;
    this.remoteAddress = remoteAddress;
    this.requestHeader = ImmutableMap.of(HAAuthenticator.HA_CLUSTER_KEY_TOKEN_HEADER, clusterKey);
    this.controller = new ReverseInternalHAController(func(this::getPrefix));
  }

  private String getPrefix() {
    return String.format("%s%s", this.remoteAddress, RoutesPrefix.prefix());
  }

  // Map a Call object to a request.
  private JsonNode makeRequest(Call call, JsonNode payload) {
    JsonNode response;
    switch (call.method()) {
      case "GET":
        response = this.apiHelper.getRequest(call.url(), this.requestHeader);
        break;
      case "PUT":
        response = this.apiHelper.putRequest(call.url(), payload, this.requestHeader);
        break;
      case "POST":
        response = this.apiHelper.postRequest(call.url(), payload, this.requestHeader);
        break;
      default:
        throw new RuntimeException("Unsupported operation: " + call.method());
    }

    if (response == null || response.get("error") != null) {
      LOG.error("Error received from remote instance {}", this.remoteAddress);

      throw new RuntimeException("Error received from remote instance " + this.remoteAddress);
    }

    return response;
  }

  /**
   * calls {@link com.yugabyte.yw.controllers.InternalHAController#getHAConfigByClusterKey()}
   * on remote platform instance
   *
   * @return a HighAvailabilityConfig model representing the remote platform instance's HA config
   */
  public HighAvailabilityConfig getRemoteConfig() {
    JsonNode response = this.makeRequest(this.controller.getHAConfigByClusterKey(), null);

    return Json.fromJson(response, HighAvailabilityConfig.class);
  }

  /**
   * calls {@link com.yugabyte.yw.controllers.InternalHAController#syncInstances(long timestamp)}
   * on remote platform instance
   *
   * @param payload the JSON platform instance data
   */
  public void syncInstances(long timestamp, JsonNode payload) {
    this.makeRequest(this.controller.syncInstances(timestamp), payload);
  }

  /**
   * calls {@link com.yugabyte.yw.controllers.InternalHAController#demoteLocalLeader(long timestamp)}
   * on remote platform instance
   *
   */
  public void demoteInstance(long timestamp) {
    this.makeRequest(this.controller.demoteLocalLeader(timestamp), Json.newObject());
  }
}
