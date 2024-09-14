// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.check;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.ServerSubTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.gflags.AutoFlagUtil;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import java.io.IOException;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class CheckXUniverseAutoFlags extends ServerSubTaskBase {

  private final AutoFlagUtil autoFlagUtil;
  private final GFlagsValidation gFlagsValidation;

  @Inject
  protected CheckXUniverseAutoFlags(
      BaseTaskDependencies baseTaskDependencies,
      GFlagsValidation gFlagsValidation,
      AutoFlagUtil autoFlagUtil) {
    super(baseTaskDependencies);
    this.gFlagsValidation = gFlagsValidation;
    this.autoFlagUtil = autoFlagUtil;
  }

  public static class Params extends ServerSubTaskParams {
    public UUID sourceUniverseUUID;
    public UUID targetUniverseUUID;
    public boolean checkAutoFlagsEqualityOnBothUniverses = false;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    Universe sourceUniverse = Universe.getOrBadRequest(taskParams().sourceUniverseUUID);
    String sourceUniverseSoftwareVersion =
        sourceUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    boolean isSourceUniverseAFCompatible =
        CommonUtils.isAutoFlagSupported(sourceUniverseSoftwareVersion);

    Universe targetUniverse = Universe.getOrBadRequest(taskParams().targetUniverseUUID);
    String targetUniverseSoftwareVersion =
        targetUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    boolean isTargetUniverseAFCompatible =
        CommonUtils.isAutoFlagSupported(targetUniverseSoftwareVersion);

    if (!isSourceUniverseAFCompatible && !isTargetUniverseAFCompatible) {
      log.warn("Skipping auto flags compatibility check as both universe are not AF compatible.");
      return;
    }
    try {
      // Check the existence of promoted auto flags on the other universe.
      checkPromotedAutoFlags(sourceUniverse, targetUniverse, ServerType.MASTER);
      checkPromotedAutoFlags(sourceUniverse, targetUniverse, ServerType.TSERVER);
      if (taskParams().checkAutoFlagsEqualityOnBothUniverses) {
        checkPromotedAutoFlags(targetUniverse, sourceUniverse, ServerType.MASTER);
        checkPromotedAutoFlags(targetUniverse, sourceUniverse, ServerType.TSERVER);
      }

    } catch (PlatformServiceException pe) {
      log.error("Error checking auto flags: ", pe);
      throw pe;
    } catch (IOException e) {
      log.error("Error checking auto flags: ", e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
    log.info("Completed {}", getName());
  }

  /**
   * Validates that every promoted auto flags of the source universe exists in the target universe.
   *
   * @param sourceUniverse
   * @param targetUniverse
   * @param serverType
   * @throws IOException
   */
  private void checkPromotedAutoFlags(
      Universe sourceUniverse, Universe targetUniverse, ServerType serverType) throws IOException {
    // Prepare map of promoted auto flags.
    Set<String> promotedAndModifiedAutoFlags =
        autoFlagUtil.getPromotedAutoFlags(
            sourceUniverse, serverType, AutoFlagUtil.LOCAL_PERSISTED_AUTO_FLAG_CLASS);
    // Fetch set of auto flags supported on target universe.
    String targetUniverseVersion =
        targetUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    Set<String> supportedAutoFlags =
        gFlagsValidation
            .extractAutoFlags(
                targetUniverseVersion,
                serverType.equals(ServerType.MASTER) ? "yb-master" : "yb-tserver")
            .autoFlagDetails
            .stream()
            .map(flag -> flag.name)
            .collect(Collectors.toSet());
    promotedAndModifiedAutoFlags.forEach(
        flag -> {
          if (!supportedAutoFlags.contains(flag)) {
            throw new PlatformServiceException(
                BAD_REQUEST,
                "Auto Flag "
                    + flag
                    + " set on universe "
                    + sourceUniverse.getUniverseUUID()
                    + " is not present on universe "
                    + targetUniverse.getUniverseUUID());
          }
        });
  }
}
