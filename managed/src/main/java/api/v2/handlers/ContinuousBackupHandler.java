package api.v2.handlers;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.NOT_FOUND;

import api.v2.mappers.ContinuousBackupMapper;
import api.v2.models.ContinuousBackup;
import api.v2.models.ContinuousBackupSpec;
import api.v2.models.ContinuousRestoreSpec;
import api.v2.models.YBATask;
import api.v2.utils.ApiControllerUtils;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.CreateContinuousBackup;
import com.yugabyte.yw.commissioner.tasks.RestoreContinuousBackup;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.models.ContinuousBackupConfig;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.TimeUnit;
import java.util.Optional;
import java.util.UUID;
import play.mvc.Http;

public class ContinuousBackupHandler extends ApiControllerUtils {

  @Inject private Commissioner commissioner;
  @Inject private ConfigHelper configHelper;

  public ContinuousBackup createContinuousBackup(
      Http.Request request, UUID cUUID, ContinuousBackupSpec continuousBackupCreateSpec)
      throws Exception {

    // Check if there is an existing config
    if (ContinuousBackupConfig.get().isPresent()) {
      throw new PlatformServiceException(BAD_REQUEST, "Continuous backup config already exists.");
    }
    ContinuousBackupConfig cbConfig =
        ContinuousBackupConfig.create(
            continuousBackupCreateSpec.getStorageConfigUuid(),
            continuousBackupCreateSpec.getFrequency(),
            TimeUnit.valueOf(continuousBackupCreateSpec.getFrequencyTimeUnit().name()),
            continuousBackupCreateSpec.getNumBackups(),
            continuousBackupCreateSpec.getBackupDir());
    CreateContinuousBackup.Params taskParams = new CreateContinuousBackup.Params();
    taskParams.storageConfigUUID = cbConfig.getStorageConfigUUID();
    taskParams.dirName = cbConfig.getBackupDir();
    // TODO: list of components?
    Schedule schedule =
        Schedule.create(
            cUUID,
            cbConfig.getUuid(),
            taskParams,
            TaskType.CreateContinuousBackup,
            cbConfig.getFrequency(),
            null,
            false /* useLocalTimezone */,
            cbConfig.getFrequencyTimeUnit(),
            "ContinuousBackupSchedule");
    return ContinuousBackupMapper.INSTANCE.toContinuousBackup(cbConfig);
  }

  public ContinuousBackup deleteContinuousBackup(Http.Request request, UUID cUUID, UUID bUUID)
      throws Exception {
    Optional<ContinuousBackupConfig> optional = ContinuousBackupConfig.get(bUUID);
    if (!optional.isPresent()) {
      throw new PlatformServiceException(BAD_REQUEST, "no continous backup config found with UUID");
    }
    // Delete the active continuous backup schedules
    Schedule.getAllActiveSchedulesByOwnerUUIDAndType(bUUID, TaskType.CreateContinuousBackup)
        .stream()
        .forEach(
            schedule -> {
              schedule.delete();
            });
    ContinuousBackupConfig.delete(bUUID);
    return new ContinuousBackup();
  }

  public ContinuousBackup editContinuousBackup(
      Http.Request request, UUID cUUID, UUID bUUID, ContinuousBackupSpec continuousBackupCreateSpec)
      throws Exception {
    Optional<ContinuousBackupConfig> optional = ContinuousBackupConfig.get(bUUID);
    if (!optional.isPresent()) {
      throw new PlatformServiceException(
          BAD_REQUEST, "No continous backup config found with UUID " + bUUID);
    }
    ContinuousBackupConfig cbConfig = optional.get();
    // TODO: Actual edit work
    return ContinuousBackupMapper.INSTANCE.toContinuousBackup(cbConfig);
  }

  public ContinuousBackup getContinuousBackup(Http.Request request, UUID cUUID) throws Exception {
    Optional<ContinuousBackupConfig> cbConfigOpt = ContinuousBackupConfig.get();
    if (!cbConfigOpt.isPresent()) {
      throw new PlatformServiceException(NOT_FOUND, "No continuous backup config found.");
    }
    ContinuousBackupConfig cbConfig = cbConfigOpt.get();
    return ContinuousBackupMapper.INSTANCE.toContinuousBackup(cbConfig);
  }

  public YBATask restoreContinuousBackup(
      Http.Request request, UUID cUUID, ContinuousRestoreSpec spec) throws Exception {
    Customer customer = Customer.getOrBadRequest(cUUID);
    RestoreContinuousBackup.Params taskParams = new RestoreContinuousBackup.Params();
    taskParams.storageConfigUUID = spec.getStorageConfigUuid();
    taskParams.backupDir = spec.getBackupDir();
    UUID taskUUID = commissioner.submit(TaskType.RestoreContinuousBackup, taskParams);
    CustomerTask.create(
        customer,
        configHelper.getYugawareUUID(),
        taskUUID,
        CustomerTask.TargetType.Yba,
        CustomerTask.TaskType.RestoreContinuousBackup,
        Util.getYwHostnameOrIP());
    return new YBATask().taskUuid(taskUUID);
  }
}
