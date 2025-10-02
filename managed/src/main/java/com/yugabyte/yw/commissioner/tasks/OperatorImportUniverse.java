package com.yugabyte.yw.commissioner.tasks;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.OperatorImportResource;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageAzureData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageGCSData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Slf4j
@Abortable
@Retryable
public class OperatorImportUniverse extends UniverseTaskBase {

  private final String UniverseImportSecretTaskName = "ImportUniverseSecret";

  private Set<UUID> createStorageConfigs = new HashSet<>();

  @Inject
  public OperatorImportUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseTaskParams {
    UUID universeUUID;
  }

  @Override
  public Params taskParams() {
    return (Params) taskParams;
  }

  private static interface ImportResourceTaskCreator {
    List<SubTaskGroup> createTask(Universe universe);
  }

  // Example list of task creators
  private final List<ImportResourceTaskCreator> importResourceTaskCreators =
      Arrays.asList(
          this::createImportReleaseSubtasks,
          this::createImportProviderSubtasks,
          this::createImportUniverseSubtasks,
          this::createImportBackupSchedulesSubtasks,
          this::createImportBackupsSubtasks,
          this::createImportCertificatesSubtasks);

  @Override
  public void run() {
    Universe universe =
        lockAndFreezeUniverseForUpdate(
            taskParams().universeUUID, taskParams().expectedUniverseVersion, null);

    for (ImportResourceTaskCreator creator : importResourceTaskCreators) {
      log.trace("creating subtaskGroup with creator {}", creator);
      List<SubTaskGroup> subTaskGroups = creator.createTask(universe);
      if (subTaskGroups == null || subTaskGroups.isEmpty()) {
        log.debug("no tasks needed for resource creator {}", creator);
        continue;
      }
      log.trace(
          "adding {} subtask(s) group for resource creator {}", subTaskGroups.size(), creator);
      for (SubTaskGroup subTaskGroup : subTaskGroups) {
        getRunnableTask().addSubTaskGroup(subTaskGroup);
      }
    }

    // Now run all the subtasks
    getRunnableTask().runSubTasks();
  }

  private void createImportSecretSubtask(
      String secretName, String secretValue, SubTaskGroup group) {
    OperatorImportResource task = createTask(OperatorImportResource.class);
    OperatorImportResource.Params params = new OperatorImportResource.Params();
    params.secretName = secretName;
    params.secretValue = secretValue;
    params.resourceType = OperatorImportResource.Params.ResourceType.SECRET;
    initializeTask(group, task, params);
  }

