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
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.ValidatingFormFactory;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
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
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.FilenameUtils;
import play.libs.Files;
import play.libs.Files.TemporaryFile;
import play.mvc.Controller;
import play.mvc.Http;
import play.mvc.Result;
import play.mvc.With;

@With(HAAuthenticator.class)
@Slf4j
public class InternalHAController extends Controller {

  private final RuntimeConfGetter runtimeConfGetter;
  private final PlatformReplicationManager replicationManager;
  private final ValidatingFormFactory formFactory;

  @Inject
  InternalHAController(
      RuntimeConfGetter runtimeConfGetter,
      PlatformReplicationManager replicationManager,
      ValidatingFormFactory formFactory,
      ConfigHelper configHelper) {
    this.runtimeConfGetter = runtimeConfGetter;
    this.replicationManager = replicationManager;
    this.formFactory = formFactory;
  }

  private String getClusterKey(Http.Request request) {
    return request.header(HAAuthenticator.HA_CLUSTER_KEY_TOKEN_HEADER).get();
  }

  public Result getHAConfigByClusterKey(Http.Request request) {
    try {
      Optional<HighAvailabilityConfig> config =
          HighAvailabilityConfig.getByClusterKey(this.getClusterKey(request));

      if (!config.isPresent()) {
        throw new PlatformServiceException(NOT_FOUND, "Could not find HA Config by cluster key");
      }

      return PlatformResults.withData(config.get());
    } catch (Exception e) {
      log.error("Error retrieving HA config");
      if (e instanceof PlatformServiceException) {
        throw (PlatformServiceException) e;
      }
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "Error retrieving HA config");
    }
  }

  // TODO: Change this to accept ObjectNode instead of ArrayNode in request body
  public synchronized Result syncInstances(long timestamp, Http.Request request) {
    log.debug("Received request to sync instances from {}", request.remoteAddress());
    Optional<HighAvailabilityConfig> config =
        HighAvailabilityConfig.getByClusterKey(this.getClusterKey(request));
    if (!config.isPresent()) {
      throw new PlatformServiceException(NOT_FOUND, "Invalid config UUID");
    }

    Optional<PlatformInstance> localInstance = config.get().getLocal();

    if (!localInstance.isPresent()) {
      log.warn("No local instance configured");
      throw new PlatformServiceException(BAD_REQUEST, "No local instance configured");
    }
    if (localInstance.get().getIsLeader()) {
      log.warn(
          "Rejecting request to import instances due to this process being designated a leader");
      throw new PlatformServiceException(BAD_REQUEST, "Cannot import instances for a leader");
    }
    String content = request.body().asBytes().utf8String();
    List<PlatformInstance> newInstances = Util.parseJsonArray(content, PlatformInstance.class);
    Set<PlatformInstance> processedInstances =
        replicationManager.importPlatformInstances(config.get(), newInstances, new Date(timestamp));

    return PlatformResults.withData(processedInstances);
  }

  public synchronized Result syncBackups(Http.Request request) throws Exception {
    Http.MultipartFormData<Files.TemporaryFile> body = request.body().asMultipartFormData();

    Map<String, String[]> reqParams = body.asFormUrlEncoded();
    String[] leaders = reqParams.getOrDefault("leader", new String[0]);
    String[] senders = reqParams.getOrDefault("sender", new String[0]);
    String[] ybaVersions = reqParams.getOrDefault("ybaversion", new String[0]);
    if (!((reqParams.size() == 3
            && leaders.length == 1
            && senders.length == 1
            && ybaVersions.length == 1)
        || (reqParams.size() == 2 && leaders.length == 1 && senders.length == 1))) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Expected exactly 2 (leader, sender) or 3 (leader, sender, ybaversion) arguments in "
              + "'application/x-www-form-urlencoded' data part. Received: "
              + reqParams);
    }
    Http.MultipartFormData.FilePart<Files.TemporaryFile> filePart = body.getFile("backup");
    if (filePart == null) {
      throw new PlatformServiceException(BAD_REQUEST, "backup file not found in request");
    }
    String fileName = FilenameUtils.getName(filePart.getFilename());
    TemporaryFile temporaryFile = filePart.getRef();
    String leader = leaders[0];
    String sender = senders[0];

    if (!leader.equals(sender)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Sender: " + sender + " does not match leader: " + leader);
    }

    Optional<HighAvailabilityConfig> config =
        HighAvailabilityConfig.getByClusterKey(this.getClusterKey(request));
    if (!config.isPresent()) {
      throw new PlatformServiceException(BAD_REQUEST, "Could not find HA Config");
    }

    Optional<PlatformInstance> localInstance = config.get().getLocal();
    if (localInstance.isPresent() && leader.equals(localInstance.get().getAddress())) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Backup originated on the node itself. Leader: " + leader);
    }

    if (ybaVersions.length == 1) {
      String ybaVersion = ybaVersions[0];
      if (Util.compareYbVersions(ybaVersion, Util.getYbaVersion()) > 0) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "Cannot sync backup from leader on higher version %s to follower on lower version"
                    + " %s",
                ybaVersion, Util.getYbaVersion()));
      }
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
    }
    throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "Failed to copy backup");
  }

  /** This is invoked by the remote peer to demote this local leader. */
  public synchronized Result demoteLocalLeader(
      long timestamp, boolean promote, Http.Request request) {
    try {
      log.debug("Received request to demote local instance from {}", request.remoteAddress());
      DemoteInstanceFormData formData =
          formFactory.getFormDataOrBadRequest(request, DemoteInstanceFormData.class).get();
      Optional<HighAvailabilityConfig> config =
          HighAvailabilityConfig.getByClusterKey(getClusterKey(request));
      if (!config.isPresent()) {
        log.warn("No HA configuration configured, skipping request");
        throw new PlatformServiceException(NOT_FOUND, "Invalid config UUID");
      }
      Optional<PlatformInstance> localInstance = config.get().getLocal();
      if (!localInstance.isPresent()) {
        log.warn("No local instance configured");
        throw new PlatformServiceException(BAD_REQUEST, "No local instance configured");
      }
      // Demote the local instance.
      replicationManager.demoteLocalInstance(
          config.get(), localInstance.get(), formData.leader_address, new Date(timestamp));
      // Only restart YBA when demote comes from promote call, not from periodic sync
      if (promote && runtimeConfGetter.getGlobalConf(GlobalConfKeys.haShutdownLevel) > 1) {
        Util.shutdownYbaProcess(5);
      }
      return PlatformResults.withData(localInstance);
    } catch (Exception e) {
      log.error("Error demoting platform instance", e);
      if (e instanceof PlatformServiceException) {
        throw (PlatformServiceException) e;
      }
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "Error demoting platform instance");
    }
  }
}
