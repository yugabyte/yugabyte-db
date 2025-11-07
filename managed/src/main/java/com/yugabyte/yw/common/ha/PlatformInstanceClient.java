/*
 * Copyright 2022 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.ha;

import static com.yugabyte.yw.common.Util.getYbaVersion;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.ConfigHelper.ConfigType;
import com.yugabyte.yw.controllers.HAAuthenticator;
import com.yugabyte.yw.controllers.ReverseInternalHAController;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import io.prometheus.client.CollectorRegistry;
import io.prometheus.client.Gauge;
import java.io.File;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;
import lombok.Getter;
import lombok.extern.slf4j.Slf4j;
import org.apache.pekko.stream.javadsl.FileIO;
import org.apache.pekko.stream.javadsl.Source;
import org.apache.pekko.util.ByteString;
import play.libs.Json;
import play.mvc.Call;
import play.mvc.Http;
import v1.RoutesPrefix;

@Slf4j
public class PlatformInstanceClient implements AutoCloseable {

  public static final String YB_HA_WS_KEY = "yb.ha.ws";
  private static final String HA_INSTANCE_VERSION_MISMATCH_NAME = "yba_ha_inst_version_mismatch";
  private static final String HA_INSTANCE_ADDR_LABEL = "instance_addr";
  private static final Gauge HA_YBA_VERSION_MISMATCH_GAUGE;

  @Getter(onMethod_ = {@VisibleForTesting})
  private final ApiHelper apiHelper;

  private final String remoteAddress;

  private final Map<String, String> requestHeader;

  private final ReverseInternalHAController controller;

  private final ConfigHelper configHelper;

  static {
    HA_YBA_VERSION_MISMATCH_GAUGE =
        Gauge.build(HA_INSTANCE_VERSION_MISMATCH_NAME, "Has Instance version mismatched")
            .labelNames(HA_INSTANCE_ADDR_LABEL)
            .register(CollectorRegistry.defaultRegistry);
  }

  public PlatformInstanceClient(
      ApiHelper apiHelper, String clusterKey, String remoteAddress, ConfigHelper configHelper) {
    this.apiHelper = apiHelper;
    this.remoteAddress = remoteAddress;
    this.requestHeader = ImmutableMap.of(HAAuthenticator.HA_CLUSTER_KEY_TOKEN_HEADER, clusterKey);
    this.controller = new ReverseInternalHAController(this::getPrefix);
    this.configHelper = configHelper;
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
      log.error("Error received from remote instance {}: {}", this.remoteAddress, response);
      throw new RuntimeException("Error received from remote instance " + this.remoteAddress);
    }

    return response;
  }

  /**
   * calls {@link com.yugabyte.yw.controllers.InternalHAController#getHAConfigByClusterKey()} on
   * remote platform instance
   *
   * @return a HighAvailabilityConfig model representing the remote platform instance's HA config
   */
  public HighAvailabilityConfig getRemoteConfig() {
    JsonNode response = this.makeRequest(this.controller.getHAConfigByClusterKey(), null);
    return Json.fromJson(response, HighAvailabilityConfig.class);
  }

  /**
   * calls {@link com.yugabyte.yw.controllers.InternalHAController#syncInstances(long timestamp)} on
   * remote platform instance
   *
   * @param payload the JSON platform instance data
   */
  public void syncInstances(long timestamp, JsonNode payload) {
    this.makeRequest(this.controller.syncInstances(timestamp), payload);
  }

  /**
   * calls {@link com.yugabyte.yw.controllers.InternalHAController#demoteLocalLeader(long timestamp,
   * boolean promote)} on remote platform instance. Returns true if successful.
   */
  public boolean demoteInstance(String localAddr, long timestamp, boolean promote) {
    boolean success = false;
    ObjectNode formData = Json.newObject().put("leader_address", localAddr);
    Call call = controller.demoteLocalLeader(timestamp, promote);
    log.info("Making a remote call to {} to demote the instance", call.url());
    final JsonNode response = this.makeRequest(call, formData);
    success = response != null && response.isObject();
    if (success) {
      log.info("Successfully demoted remote instance at {}", call.url());
      maybeGenerateVersionMismatchEvent(response.get("ybaVersion"));
    } else {
      log.warn("Failed to demote the remote instance at {}", call.url());
    }
    return success;
  }

  public boolean syncBackups(String leaderAddr, String senderAddr, File backupFile) {
    JsonNode response =
        this.apiHelper.multipartRequest(
            this.controller.syncBackups().url(),
            this.requestHeader,
            buildPartsList(
                backupFile,
                ImmutableMap.of(
                    "leader", leaderAddr, "sender", senderAddr, "ybaversion", getYbaVersion())));
    // Manually close WS client as we are calling multipartRequest on apiHelper and not makeRequest
    if (response == null || response.get("error") != null) {
      log.error("Error received from remote instance {}. Got {}", this.remoteAddress, response);
      return false;
    } else {
      return true;
    }
  }

  public boolean testConnection() {
    try {
      JsonNode response = this.makeRequest(this.controller.getHAConfigByClusterKey(), null);
    } catch (Exception e) {
      return false;
    }
    return true;
  }

  private void maybeGenerateVersionMismatchEvent(JsonNode remoteVersion) {
    if (remoteVersion == null || remoteVersion.toString().isEmpty()) {
      return;
    }
    String localVersion =
        configHelper.getConfig(ConfigType.YugawareMetadata).getOrDefault("version", "").toString();
    // Remove single or double quotes from remoteVersion
    String remoteVersionStripped = remoteVersion.toString().replaceAll("^['\"]|['\"]$", "");

    if (!localVersion.equals(remoteVersionStripped)) {
      HA_YBA_VERSION_MISMATCH_GAUGE.labels(remoteAddress).set(1);
    } else {
      HA_YBA_VERSION_MISMATCH_GAUGE.labels(remoteAddress).set(0);
    }
  }

  public static List<Http.MultipartFormData.Part<Source<ByteString, ?>>> buildPartsList(
      File file, ImmutableMap<String, String> dataParts) {
    Http.MultipartFormData.FilePart<Source<ByteString, ?>> filePart =
        new Http.MultipartFormData.FilePart<>(
            "backup", file.getName(), "application/octet-stream", FileIO.fromFile(file, 1024));

    List<Http.MultipartFormData.Part<Source<ByteString, ?>>> ret =
        dataParts.entrySet().stream()
            .map(kv -> new Http.MultipartFormData.DataPart(kv.getKey(), kv.getValue()))
            .collect(Collectors.toList());

    ret.add(filePart);
    return ret;
  }

  public void clearMetrics() {
    HA_YBA_VERSION_MISMATCH_GAUGE.clear();
  }

  @Override
  public void close() {
    this.apiHelper.closeClient();
  }
}
