/*
 * Copyright 2021 YugabyteDB, Inc. and Contributors
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
import com.yugabyte.yw.models.common.YbaApi;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import java.net.URL;
import java.util.Date;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.function.Function;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.FilenameUtils;
import play.libs.Files;
import play.libs.Files.TemporaryFile;
import play.mvc.Controller;
import play.mvc.Http;
import play.mvc.Result;
import play.mvc.With;

@Api(value = "Internal HA")
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
    HighAvailabilityConfig config =
        HighAvailabilityConfig.getByClusterKey(this.getClusterKey(request))
            .orElseThrow(
                () ->
                    new PlatformServiceException(
                        NOT_FOUND, "Could not find HA Config by cluster key"));
    return PlatformResults.withData(config);
  }

  // TODO: Change this to accept ObjectNode instead of ArrayNode in request body
  public Result syncInstances(long timestamp, Http.Request request) {
    log.debug("Received request to sync instances from {}", request.remoteAddress());
    HighAvailabilityConfig config =
        HighAvailabilityConfig.getByClusterKey(getClusterKey(request))
            .orElseThrow(
                () ->
                    new PlatformServiceException(
                        NOT_FOUND, "Could not find HA Config by cluster key"));
    String content = request.body().asBytes().utf8String();
    List<PlatformInstance> newInstances = Util.parseJsonArray(content, PlatformInstance.class);
    Set<PlatformInstance> processedInstances =
        HighAvailabilityConfig.doWithTryLock(
                config.getUuid(),
                c -> {
                  Optional<PlatformInstance> localInstance = c.getLocal();
                  if (!localInstance.isPresent()) {
                    log.warn("No local instance configured");
                    throw new PlatformServiceException(BAD_REQUEST, "No local instance configured");
                  }
                  if (localInstance.get().getIsLeader()) {
                    log.warn(
                        "Rejecting request to import instances due to this process being designated"
                            + " a leader");
                    throw new PlatformServiceException(
                        BAD_REQUEST, "Cannot import instances for a leader");
                  }
                  return replicationManager.importPlatformInstances(
                      c, newInstances, new Date(timestamp));
                })
            .orElseThrow(
                () -> new PlatformServiceException(529, "Server is busy with ongoing HA process"));
    return PlatformResults.withData(processedInstances);
  }

  public Result syncBackups(Http.Request request) throws Exception {
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
    HighAvailabilityConfig config =
        HighAvailabilityConfig.getByClusterKey(this.getClusterKey(request))
            .orElseThrow(
                () -> new PlatformServiceException(BAD_REQUEST, "Could not find HA Config"));
    // Use non-blocking lock-acquire for syncs to avoid deadlock.
    return HighAvailabilityConfig.doWithTryLock(
            config.getUuid(),
            c -> {
              Optional<PlatformInstance> localInstance = c.getLocal();
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
                          "Cannot sync backup from leader on higher version %s to follower on lower"
                              + " version %s",
                          ybaVersion, Util.getYbaVersion()));
                }
              }
              URL leaderUrl = Util.toURL(leader);
              // For all the other cases we will accept the backup without checking local config
              // state.
              boolean success =
                  replicationManager.saveReplicationData(
                      fileName, temporaryFile.path(), leaderUrl, Util.toURL(sender));
              if (success) {
                // TODO: (Daniel) - Need to cleanup backups in non-current leader dir too.
                replicationManager.cleanupReceivedBackups(leaderUrl);
                return YBPSuccess.withMessage("File uploaded");
              }
              throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "Failed to copy backup");
            })
        .orElseThrow(
            () -> new PlatformServiceException(529, "Server is busy with ongoing HA process"));
  }

  /** This is invoked by the remote peer to demote this local leader. */
  @ApiOperation(
      notes = "Available since YBA version 2.20.0.",
      value = "Demote local leader",
      response = PlatformInstance.class,
      nickname = "demoteLocalLeader")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PUBLIC, sinceYBAVersion = "2.20.0")
  public Result demoteLocalLeader(long timestamp, boolean promote, Http.Request request) {
    log.debug("Received request to demote local instance from {}", request.remoteAddress());
    String clusterKey = getClusterKey(request);
    HighAvailabilityConfig config =
        HighAvailabilityConfig.getByClusterKey(clusterKey)
            .orElseThrow(() -> new PlatformServiceException(NOT_FOUND, "Invalid config UUID"));
    Function<HighAvailabilityConfig, PlatformInstance> func =
        c -> {
          DemoteInstanceFormData formData =
              formFactory.getFormDataOrBadRequest(
                  request.body().asJson(), DemoteInstanceFormData.class);
          // Demote the local instance.
          PlatformInstance i =
              replicationManager.demoteLocalInstance(
                  c, formData.leader_address, new Date(timestamp));
          // Only restart YBA when demote comes from promote call, not from periodic sync
          if (promote && runtimeConfGetter.getGlobalConf(GlobalConfKeys.haShutdownLevel) > 1) {
            Util.shutdownYbaProcess(5);
          }
          return i;
        };
    // Use non-blocking lock-acquire for background syncs to avoid deadlock.
    PlatformInstance localInstance =
        promote
            ? HighAvailabilityConfig.doWithLock(config.getUuid(), func)
            : HighAvailabilityConfig.doWithTryLock(config.getUuid(), func).orElse(null);
    if (localInstance == null) {
      log.warn("Local leader was not demoted possibly due to an ongoining promotion");
    }
    return PlatformResults.withData(localInstance);
  }
}
