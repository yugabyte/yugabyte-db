package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.commissioner.tasks.subtasks.OperatorImportResource.*;
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
import com.yugabyte.yw.common.operator.StorageConfigReconciler;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageAzureData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageGCSData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.provider.KubernetesInfo;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Slf4j
@Abortable
@Retryable
public class OperatorImportUniverse extends UniverseTaskBase {

  private final String UniverseImportSecretTaskName = "ImportUniverseSecret";

  // Injected dependencies
  private final OperatorUtils operatorUtils;

  private Set<UUID> createStorageConfigs = new HashSet<>();

  @Inject
  public OperatorImportUniverse(
      OperatorUtils operatorUtils, BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
    this.operatorUtils = operatorUtils;
  }

  public static class Params extends UniverseTaskParams {
    // UniverseUUID is also required, but already defined in the base class.
    public String namespace;
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
    try {
      Universe universe =
          lockAndFreezeUniverseForUpdate(
              taskParams().getUniverseUUID(), -1, null /* Txn callback */);

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

      createMarkUniverseUpdateSuccessTasks();
      // Now run all the subtasks
      getRunnableTask().runSubTasks();
    } finally {
      unlockUniverseForUpdate();
    }
  }

  private void createImportSecretSubtask(
      String secretName, String secretKey, String secretValue, SubTaskGroup group) {
    OperatorImportResource task = createTask(OperatorImportResource.class);
    OperatorImportResource.Params params = new OperatorImportResource.Params();
    params.secretName = secretName;
    params.secretKey = secretKey;
    params.secretValue = secretValue;
    params.resourceType = OperatorImportResource.Params.ResourceType.SECRET;
    params.namespace = taskParams().namespace;

    initializeTask(group, task, params);
    log.trace("initialized task for secret");
  }

  private void createImportStorageConfigSubtask(
      CustomerConfig storageConfig, SubTaskGroup storageCfgGroup, SubTaskGroup secretGroup) {
    if (createStorageConfigs.contains(storageConfig.getConfigUUID())) {
      log.debug("Storage config {} already created, skipping", storageConfig.getConfigUUID());
      return;
    }
    createStorageConfigs.add(storageConfig.getConfigUUID());

    OperatorImportResource task = createTask(OperatorImportResource.class);
    OperatorImportResource.Params params = new OperatorImportResource.Params();
    params.storageConfigUUID = storageConfig.getConfigUUID();
    params.resourceType = OperatorImportResource.Params.ResourceType.STORAGE_CONFIG;
    params.namespace = taskParams().namespace;
    String scName = OperatorUtils.kubernetesCompatName(storageConfig.getConfigName());
    // Create Secret task(s)
    String secretName = null;
    switch (storageConfig.getName()) {
      case Util.S3:
        CustomerConfigStorageS3Data s3Data =
            (CustomerConfigStorageS3Data) storageConfig.getDataObject();
        secretName = scName + "-aws-secret-access-key-secret";
        // Kubernetes names must be less than 63 characters
        secretName =
            secretName.substring(
                0, Math.min(secretName.length(), OperatorUtils.KUBERNETES_NAME_MAX_LENGTH));
        params.secretName = secretName;
        createImportSecretSubtask(
            secretName,
            StorageConfigReconciler.AWS_SECRET_ACCESS_KEY_SECRET_KEY,
            s3Data.awsSecretAccessKey,
            secretGroup);
        break;
      case Util.GCS:
        CustomerConfigStorageGCSData gcsData =
            (CustomerConfigStorageGCSData) storageConfig.getDataObject();
        secretName = scName + "-gcs-credentials-json-secret";
        // Kubernetes names must be less than 63 characters
        secretName =
            secretName.substring(
                0, Math.min(secretName.length(), OperatorUtils.KUBERNETES_NAME_MAX_LENGTH));
        params.secretName = secretName;
        createImportSecretSubtask(
            secretName,
            StorageConfigReconciler.GCS_CREDENTIALS_JSON_SECRET_KEY,
            gcsData.gcsCredentialsJson,
            secretGroup);
        break;
      case Util.AZ:
        CustomerConfigStorageAzureData azData =
            (CustomerConfigStorageAzureData) storageConfig.getDataObject();
        secretName = scName + "-azure-storage-sas-token-secret";
        // Kubernetes names must be less than 63 characters
        secretName =
            secretName.substring(
                0, Math.min(secretName.length(), OperatorUtils.KUBERNETES_NAME_MAX_LENGTH));
        params.secretName = secretName;
        createImportSecretSubtask(
            secretName,
            StorageConfigReconciler.AZURE_STORAGE_SAS_TOKEN_SECRET_KEY,
            azData.azureSasToken,
            secretGroup);
        break;
      case Util.NFS:
        // No secrets for NFS
        break;
      default:
        throw new RuntimeException("Unknown storage config type: " + storageConfig.getName());
    }

    initializeTask(storageCfgGroup, task, params);
    log.trace("initialized task for storage config");
  }

