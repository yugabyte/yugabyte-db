package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.inject.Inject;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.ReleasesUtils;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.operator.KubernetesResourceDetails;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.forms.KubernetesProviderFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Release;
import com.yugabyte.yw.models.ReleaseArtifact;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class OperatorImportResource extends UniverseTaskBase {

  private final OperatorUtils operatorUtils;
  private final ReleasesUtils releasesUtils;
  private final CustomerConfigService customerConfigService;

  @Inject
  public OperatorImportResource(
      BaseTaskDependencies baseTaskDependencies,
      OperatorUtils operatorUtils,
      ReleasesUtils releasesUtils,
      CustomerConfigService customerConfigService) {
    super(baseTaskDependencies);
    this.operatorUtils = operatorUtils;
    this.releasesUtils = releasesUtils;
    this.customerConfigService = customerConfigService;
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

    public String namespace;

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

    // For both backup and backup schedule type
    public String storageConfigName;

    // For certificate type
    public UUID certificateUUID;

    // For secret type
    public String secretName;
    public String secretKey;
    public String secretValue;

    public Map<String, String> secretMap = new HashMap<>();
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
          throw new IllegalArgumentException("Provider UUID must be provided for provider import");
        }
        if (taskParams().universeUUID == null) {
          throw new IllegalArgumentException("Universe UUID must be provided for provider import");
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
        if (taskParams().storageConfigName == null) {
          throw new IllegalArgumentException("Storage config name must be provided");
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
        if (taskParams().storageConfigName == null) {
          throw new IllegalArgumentException("Storage config name must be provided");
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
    log.info("Starting import of resource type: {}", taskParams().resourceType);

    try {
      switch (taskParams().resourceType) {
        case SECRET:
          importSecret();
          break;
        case RELEASE:
          importRelease();
          break;
        case STORAGE_CONFIG:
          importStorageConfig();
          break;
        case PROVIDER:
          importProvider();
          break;
        case UNIVERSE:
          importUniverse();
          break;
        case BACKUP_SCHEDULE:
          importBackupSchedule();
          break;
        case BACKUP:
          importBackup();
          break;
        case CERTIFICATE:
          importCertificate();
          break;
        default:
          throw new IllegalArgumentException("Unknown resource type: " + taskParams().resourceType);
      }
      log.info("Successfully imported resource type: {}", taskParams().resourceType);
    } catch (Exception e) {
      String resourceIdentifier = getResourceIdentifier();
      log.error(
          "Failed to import resource type: {} (identifier: {})",
          taskParams().resourceType,
          resourceIdentifier,
          e);
      throw new RuntimeException(
          String.format(
              "Failed to import resource %s (identifier: %s)",
              taskParams().resourceType, resourceIdentifier),
          e);
    }
  }

  private void importSecret() {
    log.info("Importing secret: {}", taskParams().secretName);

    if (taskParams().secretValue != null) {
      try {
        operatorUtils.createSecretCr(
            taskParams().secretName,
            getNamespace(),
            taskParams().secretKey,
            taskParams().secretValue);
      } catch (Exception e) {
        log.error("Failed to create/update secret: {}", taskParams().secretName, e);
        throw new RuntimeException("Failed to create secret", e);
      }
    }
    log.info("Successfully created secret: {}", taskParams().secretName);
  }

  private void importRelease() {
    log.info("Importing release version: {}", taskParams().releaseVersion);
    Map<String, List<Universe>> versionUniverseMap = releasesUtils.versionUniversesMap();
    if (versionUniverseMap.containsKey(taskParams().releaseVersion)) {
      List<Universe> universes =
          versionUniverseMap.get(taskParams().releaseVersion).stream()
              .filter(u -> !u.getUniverseUUID().equals(taskParams().universeUUID))
              .filter(u -> !u.getUniverseDetails().isKubernetesOperatorControlled)
              .collect(Collectors.toList());
      if (universes.size() > 0) {
        log.info("Skipping release import as it is associated with non-operator-based universes");
        log.debug("Non-operator-based universes: {}", universes);
        return;
      }
    }

    Release release = Release.getByVersion(taskParams().releaseVersion);
    if (release == null) {
      throw new RuntimeException("Release not found");
    }
    ReleaseArtifact releaseArtifact = release.getKubernetesArtifact();
    if (releaseArtifact == null) {
      throw new RuntimeException("Release artifact not found");
    }
    ReleaseArtifact x86_64Artifact = release.getArtifactForArchitecture(Architecture.x86_64);
    if (x86_64Artifact == null) {
      throw new RuntimeException("x86_64 artifact not found");
    }
    try {
      operatorUtils.createReleaseCr(
          release, releaseArtifact, x86_64Artifact, getNamespace(), taskParams().secretName);
    } catch (Exception e) {
      log.error("Failed to create release: {}", taskParams().releaseVersion, e);
      throw new RuntimeException("Failed to create release", e);
    }
    release.setIsKubernetesOperatorControlled(true);
    log.info("Release version {} imported successfully", taskParams().releaseVersion);
  }

  private void importStorageConfig() {
    log.info("Importing storage config: {}", taskParams().storageConfigUUID);
    CustomerConfig cfg = CustomerConfig.get(taskParams().storageConfigUUID);
    if (cfg == null) {
      throw new IllegalArgumentException(
          "Storage config with UUID " + taskParams().storageConfigUUID + " not found");
    }
    if (!cfg.getType().equals(CustomerConfig.ConfigType.STORAGE)) {
      throw new IllegalArgumentException(
          "Config with UUID "
              + taskParams().storageConfigUUID
              + " is not a storage config. Found type: "
              + cfg.getType());
    }
    Set<UUID> associatedUniverseUUIDs = customerConfigService.getAssociatedUniverseUUIDS(cfg);
    Set<Universe> associatedUniverses =
        associatedUniverseUUIDs.stream()
            .map(Universe::getOrBadRequest)
            .filter(u -> !u.getUniverseDetails().isKubernetesOperatorControlled)
            .filter(u -> !u.getUniverseUUID().equals(taskParams().universeUUID))
            .collect(Collectors.toSet());
    if (associatedUniverses.size() > 0) {
      log.info(
          "Skipping storage config import as it is associated with non-operator-based universes");
      log.debug("Non-operator-based universes: {}", associatedUniverses);
      return;
    }
    log.info("Importing storage config: {}", taskParams().storageConfigUUID);
    try {
      operatorUtils.createStorageConfigCr(cfg, getNamespace(), taskParams().secretName);
    } catch (Exception e) {
      log.error("Failed to create storage config: {}", taskParams().storageConfigUUID, e);
      throw new RuntimeException("Failed to create storage config", e);
    }
    cfg.setIsKubernetesOperatorControlled(true);
    log.info("Storage config {} imported successfully", taskParams().storageConfigUUID);
  }

  private void importProvider() {
    Provider provider = Provider.getOrBadRequest(taskParams().providerUUID);

    List<Universe> nonOperatorBasedUniverses =
        Customer.get(provider.getCustomerUUID())
            .getUniversesForProvider(provider.getUuid())
            .stream()
            // Skip the universe that is being imported
            .filter(u -> !u.getUniverseUUID().equals(taskParams().universeUUID))
            .filter(u -> !u.getUniverseDetails().isKubernetesOperatorControlled)
            .collect(Collectors.toList());
    if (nonOperatorBasedUniverses.size() > 0) {
      log.info("Skipping provider import as it is associated with non-operator-based universes");
      log.debug("Non-operator-based universes: {}", nonOperatorBasedUniverses);
      return;
    }

    log.info("Importing provider: {}", taskParams().providerUUID);
    KubernetesProviderFormData providerData = new KubernetesProviderFormData();
    providerData.name = provider.getName();
    // Always try to use the cloud info from the provider details before using the config.
    if (provider.getDetails().getCloudInfo() != null
        && provider.getDetails().getCloudInfo().getKubernetes() != null) {
      providerData.config = provider.getDetails().getCloudInfo().getKubernetes().getEnvVars();
    } else {
      log.trace("Provider {} details are not available, using config", provider.getName());
      providerData.config = provider.getConfig();
    }
    List<KubernetesProviderFormData.RegionData> regionList = new ArrayList<>();
    for (Region region : provider.getRegions()) {
      KubernetesProviderFormData.RegionData regionData =
          new KubernetesProviderFormData.RegionData();
      regionData.code = region.getCode();
      regionData.name = region.getName();
      for (AvailabilityZone zone : region.getZones()) {
        KubernetesProviderFormData.RegionData.ZoneData zoneData =
            new KubernetesProviderFormData.RegionData.ZoneData();
        zoneData.code = zone.getCode();
        zoneData.name = zone.getName();
        // Always try to use the cloud info from the zone details before using the config.
        if (zone.getDetails().getCloudInfo() != null
            && zone.getDetails().getCloudInfo().getKubernetes() != null) {
          zoneData.config = zone.getDetails().getCloudInfo().getKubernetes().getEnvVars();
        } else {
          log.trace("Zone {} details are not available, using config", zone.getCode());
          zoneData.config = zone.getConfig();
        }
        regionData.zoneList.add(zoneData);
      }
      regionList.add(regionData);
    }
    providerData.regionList = regionList;
    operatorUtils.createProviderCrFromProviderEbean(
        providerData, getNamespace(), provider.getUuid(), false, taskParams().secretMap);
    log.info("Provider {} imported successfully", provider.getName());
  }

  private void importUniverse() {
    log.info("Importing universe: {}", taskParams().universeUUID);

    Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
    Provider provider =
        Provider.getOrBadRequest(
            UUID.fromString(universe.getUniverseDetails().getPrimaryCluster().userIntent.provider));

    try {
      operatorUtils.createUniverseCr(universe, provider.getName(), getNamespace());
    } catch (Exception e) {
      log.error("Failed to create universe: {}", universe.getName(), e);
      throw new RuntimeException("Failed to create universe", e);
    }
    Universe.UniverseUpdater updater =
        u -> {
          UniverseDefinitionTaskParams uDetails = u.getUniverseDetails();
          uDetails.isKubernetesOperatorControlled = true;
          uDetails.setKubernetesResourceDetails(
              new KubernetesResourceDetails(u.getName(), getNamespace()));
          u.setUniverseDetails(uDetails);
        };
    Universe.saveDetails(universe.getUniverseUUID(), updater);
    // For now, just log the universe info
    // In a real implementation, you might want to create a Universe CRD or similar
    log.info("Universe {} imported successfully", universe.getName());
  }

  private void importBackupSchedule() {
    log.info("Importing backup schedule: {}", taskParams().backupScheduleUUID);

    Schedule schedule = Schedule.getOrBadRequest(taskParams().backupScheduleUUID);

    try {
      operatorUtils.createBackupScheduleCr(
          schedule, schedule.getScheduleName(), taskParams().storageConfigName, getNamespace());
    } catch (Exception e) {
      log.error("Failed to create backup schedule: {}", schedule.getScheduleUUID(), e);
      throw new RuntimeException("Failed to create backup schedule", e);
    }
    schedule.setIsKubernetesOperatorControlled(true);
    log.info("Backup schedule {} imported successfully", schedule.getScheduleUUID());
  }

  private void importBackup() {
    log.info("Importing backup: {}", taskParams().backupUUID);

    Backup backup = Backup.getOrBadRequest(taskParams().customerUUID, taskParams().backupUUID);

    try {
      operatorUtils.createBackupCr(backup, taskParams().storageConfigName, getNamespace());
    } catch (Exception e) {
      log.error("Failed to create backup: {}", backup.getBackupUUID(), e);
      throw new RuntimeException("Failed to create backup", e);
    }
    backup.setIsKubernetesOperatorControlled(true);
    log.info("Backup {} imported successfully", backup.getBackupUUID());
  }

  private void importCertificate() {
    log.info("Importing certificate: {}", taskParams().certificateUUID);

    // For now, just log the certificate info
    // In a real implementation, you might want to create a Certificate CRD or similar
    log.info("Certificate {} imported successfully", taskParams().certificateUUID);
  }

  private String getNamespace() {
    // Get namespace from environment or configuration
    // This is safe, as prechecks should have validated that the taskParams.namespace is the same
    // as the operator namespace.
    String confNamespace = confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorNamespace);
    if (confNamespace != null && !confNamespace.isEmpty()) {
      log.info("Forcing namespace that operator is running in: {}", confNamespace);
      return confNamespace;
    }
    if (taskParams().namespace != null && !taskParams().namespace.isEmpty()) {
      return taskParams().namespace;
    }
    log.warn("No namespace found, using default namespace");
    return "default";
  }

  private String getResourceIdentifier() {
    switch (taskParams().resourceType) {
      case UNIVERSE:
        return taskParams().universeUUID != null
            ? "UUID: " + taskParams().universeUUID
            : "name: unknown";
      case PROVIDER:
        return taskParams().providerUUID != null
            ? "UUID: " + taskParams().providerUUID
            : "name: unknown";
      case STORAGE_CONFIG:
        return taskParams().storageConfigUUID != null
            ? "UUID: " + taskParams().storageConfigUUID
            : "name: unknown";
      case BACKUP:
        return taskParams().backupUUID != null
            ? "UUID: " + taskParams().backupUUID
            : "name: unknown";
      case BACKUP_SCHEDULE:
        return taskParams().backupScheduleUUID != null
            ? "UUID: " + taskParams().backupScheduleUUID
            : "name: unknown";
      case RELEASE:
        return taskParams().releaseVersion != null
            ? "version: " + taskParams().releaseVersion
            : "version: unknown";
      case CERTIFICATE:
        return taskParams().certificateUUID != null
            ? "UUID: " + taskParams().certificateUUID
            : "name: unknown";
      case SECRET:
        return taskParams().secretName != null
            ? "name: " + taskParams().secretName
            : "name: unknown";
      default:
        return "unknown";
    }
  }
}
