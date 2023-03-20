// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.check;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.inject.Inject;
import com.google.protobuf.ProtocolStringList;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.ServerSubTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.ReleaseManager.ReleaseMetadata;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.common.gflags.GFlagsValidation.AutoFlagsPerServer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import java.io.InputStream;
import java.util.Collections;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.yb.WireProtocol.AutoFlagsConfigPB;
import org.yb.client.YBClient;

@Slf4j
public class CheckUpgrade extends ServerSubTaskBase {

  private final GFlagsValidation gFlagsValidation;
  private final ReleaseManager releaseManager;
  private final Config appConfig;

  @Inject
  protected CheckUpgrade(
      BaseTaskDependencies baseTaskDependencies,
      Config appConfig,
      GFlagsValidation gFlagsValidation,
      ReleaseManager releaseManager) {
    super(baseTaskDependencies);
    this.appConfig = appConfig;
    this.gFlagsValidation = gFlagsValidation;
    this.releaseManager = releaseManager;
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

      AutoFlagsConfigPB autoFlagConfig = null;
      // Fetch the promoted auto flags list.
      try (YBClient client = getClient(); ) {
        autoFlagConfig = client.autoFlagsConfig().getAutoFlagsConfig();
      } catch (Exception e) {
        throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
      }

      // Make sure there are no new auto flags missing from the modified version,
      // which could occur during a downgrade.
      if (autoFlagConfig != null && autoFlagConfig.getPromotedFlagsList().size() > 0) {
        ProtocolStringList oldMasterAutoFlags = autoFlagConfig.getPromotedFlags(0).getFlagsList();
        ProtocolStringList oldTserverAutoFlags = autoFlagConfig.getPromotedFlags(1).getFlagsList();
        if (CollectionUtils.isNotEmpty(oldMasterAutoFlags)) {
          AutoFlagsPerServer newMasterAutoFlags =
              gFlagsValidation.extractAutoFlags(newVersion, "yb-master");
          checkAutoFlagsAvailability(oldMasterAutoFlags, newMasterAutoFlags, newVersion);
        }

        if (!CollectionUtils.isNotEmpty(oldTserverAutoFlags)) {
          AutoFlagsPerServer newTserverAutoFlags =
              gFlagsValidation.extractAutoFlags(newVersion, "yb-tserver");
          checkAutoFlagsAvailability(oldTserverAutoFlags, newTserverAutoFlags, newVersion);
        }
      }
    } catch (PlatformServiceException e) {
      throw e;
    } catch (Exception e) {
      log.error("Error occurred while performing upgrade check: ", e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }

  private void checkAutoFlagsAvailability(
      ProtocolStringList oldFlags, AutoFlagsPerServer newFlags, String version) {
    for (String oldFlag : oldFlags) {
      if (newFlags.autoFlagDetails.stream().noneMatch((flag -> flag.name.equals(oldFlag)))) {
        throw new PlatformServiceException(
            BAD_REQUEST, oldFlag + " is not present in the requested db version " + version);
      }
    }
  }
}