  private List<SubTaskGroup> createImportReleaseSubtasks(Universe universe) {
    SubTaskGroup group =
        createSubTaskGroup("ImportRelease", SubTaskGroupType.OperatorImportResource);
    OperatorImportResource task = createTask(OperatorImportResource.class);
    OperatorImportResource.Params params = new OperatorImportResource.Params();
    params.releaseVersion =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    params.universeUUID = universe.getUniverseUUID();
    params.resourceType = OperatorImportResource.Params.ResourceType.RELEASE;
    params.namespace = taskParams().namespace;
    initializeTask(group, task, params);
    log.trace("initialized task for release");
    return List.of(group);
  }

  private List<SubTaskGroup> createImportProviderSubtasks(Universe universe) {
    SubTaskGroup group =
        createSubTaskGroup("ImportProvider", SubTaskGroupType.OperatorImportResource);
    SubTaskGroup secretGroup =
        createSubTaskGroup("ImportProviderSecrets", SubTaskGroupType.OperatorImportResource);
    OperatorImportResource.Params params = new OperatorImportResource.Params();

    UUID providerUUID =
        UUID.fromString(universe.getUniverseDetails().getPrimaryCluster().userIntent.provider);
    Provider provider = Provider.getOrBadRequest(providerUUID);

    String secretKeyTemplate = provider.getName() + "-%s-kubeconfig";
    Map<String, String> secretMap = new HashMap<>();
    KubernetesInfo kubernetesInfo = provider.getDetails().getCloudInfo().getKubernetes();
    String kubeconfigContent = getKubeconfigContent(kubernetesInfo);
    // Create the provider level kubeconfig secret
    if (kubeconfigContent != null) {
      String key = String.format(secretKeyTemplate, "provider");
      secretMap.put(key, kubeconfigContent);
      params.secretMap.put(OperatorUtils.PROVIDER_KUBECONFIG_KEY, key);
    }
    // Create the zone level kubeconfig secrets
    provider
        .getRegions()
        .forEach(
            region -> {
              region
                  .getZones()
                  .forEach(
                      az -> {
                        KubernetesInfo zoneKubernetesInfo =
                            az.getDetails().getCloudInfo().getKubernetes();
                        String zoneKubeconfigContent = getKubeconfigContent(zoneKubernetesInfo);
                        if (zoneKubeconfigContent != null) {
                          String key = String.format(secretKeyTemplate, az.getCode());
                          secretMap.put(key, zoneKubeconfigContent);
                          params.secretMap.put(az.getCode(), key);
                        }
                      });
            });
    if (secretMap.size() > 0) {
      log.info("Creating {} secrets for provider {}", secretMap.size(), providerUUID);
      for (Map.Entry<String, String> entry : secretMap.entrySet()) {
        createImportSecretSubtask(entry.getKey(), "kubeconfig", entry.getValue(), secretGroup);
      }
    }
    OperatorImportResource task = createTask(OperatorImportResource.class);
    params.providerUUID =
        UUID.fromString(universe.getUniverseDetails().getPrimaryCluster().userIntent.provider);
    params.universeUUID = universe.getUniverseUUID();
    params.resourceType = OperatorImportResource.Params.ResourceType.PROVIDER;
    params.namespace = taskParams().namespace;
    initializeTask(group, task, params);
    log.trace("initialized task for provider");
    return List.of(secretGroup, group);
  }