  private void createImportStorageConfigSubtask(
      UUID storageConfigUUID, SubTaskGroup storageCfgGroup, SubTaskGroup secretGroup) {
    if (createStorageConfigs.contains(storageConfigUUID)) {
      log.debug("Storage config {} already created, skipping", storageConfigUUID);
      return;
    }
    createStorageConfigs.add(storageConfigUUID);
    CustomerConfig storageConfig = CustomerConfig.get(storageConfigUUID);
    if (storageConfig == null
        || !storageConfig.getType().equals(CustomerConfig.ConfigType.STORAGE)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "no storage config %s found or is not a storage config", storageConfigUUID));
    }
    // Create Secret task(s)
    switch (storageConfig.getName()) {
      case Util.S3:
        CustomerConfigStorageS3Data s3Data =
            (CustomerConfigStorageS3Data) storageConfig.getDataObject();
        createImportSecretSubtask(
            "awsSecretAccessKeySecret", s3Data.awsSecretAccessKey, secretGroup);
        break;
      case Util.GCS:
        CustomerConfigStorageGCSData gcsData =
            (CustomerConfigStorageGCSData) storageConfig.getDataObject();
        createImportSecretSubtask(
            "gcsCredentialsJsonSecret", gcsData.gcsCredentialsJson, secretGroup);
        break;
      case Util.AZ:
        CustomerConfigStorageAzureData azData =
            (CustomerConfigStorageAzureData) storageConfig.getDataObject();
        createImportSecretSubtask("azureStorageSasTokenSecret", azData.azureSasToken, secretGroup);
        break;
      case Util.NFS:
        // No secrets for NFS
        break;
      default:
        throw new RuntimeException("Unknown storage config type: " + storageConfig.getName());
    }

    OperatorImportResource task = createTask(OperatorImportResource.class);
    OperatorImportResource.Params params = new OperatorImportResource.Params();
    params.storageConfigUUID = storageConfigUUID;
    params.resourceType = OperatorImportResource.Params.ResourceType.STORAGE_CONFIG;
    initializeTask(storageCfgGroup, task, params);
  }

  private List<SubTaskGroup> createImportReleaseSubtasks(Universe universe) {
    SubTaskGroup group =
        createSubTaskGroup("ImportRelease", SubTaskGroupType.OperatorImportResource);
    OperatorImportResource task = createTask(OperatorImportResource.class);
    OperatorImportResource.Params params = new OperatorImportResource.Params();
    params.releaseVersion =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    params.resourceType = OperatorImportResource.Params.ResourceType.RELEASE;
    initializeTask(group, task, params);
    return List.of(group);
  }

  private List<SubTaskGroup> createImportProviderSubtasks(Universe universe) {
    SubTaskGroup group =
        createSubTaskGroup("ImportProvider", SubTaskGroupType.OperatorImportResource);

    // TODO: Any provider secrets should have tasks created here. Waiting for Provider CRD
    // to land before adding them
    OperatorImportResource task = createTask(OperatorImportResource.class);
    OperatorImportResource.Params params = new OperatorImportResource.Params();
    params.providerUUID =
        UUID.fromString(universe.getUniverseDetails().getPrimaryCluster().userIntent.provider);
    params.resourceType = OperatorImportResource.Params.ResourceType.PROVIDER;
    initializeTask(group, task, params);
    return List.of(group);
  }

  private List<SubTaskGroup> createImportUniverseSubtasks(Universe universe) {
    List<SubTaskGroup> groups = new ArrayList<>();
    String secretName;
    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    SubTaskGroup secretGroup =
        createSubTaskGroup(UniverseImportSecretTaskName, SubTaskGroupType.OperatorImportResource);
    if (userIntent.isYSQLAuthEnabled()) {
      secretName = universe.getName() + "-ysqlpassword";
      createImportSecretSubtask(secretName, userIntent.ysqlPassword, secretGroup);
    }
    if (userIntent.enableYCQLAuth) {
      secretName = universe.getName() + "-ycqlpassword";
      createImportSecretSubtask(secretName, userIntent.ycqlPassword, secretGroup);
    }
    if (secretGroup.getSubTaskCount() > 0) {
      groups.add(secretGroup);
    }
    SubTaskGroup group =
        createSubTaskGroup("ImportUniverse", SubTaskGroupType.OperatorImportResource);
    groups.add(group);
    OperatorImportResource task = createTask(OperatorImportResource.class);
    OperatorImportResource.Params params = new OperatorImportResource.Params();
    params.universeUUID = universe.getUniverseUUID();
    params.resourceType = OperatorImportResource.Params.ResourceType.UNIVERSE;
    initializeTask(group, task, params);
    return groups;
  }

  private List<SubTaskGroup> createImportBackupSchedulesSubtasks(Universe universe) {
    SubTaskGroup group =
        createSubTaskGroup("ImportBackupSchedules", SubTaskGroupType.OperatorImportResource);
    SubTaskGroup storageCfgGroup =
        createSubTaskGroup(
            "ImportBackupSchedulesStorageConfigs", SubTaskGroupType.OperatorImportResource);
    SubTaskGroup secretGroup =
        createSubTaskGroup("ImportBackupSchedulesSecrets", SubTaskGroupType.OperatorImportResource);
    List<Schedule> schedules =
        Schedule.getAllSchedulesByOwnerUUIDAndType(
            universe.getUniverseUUID(), TaskType.CreateBackup);
    schedules.forEach(
        schedule -> {
          // Create tasks for migrating storage configs
          BackupRequestParams backupParams =
              Json.mapper().convertValue(schedule.getTaskParams(), BackupRequestParams.class);
          createImportStorageConfigSubtask(backupParams.scheduleUUID, storageCfgGroup, secretGroup);

          // Now migrate the actual schedule.
          OperatorImportResource task = createTask(OperatorImportResource.class);
          OperatorImportResource.Params params = new OperatorImportResource.Params();
          params.backupScheduleUUID = schedule.getScheduleUUID();
          params.resourceType = OperatorImportResource.Params.ResourceType.BACKUP_SCHEDULE;
          initializeTask(group, task, params);
        });
    return List.of(secretGroup, storageCfgGroup, group);
  }

  private List<SubTaskGroup> createImportBackupsSubtasks(Universe universe) {
    Customer customer = Customer.get(universe.getCustomerId());
    SubTaskGroup group =
        createSubTaskGroup("ImportBackups", SubTaskGroupType.OperatorImportResource);
    SubTaskGroup storageCfgGroup =
        createSubTaskGroup("ImportBackupsStorageConfigs", SubTaskGroupType.OperatorImportResource);
    SubTaskGroup secretGroup =
        createSubTaskGroup("ImportBackupsSecrets", SubTaskGroupType.OperatorImportResource);

    List<Backup> backups =
        Backup.fetchByUniverseUUID(customer.getUuid(), universe.getUniverseUUID());
    backups.forEach(
        backup -> {
          createImportStorageConfigSubtask(
              backup.getStorageConfigUUID(), storageCfgGroup, secretGroup);

          OperatorImportResource task = createTask(OperatorImportResource.class);
          OperatorImportResource.Params params = new OperatorImportResource.Params();
          params.backupUUID = backup.getBackupUUID();
          params.resourceType = OperatorImportResource.Params.ResourceType.BACKUP;
          initializeTask(group, task, params);
        });
    return List.of(group);
  }

  private List<SubTaskGroup> createImportCertificatesSubtasks(Universe universe) {
    log.warn("No import task for certificates yet.");
    return null;
  }

  private void initializeTask(
      SubTaskGroup group, OperatorImportResource task, OperatorImportResource.Params params) {
    task.initialize(params);
    task.setUserTaskUUID(getUserTaskUUID());
    group.addSubTask(task);
  }
}
