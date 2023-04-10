/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.ConfigHelper.ConfigType;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.ValidatingFormFactory;
import com.yugabyte.yw.common.ha.PlatformReplicationManager;
import com.yugabyte.yw.forms.DemoteInstanceFormData;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.PlatformInstance;
import java.net.URL;
import java.util.Date;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import org.apache.commons.io.FilenameUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Files;
import play.libs.Files.TemporaryFile;
import play.mvc.Controller;
import play.mvc.Http;
import play.mvc.Result;
import play.mvc.With;

@With(HAAuthenticator.class)
public class InternalHAController extends Controller {

  public static final Logger LOG = LoggerFactory.getLogger(InternalHAController.class);

  private final PlatformReplicationManager replicationManager;
  private final ValidatingFormFactory formFactory;
  private final ConfigHelper configHelper;

  @Inject
  InternalHAController(
      PlatformReplicationManager replicationManager,
      ValidatingFormFactory formFactory,
      ConfigHelper configHelper) {
    this.replicationManager = replicationManager;
    this.formFactory = formFactory;
    this.configHelper = configHelper;
  }

  private String getClusterKey(Http.Request request) {
    return request.header(HAAuthenticator.HA_CLUSTER_KEY_TOKEN_HEADER).get();
  }

  public Result getHAConfigByClusterKey(Http.Request request) {
    try {
      Optional<HighAvailabilityConfig> config =
          HighAvailabilityConfig.getByClusterKey(this.getClusterKey(request));

      if (!config.isPresent()) {
        return ApiResponse.error(NOT_FOUND, "Could not find HA Config by cluster key");
      }

      return PlatformResults.withData(config.get());
    } catch (Exception e) {
      LOG.error("Error retrieving HA config");

      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Error retrieving HA config");
    }
  }

  // TODO: Change this to accept ObjectNode instead of ArrayNode in request body
  public Result syncInstances(long timestamp, Http.Request request) {
    Optional<HighAvailabilityConfig> config =
        HighAvailabilityConfig.getByClusterKey(this.getClusterKey(request));
    if (!config.isPresent()) {
      return ApiResponse.error(NOT_FOUND, "Invalid config UUID");
    }

    Optional<PlatformInstance> localInstance = config.get().getLocal();

    if (!localInstance.isPresent()) {
      LOG.warn("No local instance configured");

      return ApiResponse.error(BAD_REQUEST, "No local instance configured");
    }

    if (localInstance.get().getIsLeader()) {
      LOG.warn(
          "Rejecting request to import instances due to this process being designated a leader");

      return ApiResponse.error(BAD_REQUEST, "Cannot import instances for a leader");
    }

    Date requestLastFailover = new Date(timestamp);
    Date localLastFailover = config.get().getLastFailover();

    // Reject the request if coming from a platform instance that was failed over to earlier.
    if (localLastFailover != null && localLastFailover.after(requestLastFailover)) {
      LOG.warn("Rejecting request to import instances due to request lastFailover being stale");

      return ApiResponse.error(BAD_REQUEST, "Cannot import instances from stale leader");
    }

    String content = request.body().asBytes().utf8String();
    List<PlatformInstance> newInstances = Util.parseJsonArray(content, PlatformInstance.class);
    Set<PlatformInstance> processedInstances =
        replicationManager.importPlatformInstances(config.get(), newInstances);

    return PlatformResults.withData(processedInstances);
  }

  public Result syncBackups(Http.Request request) throws Exception {
    Http.MultipartFormData<Files.TemporaryFile> body = request.body().asMultipartFormData();

    Map<String, String[]> reqParams = body.asFormUrlEncoded();
    String[] leaders = reqParams.getOrDefault("leader", new String[0]);
    String[] senders = reqParams.getOrDefault("sender", new String[0]);
    if (reqParams.size() != 2 || leaders.length != 1 || senders.length != 1) {
      return ApiResponse.error(
          BAD_REQUEST,
          "Expected exactly 2 (leader and sender) argument in 'application/x-www-form-urlencoded' "
              + "data part. Received: "
              + reqParams);
    }
    Http.MultipartFormData.FilePart<Files.TemporaryFile> filePart = body.getFile("backup");
    if (filePart == null) {
      return ApiResponse.error(BAD_REQUEST, "backup file not found in request");
    }
    String fileName = FilenameUtils.getName(filePart.getFilename());
    TemporaryFile temporaryFile = filePart.getRef();
    String leader = leaders[0];
    String sender = senders[0];

    if (!leader.equals(sender)) {
      return ApiResponse.error(
          BAD_REQUEST, "Sender: " + sender + " does not match leader: " + leader);
    }

    Optional<HighAvailabilityConfig> config =
        HighAvailabilityConfig.getByClusterKey(this.getClusterKey(request));
    if (!config.isPresent()) {
      return ApiResponse.error(BAD_REQUEST, "Could not find HA Config");
    }

    Optional<PlatformInstance> localInstance = config.get().getLocal();
    if (localInstance.isPresent() && leader.equals(localInstance.get().getAddress())) {
      return ApiResponse.error(
          BAD_REQUEST, "Backup originated on the node itself. Leader: " + leader);
    }

    URL leaderUrl = new URL(leader);

    // For all the other cases we will accept the backup without checking local config state.
    boolean success =
        replicationManager.saveReplicationData(
            fileName, temporaryFile.path(), leaderUrl, new URL(sender));
    if (success) {
      // TODO: (Daniel) - Need to cleanup backups in non-current leader dir too.
      replicationManager.cleanupReceivedBackups(leaderUrl);
      return YBPSuccess.withMessage("File uploaded");
    } else {
      return ApiResponse.error(INTERNAL_SERVER_ERROR, "failed to copy backup");
    }
  }

  public Result demoteLocalLeader(long timestamp, Http.Request request) {
    try {
      Optional<HighAvailabilityConfig> config =
          HighAvailabilityConfig.getByClusterKey(this.getClusterKey(request));
      if (!config.isPresent()) {
        LOG.warn("No HA configuration configured, skipping request");

        return ApiResponse.error(NOT_FOUND, "Invalid config UUID");
      }

      DemoteInstanceFormData formData =
          formFactory.getFormDataOrBadRequest(request, DemoteInstanceFormData.class).get();

      Optional<PlatformInstance> localInstance = config.get().getLocal();

      if (!localInstance.isPresent()) {
        LOG.warn("No local instance configured");

        return ApiResponse.error(BAD_REQUEST, "No local instance configured");
      }

      Date requestLastFailover = new Date(timestamp);
      Date localLastFailover = config.get().getLastFailover();

      // Reject the request if coming from a platform instance that was failed over to earlier.
      if (localLastFailover != null && localLastFailover.after(requestLastFailover)) {
        LOG.warn("Rejecting demote request due to request lastFailover being stale");

        return ApiResponse.error(BAD_REQUEST, "Rejecting demote request from stale leader");
      } else if (localLastFailover == null || localLastFailover.before(requestLastFailover)) {
        // Otherwise, update the last failover timestamp and proceed with demotion request.
        config.get().updateLastFailover(requestLastFailover);
      }

      // Demote the local instance.
      replicationManager.demoteLocalInstance(localInstance.get(), formData.leader_address);

      String version =
          configHelper
              .getConfig(ConfigType.YugawareMetadata)
              .getOrDefault("version", "UNKNOWN")
              .toString();

      localInstance.get().setYbaVersion(version);

      return PlatformResults.withData(localInstance);
    } catch (Exception e) {
      LOG.error("Error demoting platform instance", e);

      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Error demoting platform instance");
    }
  }
}