  private String getKubeconfigContent(KubernetesInfo kubernetesInfo) {
    if (kubernetesInfo.getKubeConfigContent() != null) {
      return kubernetesInfo.getKubeConfigContent();
    }
    if (kubernetesInfo.getKubeConfig() != null) {
      String kubeConfigPath = kubernetesInfo.getKubeConfig();
      if (!kubeConfigPath.isEmpty()) {
        try {
          if (Files.exists(Paths.get(kubeConfigPath))) {
            return new String(
                Files.readAllBytes(Paths.get(kubeConfigPath)), StandardCharsets.UTF_8);
          }
        } catch (Exception e) {
          log.warn("Failed to read kubeconfig file at path: {}", kubeConfigPath, e);
        }
      }
    }
    return null;
  }

  private List<SubTaskGroup> createImportUniverseSubtasks(Universe universe) {
    List<SubTaskGroup> groups = new ArrayList<>();

    // Create universe params first so we can reference them when creating secrets
    OperatorImportResource.Params universeParams = new OperatorImportResource.Params();
    universeParams.universeUUID = universe.getUniverseUUID();
    universeParams.resourceType = OperatorImportResource.Params.ResourceType.UNIVERSE;
    universeParams.namespace = taskParams().namespace;

    SubTaskGroup group =
        createSubTaskGroup("ImportUniverse", SubTaskGroupType.OperatorImportResource);
    groups.add(group);
    OperatorImportResource task = createTask(OperatorImportResource.class);
    initializeTask(group, task, universeParams);
    log.trace("initialized task for universe");
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
          CustomerConfig storageConfig = CustomerConfig.get(backupParams.storageConfigUUID);
          createImportStorageConfigSubtask(storageConfig, storageCfgGroup, secretGroup);

          // Now migrate the actual schedule.
          OperatorImportResource task = createTask(OperatorImportResource.class);
          OperatorImportResource.Params params = new OperatorImportResource.Params();
          params.backupScheduleUUID = schedule.getScheduleUUID();
          params.resourceType = OperatorImportResource.Params.ResourceType.BACKUP_SCHEDULE;
          params.namespace = taskParams().namespace;
          params.storageConfigName =
              OperatorUtils.kubernetesCompatName(storageConfig.getConfigName());

          // Add any secrets from storage configs to this backup schedule's secret map
          // The storage config subtask will have populated the secret map
          initializeTask(group, task, params);
          log.trace("initialized task for backup schedule");
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
          CustomerConfig storageConfig = CustomerConfig.get(backup.getStorageConfigUUID());
          if (storageConfig == null
              || !storageConfig.getType().equals(CustomerConfig.ConfigType.STORAGE)) {
            throw new PlatformServiceException(
                BAD_REQUEST,
                String.format(
                    "no storage config %s found or is not a storage config",
                    storageConfig.getConfigUUID()));
          }
          createImportStorageConfigSubtask(storageConfig, storageCfgGroup, secretGroup);

          OperatorImportResource task = createTask(OperatorImportResource.class);
          OperatorImportResource.Params params = new OperatorImportResource.Params();
          params.backupUUID = backup.getBackupUUID();
          params.customerUUID = customer.getUuid();
          params.storageConfigName =
              OperatorUtils.kubernetesCompatName(storageConfig.getConfigName());
          params.resourceType = OperatorImportResource.Params.ResourceType.BACKUP;
          params.namespace = taskParams().namespace;
          // Add any secrets from storage configs to this backup's secret map
          // The storage config subtask will have populated the secret map
          initializeTask(group, task, params);
          log.trace("initialized task for backup");
        });
    return List.of(secretGroup, storageCfgGroup, group);
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
