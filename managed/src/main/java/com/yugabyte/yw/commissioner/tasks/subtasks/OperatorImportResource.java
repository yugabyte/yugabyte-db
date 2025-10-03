package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.datadoghq.jakarta.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class OperatorImportResource extends UniverseTaskBase {

  @Inject
  public OperatorImportResource(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseTaskParams {
    public enum ResourceType {
      SECRET,
      RELEASE,
      STORAGE_CONFIG,
      PROVIDER,
      UNIVERSE,
      BACKUP_SCHEDULE,
      BACKUP,
      CERTIFICATE,
    }

    public ResourceType resourceType;

    // For release type
    public String releaseVersion;

    // For storage config type
    public UUID storageConfigUUID;

    // For provider type
    public UUID providerUUID;

    // For universe type
    public UUID universeUUID;

    // For backup schedule type
    public UUID backupScheduleUUID;

    // For backup type
    public UUID backupUUID;
    public UUID customerUUID;

    // For certificate type
    public UUID certificateUUID;

    // For secret type
    public String secretName;
    public String secretValue;
  }

  @Override
  public Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void validateParams(boolean firstTry) {
    super.validateParams(firstTry);
    // Validate that each resource type has its corresponding params set.
    switch (taskParams().resourceType) {
      case SECRET:
        if (taskParams().secretName == null || taskParams().secretValue == null) {
          throw new IllegalArgumentException("Secret name and value must be provided");
        }
        break;
      case RELEASE:
        if (taskParams().releaseVersion == null) {
          throw new IllegalArgumentException("Release version must be provided");
        }
        break;
      case STORAGE_CONFIG:
        if (taskParams().storageConfigUUID == null) {
          throw new IllegalArgumentException("Storage config UUID must be provided");
        }
        CustomerConfig cfg = CustomerConfig.get(taskParams().storageConfigUUID);
        if (cfg == null || !cfg.getType().equals(CustomerConfig.ConfigType.STORAGE)) {
          throw new IllegalArgumentException("Invalid storage config");
        }
        break;
      case PROVIDER:
        if (taskParams().providerUUID == null) {
          throw new IllegalArgumentException("Provider UUID must be provided");
        }
        Provider.getOrBadRequest(taskParams().providerUUID);
        break;
      case UNIVERSE:
        if (taskParams().universeUUID == null) {
          throw new IllegalArgumentException("Universe UUID must be provided");
        }
        if (!Util.isKubernetesBasedUniverse(Universe.getOrBadRequest(taskParams().universeUUID))) {
          throw new IllegalArgumentException("Universe must be a Kubernetes-based universe");
        }
        break;
      case BACKUP_SCHEDULE:
        if (taskParams().backupScheduleUUID == null) {
          throw new IllegalArgumentException("Backup schedule UUID must be provided");
        }
        Schedule.getOrBadRequest(taskParams().backupScheduleUUID);
        break;
      case BACKUP:
        if (taskParams().backupUUID == null) {
          throw new IllegalArgumentException("Backup UUID must be provided");
        }
        if (taskParams().customerUUID == null) {
          throw new IllegalArgumentException("Customer UUID must be provided");
        }
        Backup.getOrBadRequest(taskParams().customerUUID, taskParams().backupUUID);
        break;
      case CERTIFICATE:
        // Validate certificate parameters
        log.warn("Certificate validation not implemented");
        break;
      default:
        throw new IllegalArgumentException("Unknown resource type: " + taskParams().resourceType);
    }
  }

  @Override
  public void run() {
    log.warn("operatorimportresource task 'run' not implemented yet");
  }
}
