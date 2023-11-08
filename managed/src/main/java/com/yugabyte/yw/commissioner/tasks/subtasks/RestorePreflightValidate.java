package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.forms.RestorePreflightParams;
import com.yugabyte.yw.forms.RestorePreflightResponse;
import com.yugabyte.yw.models.Backup.BackupCategory;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import java.util.Map;
import java.util.Set;
import java.util.stream.Collectors;
import org.apache.commons.lang3.StringUtils;
import org.yb.CommonTypes.TableType;

public class RestorePreflightValidate extends AbstractTaskBase {

  private final CustomerConfigService configService;

  @Inject
  protected RestorePreflightValidate(
      BaseTaskDependencies baseTaskDependencies, CustomerConfigService configService) {
    super(baseTaskDependencies);
    this.configService = configService;
  }

  @Override
  public RestoreBackupParams taskParams() {
    return (RestoreBackupParams) taskParams;
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    CustomerConfig storageConfig =
        configService.getOrBadRequest(taskParams().customerUUID, taskParams().storageConfigUUID);

    if (taskParams().category.equals(BackupCategory.YB_CONTROLLER)) {
      Set<String> backupLocations =
          taskParams()
              .backupStorageInfoList
              .parallelStream()
              .map(bSI -> bSI.storageLocation)
              .collect(Collectors.toSet());
      backupHelper.validateStorageConfigForSuccessMarkerDownloadOnUniverse(
          storageConfig, universe, backupLocations);
    }

    restoreActionPreflightCheck();

    if (taskParams().category.equals(BackupCategory.YB_CONTROLLER)) {
      backupHelper.validateStorageConfigForYbcRestoreTask(
          taskParams().storageConfigUUID,
          taskParams().customerUUID,
          taskParams().getUniverseUUID(),
          taskParams().getSuccessMarkerMap().values());
    }
  }

  private RestorePreflightResponse getRestorePreflightResponse() {
    RestorePreflightParams preflightParams = new RestorePreflightParams();
    preflightParams.setUniverseUUID(taskParams().getUniverseUUID());
    preflightParams.setStorageConfigUUID(taskParams().storageConfigUUID);
    Set<String> backupLocations =
        taskParams()
            .backupStorageInfoList
            .parallelStream()
            .map(bSI -> bSI.storageLocation)
            .collect(Collectors.toSet());
    preflightParams.setBackupLocations(backupLocations);

    return backupHelper.restorePreflightWithoutBackupObject(
        taskParams().customerUUID, preflightParams, false);
  }

  private void restoreActionPreflightCheck() {
    RestorePreflightResponse preflightResponse = getRestorePreflightResponse();

    taskParams().setSuccessMarkerMap(preflightResponse.getSuccessMarkerMap());
    // Validate common validation points like backup category, KMS, Table type etc.
    BackupUtil.validateRestoreActionUsingBackupMetadata(taskParams(), preflightResponse);

    // For first try( i.e. non-retries ) validate overwrite.
    if (shouldValidateRestoreOverwrite()) {
      // Verify non-repetitive restore request.
      Map<TableType, Map<String, Set<String>>> mapToRestore =
          YbcBackupUtil.generateMapToRestoreNonRedisYBC(
              taskParams().backupStorageInfoList, preflightResponse.getPerLocationBackupInfoMap());

      backupHelper.validateMapToRestoreWithUniverseNonRedisYBC(
          taskParams().getUniverseUUID(), mapToRestore);
    }
  }

  private boolean shouldValidateRestoreOverwrite() {
    return taskParams().category.equals(BackupCategory.YB_CONTROLLER)
        && (isFirstTry()
            || (taskParams().currentIdx == 0
                && StringUtils.isBlank(taskParams().currentYbcTaskId)));
  }
}
