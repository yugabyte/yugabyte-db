// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.check;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.ServerSubTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.AutoFlagUtil;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import java.io.IOException;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class CheckXUniverseAutoFlags extends ServerSubTaskBase {

  @Inject
  protected CheckXUniverseAutoFlags(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
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
    if (confGetter.getConfForScope(
            sourceUniverse, UniverseConfKeys.skipAutoflagsAndYsqlMigrationFilesValidation)
        || confGetter.getConfForScope(
            targetUniverse, UniverseConfKeys.skipAutoflagsAndYsqlMigrationFilesValidation)) {
      log.info("Skipping auto flags and YSQL migration files validation");
      return;
    }
    try {
      // Check the existence of promoted auto flags on the other universe.
      checkPromotedAutoFlags(sourceUniverse, targetUniverse, ServerType.MASTER);
      checkPromotedAutoFlags(sourceUniverse, targetUniverse, ServerType.TSERVER);
      validateYsqlMigrationFiles(sourceUniverse, targetUniverse);
      if (taskParams().checkAutoFlagsEqualityOnBothUniverses) {
        checkPromotedAutoFlags(targetUniverse, sourceUniverse, ServerType.MASTER);
        checkPromotedAutoFlags(targetUniverse, sourceUniverse, ServerType.TSERVER);
        validateYsqlMigrationFiles(targetUniverse, sourceUniverse);
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

  private void validateYsqlMigrationFiles(Universe sourceUniverse, Universe targetUniverse)
      throws IOException {
    String sourceUniverseVersion =
        sourceUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    if (sourceUniverse.getUniverseDetails().isSoftwareRollbackAllowed
        && sourceUniverse.getUniverseDetails().prevYBSoftwareConfig != null) {
      sourceUniverseVersion =
          sourceUniverse.getUniverseDetails().prevYBSoftwareConfig.getSoftwareVersion();
    }
    String targetUniverseVersion =
        targetUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    if (targetUniverse.getUniverseDetails().isSoftwareRollbackAllowed
        && targetUniverse.getUniverseDetails().prevYBSoftwareConfig != null) {
      targetUniverseVersion =
          targetUniverse.getUniverseDetails().prevYBSoftwareConfig.getSoftwareVersion();
    }
    Set<String> sourceYsqlMigrationFiles =
        gFlagsValidation.getYsqlMigrationFilesList(sourceUniverseVersion);
    Set<String> targetYsqlMigrationFiles =
        gFlagsValidation.getYsqlMigrationFilesList(targetUniverseVersion);
    if (sourceUniverseVersion.startsWith("2025.1.0")
        || targetUniverseVersion.startsWith("2025.1.0")) {
      log.info("Skipping YSQL migration files validation for 2025.1.0 version");
      return;
    }
    // Check if the sourceYsqlMigrationFiles is subset of targetYsqlMigrationFiles.
    if (!targetYsqlMigrationFiles.containsAll(sourceYsqlMigrationFiles)) {
      log.error(
          "Universe {} YSQL migration files are not a subset of universe {} YSQL migration files."
              + " Universe {} YSQL migration files: {}, Universe {} YSQL migration files: {}",
          sourceUniverse.getUniverseUUID(),
          targetUniverse.getUniverseUUID(),
          sourceUniverse.getUniverseUUID(),
          sourceYsqlMigrationFiles,
          targetUniverse.getUniverseUUID(),
          targetYsqlMigrationFiles);
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Universe "
              + sourceUniverse.getUniverseUUID()
              + " YSQL migration files are not a subset of universe "
              + targetUniverse.getUniverseUUID()
              + " YSQL migration files.");
    }
  }
}
