// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.check;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.ServerSubTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.ReleaseManager.ReleaseMetadata;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.gflags.AutoFlagUtil;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import java.io.InputStream;
import java.util.Collections;
import java.util.Set;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;

@Slf4j
public class CheckUpgrade extends ServerSubTaskBase {

  private final GFlagsValidation gFlagsValidation;
  private final ReleaseManager releaseManager;
  private final Config appConfig;
  private final AutoFlagUtil autoFlagUtil;

  @Inject
  protected CheckUpgrade(
      BaseTaskDependencies baseTaskDependencies,
      Config appConfig,
      GFlagsValidation gFlagsValidation,
      ReleaseManager releaseManager,
      AutoFlagUtil autoFlagUtil) {
    super(baseTaskDependencies);
    this.appConfig = appConfig;
    this.gFlagsValidation = gFlagsValidation;
    this.releaseManager = releaseManager;
    this.autoFlagUtil = autoFlagUtil;
  }

  public static class Params extends ServerSubTaskParams {
    public String ybSoftwareVersion;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    String oldVersion =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    String newVersion = taskParams().ybSoftwareVersion;
    if (CommonUtils.isAutoFlagSupported(oldVersion)) {
      if (!CommonUtils.isAutoFlagSupported(newVersion)) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Cannot upgrade DB to version which does not contains auto flags");
      }
    } else {
      log.info(
          "Skipping auto flags check as it was not enabled earlier on version: {}", oldVersion);
      return;
    }

    try {
      // Extract auto flag file from db package if not exists earlier.
      String releasePath = appConfig.getString(Util.YB_RELEASES_PATH);
      if (!gFlagsValidation.checkGFlagFileExists(
          releasePath, newVersion, Util.AUTO_FLAG_FILENAME)) {
        ReleaseMetadata rm = releaseManager.getReleaseByVersion(newVersion);
        try (InputStream inputStream =
            releaseManager.getTarGZipDBPackageInputStream(newVersion, rm)) {
          gFlagsValidation.fetchGFlagFilesFromTarGZipInputStream(
              inputStream,
              newVersion,
              Collections.singletonList(Util.AUTO_FLAG_FILENAME),
              releasePath);
        }
      }

      // Make sure there are no new auto flags missing from the modified version,
      // which could occur during a downgrade.
      Set<String> oldMasterAutoFlags =
          autoFlagUtil.getPromotedAutoFlags(universe, ServerType.MASTER, -1);
      if (CollectionUtils.isNotEmpty(oldMasterAutoFlags)) {
        Set<String> newMasterAutoFlags =
            gFlagsValidation.extractAutoFlags(newVersion, "yb-master").autoFlagDetails.stream()
                .map(flag -> flag.name)
                .collect(Collectors.toSet());
        checkAutoFlagsAvailability(oldMasterAutoFlags, newMasterAutoFlags, newVersion);
      }

      Set<String> oldTserverAutoFlags =
          autoFlagUtil.getPromotedAutoFlags(universe, ServerType.TSERVER, -1);
      if (!CollectionUtils.isNotEmpty(oldTserverAutoFlags)) {
        Set<String> newTserverAutoFlags =
            gFlagsValidation.extractAutoFlags(newVersion, "yb-tserver").autoFlagDetails.stream()
                .map(flag -> flag.name)
                .collect(Collectors.toSet());
        checkAutoFlagsAvailability(oldTserverAutoFlags, newTserverAutoFlags, newVersion);
      }
    } catch (PlatformServiceException e) {
      throw e;
    } catch (Exception e) {
      log.error("Error occurred while performing upgrade check: ", e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }

  private void checkAutoFlagsAvailability(
      Set<String> oldFlags, Set<String> newFlags, String version) {
    for (String oldFlag : oldFlags) {
      if (!newFlags.contains(oldFlag)) {
        throw new PlatformServiceException(
            BAD_REQUEST, oldFlag + " is not present in the requested db version " + version);
      }
    }
  }
}
