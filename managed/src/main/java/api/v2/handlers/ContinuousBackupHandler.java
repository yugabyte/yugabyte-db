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
import com.yugabyte.yw.common.StorageUtil;
import com.yugabyte.yw.common.StorageUtilFactory;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.models.ContinuousBackupConfig;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.TimeUnit;
import java.util.Optional;
import java.util.UUID;
import play.mvc.Http;

public class ContinuousBackupHandler extends ApiControllerUtils {

  @Inject private Commissioner commissioner;
  @Inject private ConfigHelper configHelper;
  @Inject private StorageUtilFactory storageUtilFactory;

  public ContinuousBackup createContinuousBackup(
      Http.Request request, UUID cUUID, ContinuousBackupSpec continuousBackupCreateSpec)
      throws Exception {

    // Check if there is an existing config
    if (ContinuousBackupConfig.get().isPresent()) {
      throw new PlatformServiceException(BAD_REQUEST, "Continuous backup config already exists.");
    }
    UUID storageConfigUUID = continuousBackupCreateSpec.getStorageConfigUuid();
    CustomerConfig customerConfig = CustomerConfig.get(storageConfigUUID);
    if (customerConfig == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Could not find storage config UUID.");
    }
    ContinuousBackupConfig cbConfig =
        ContinuousBackupConfig.create(
            storageConfigUUID,
            continuousBackupCreateSpec.getFrequency(),
            TimeUnit.valueOf(continuousBackupCreateSpec.getFrequencyTimeUnit().name()),
            continuousBackupCreateSpec.getNumBackups(),
            continuousBackupCreateSpec.getBackupDir());
    StorageUtil storageUtil = storageUtilFactory.getStorageUtil(customerConfig.getName());
    String storageLocation =
        storageUtil.getStorageLocation(
            customerConfig.getDataObject(), continuousBackupCreateSpec.getBackupDir());
    if (storageLocation == null || storageLocation.isBlank()) {
      throw new PlatformServiceException(BAD_REQUEST, "Could not determine storage location.");
    }
    cbConfig.updateStorageLocation(storageLocation);

    CreateContinuousBackup.Params taskParams = new CreateContinuousBackup.Params();
    taskParams.cbConfig = cbConfig;
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
    // Delete the metrics Gauge
    CreateContinuousBackup.clearGauge();
    ContinuousBackupConfig.delete(bUUID);
    return new ContinuousBackup();
  }

  public ContinuousBackup editContinuousBackup(
      Http.Request request, UUID cUUID, UUID bUUID, ContinuousBackupSpec continuousBackupEditSpec)
      throws Exception {
    Optional<ContinuousBackupConfig> optional = ContinuousBackupConfig.get(bUUID);
    // Validate params
    if (!optional.isPresent()) {
      throw new PlatformServiceException(
          BAD_REQUEST, "No continous backup config found with UUID " + bUUID);
    }
    UUID newStorageConfigUuid = continuousBackupEditSpec.getStorageConfigUuid();
    CustomerConfig customerConfig = CustomerConfig.get(newStorageConfigUuid);
    if (customerConfig == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Could not find storage config UUID.");
    }
    StorageUtil storageUtil = storageUtilFactory.getStorageUtil(customerConfig.getName());
    String storageLocation =
        storageUtil.getStorageLocation(
            customerConfig.getDataObject(), continuousBackupEditSpec.getBackupDir());
    if (storageLocation == null || storageLocation.isBlank()) {
      throw new PlatformServiceException(BAD_REQUEST, "Could not determine storage location.");
    }

    // Delete the active continuous backup schedules
    Schedule.getAllActiveSchedulesByOwnerUUIDAndType(bUUID, TaskType.CreateContinuousBackup)
        .stream()
        .forEach(
            schedule -> {
              schedule.delete();
            });
    // Clear metrics
    CreateContinuousBackup.clearGauge();

    // Edit work
    ContinuousBackupConfig cbConfig = optional.get();
    cbConfig.setStorageConfigUUID(continuousBackupEditSpec.getStorageConfigUuid());
    cbConfig.setBackupDir(continuousBackupEditSpec.getBackupDir());
    cbConfig.setFrequency(continuousBackupEditSpec.getFrequency());
    cbConfig.setFrequencyTimeUnit(
        TimeUnit.valueOf(continuousBackupEditSpec.getFrequencyTimeUnit().name()));
    cbConfig.setNumBackupsToRetain(continuousBackupEditSpec.getNumBackups());
    cbConfig.setStorageLocation(storageLocation);
    cbConfig.update();
    CreateContinuousBackup.Params taskParams = new CreateContinuousBackup.Params();
    taskParams.cbConfig = cbConfig;
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
