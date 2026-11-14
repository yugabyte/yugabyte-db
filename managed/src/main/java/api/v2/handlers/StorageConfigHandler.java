// Copyright (c) YugabyteDB, Inc.

package api.v2.handlers;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.CONFLICT;
import static play.mvc.Http.Status.NOT_FOUND;

import api.v2.mappers.StorageConfigMapper;
import api.v2.models.AzureCredentials;
import api.v2.models.AzureRegionLocation;
import api.v2.models.AzureSasTokenCredentials;
import api.v2.models.AzureStorageSpec;
import api.v2.models.GcsServiceAccountKeyCredentials;
import api.v2.models.GcsStorageSpec;
import api.v2.models.NfsRegionLocation;
import api.v2.models.NfsStorageSpec;
import api.v2.models.OciIamCredentials;
import api.v2.models.OciRegionLocation;
import api.v2.models.OciS3CompatibleCredentials;
import api.v2.models.OciStorageSpec;
import api.v2.models.S3AccessKeyCredentials;
import api.v2.models.S3RegionLocation;
import api.v2.models.S3StorageSpec;
import api.v2.models.StorageConfig;
import api.v2.models.StorageConfigSpec;
import api.v2.models.StorageSpec;
import api.v2.models.YBATask;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.ValidateStorageConfig;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.customer.config.UnsavedStorageConfigs;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigState;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigType;
import com.yugabyte.yw.models.helpers.CustomerConfigValidator;
import com.yugabyte.yw.models.helpers.TaskType;
import io.ebean.DuplicateKeyException;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.function.Function;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import play.libs.Json;

/**
 * v2 storage configs: typed specs over the v1 {@link CustomerConfig} of type STORAGE.
 *
 * <p>Create and validate run on the request only the checks that need no storage, and return a
 * {@link ValidateStorageConfig} task that runs the storage validators, which block on cloud SDK
 * calls. Create saves the config in state Validating, which the task makes Active, or deletes the
 * config when the check fails. Validate saves nothing: the task gets the config in memory, through
 * {@link UnsavedStorageConfigs}.
 *
 * <p>Status codes, unlike v1: 400 for an invalid spec, 409 with a code and the existing config's
 * UUID for a name conflict (also when the DB unique constraint catches a concurrent create), and
 * 404 for a missing config. A failed storage check fails the task, with the error code in its
 * message.
 */
@Slf4j
@Singleton
public class StorageConfigHandler {

  public static final String NAME_CONFLICT = "STORAGE_CONFIG_NAME_CONFLICT";
  public static final String CONFIG_INVALID = StorageValidationErrors.CONFIG_INVALID;
  public static final String VALIDATION_FAILED = StorageValidationErrors.VALIDATION_FAILED;
  public static final String PROVIDER_UNAVAILABLE = StorageValidationErrors.PROVIDER_UNAVAILABLE;

  private final CustomerConfigValidator configValidator;
  private final Commissioner commissioner;
  private final UnsavedStorageConfigs unsavedConfigs;

  @Inject
  public StorageConfigHandler(
      CustomerConfigValidator configValidator,
      Commissioner commissioner,
      UnsavedStorageConfigs unsavedConfigs) {
    this.configValidator = configValidator;
    this.commissioner = commissioner;
    this.unsavedConfigs = unsavedConfigs;
  }

  public YBATask createStorageConfig(UUID cUUID, StorageConfigSpec spec) {
    Customer customer = Customer.getOrNotFound(cUUID);
    CustomerConfig config = validate(cUUID, spec);
    config.generateUUID();
    config.setState(ConfigState.Validating);
    try {
      config.save();
    } catch (DuplicateKeyException e) {
      // A concurrent request created a config with the same name after the check in validate.
      throw nameConflict(cUUID, config.getConfigName());
    }
    ValidateStorageConfig.Params params = new ValidateStorageConfig.Params();
    params.customerUUID = cUUID;
    params.configUUID = config.getConfigUUID();
    UUID taskUUID;
    try {
      taskUUID = commissioner.submit(TaskType.ValidateStorageConfig, params);
    } catch (RuntimeException e) {
      // No task will make it Active.
      config.delete();
      throw e;
    }
    log.info(
        "Submitted task {} to check the storage of config {}", taskUUID, config.getConfigUUID());
    CustomerTask.create(
        customer,
        config.getConfigUUID(),
        taskUUID,
        CustomerTask.TargetType.CustomerConfiguration,
        CustomerTask.TaskType.Create,
        config.getConfigName());
    return new YBATask().taskUuid(taskUUID).resourceUuid(config.getConfigUUID());
  }

  public YBATask validateStorageConfig(UUID cUUID, StorageConfigSpec spec) {
    Customer customer = Customer.getOrNotFound(cUUID);
    CustomerConfig config = validate(cUUID, spec);
    // Kept in memory for the task: the task params are stored unencrypted, the secrets aren't.
    UUID key = unsavedConfigs.put(config);
    ValidateStorageConfig.Params params = new ValidateStorageConfig.Params();
    params.customerUUID = cUUID;
    params.configUUID = key;
    params.dryRun = true;
    UUID taskUUID;
    try {
      taskUUID = commissioner.submit(TaskType.ValidateStorageConfig, params);
    } catch (RuntimeException e) {
      unsavedConfigs.remove(key);
      throw e;
    }
    CustomerTask.create(
        customer,
        key,
        taskUUID,
        CustomerTask.TargetType.CustomerConfiguration,
        CustomerTask.TaskType.Validate,
        config.getConfigName());
    return new YBATask().taskUuid(taskUUID);
  }

  public StorageConfig getStorageConfig(UUID cUUID, UUID scUUID) {
    CustomerConfig config = CustomerConfig.get(cUUID, scUUID);
    if (config == null || config.getType() != ConfigType.STORAGE) {
      throw new PlatformServiceException(NOT_FOUND, "No storage config with UUID " + scUUID);
    }
    return StorageConfigMapper.INSTANCE.toStorageConfig(config);
  }

  /**
   * Runs the checks of a create that need no storage and returns the v1 config to save. The storage
   * validators run in the task.
   */
  private CustomerConfig validate(UUID cUUID, StorageConfigSpec spec) {
    checkSpec(spec.getStorage());
    // Stored trimmed, as v1 does; the schema's minLength applies before trimming.
    String name = spec.getName().trim();
    if (name.isEmpty()) {
      throw badRequest("name must not be blank");
    }
    if (CustomerConfig.get(cUUID, name) != null) {
      throw nameConflict(cUUID, name);
    }
    CustomerConfig config = StorageConfigMapper.INSTANCE.toV1CustomerConfig(cUUID, spec);
    // Field constraints of the v1 data that the v2 schema doesn't express, such as the maximum
    // length of a backup location: 400.
    try {
      configValidator.validateConfigData(config);
    } catch (PlatformServiceException e) {
      throw StorageValidationErrors.toV2Error(e);
    }
    return config;
  }

  /**
   * Checks what the schema can't: the secrets the credentials need (they aren't required in the
   * schema, because responses leave them out), and region locations.
   */
  private static void checkSpec(StorageSpec storage) {
    if (storage instanceof S3StorageSpec) {
      S3StorageSpec s3 = (S3StorageSpec) storage;
      if (s3.getCredentials() instanceof S3AccessKeyCredentials) {
        S3AccessKeyCredentials keys = (S3AccessKeyCredentials) s3.getCredentials();
        requireSecret(keys.getAccessKeyId(), "storage.credentials.access_key_id");
        requireSecret(keys.getSecretAccessKey(), "storage.credentials.secret_access_key");
      }
      checkRegions(s3.getRegionLocations(), S3RegionLocation::getRegion);
    } else if (storage instanceof GcsStorageSpec) {
      GcsStorageSpec gcs = (GcsStorageSpec) storage;
      if (gcs.getCredentials() instanceof GcsServiceAccountKeyCredentials) {
        GcsServiceAccountKeyCredentials key =
            (GcsServiceAccountKeyCredentials) gcs.getCredentials();
        requireSecret(key.getCredentialsJson(), "storage.credentials.credentials_json");
      }
      checkRegions(gcs.getRegionLocations(), r -> r.getRegion());
    } else if (storage instanceof AzureStorageSpec) {
      AzureStorageSpec azure = (AzureStorageSpec) storage;
      checkAzure(azure);
    } else if (storage instanceof NfsStorageSpec) {
      NfsStorageSpec nfs = (NfsStorageSpec) storage;
      checkRegions(nfs.getRegionLocations(), NfsRegionLocation::getRegion);
    } else if (storage instanceof OciStorageSpec) {
      OciStorageSpec oci = (OciStorageSpec) storage;
      if (oci.getCredentials() instanceof OciS3CompatibleCredentials) {
        OciS3CompatibleCredentials keys = (OciS3CompatibleCredentials) oci.getCredentials();
        requireSecret(keys.getAccessKeyId(), "storage.credentials.access_key_id");
        requireSecret(keys.getSecretAccessKey(), "storage.credentials.secret_access_key");
      } else if (oci.getCredentials() instanceof OciIamCredentials
          && StringUtils.isBlank(oci.getNamespace())) {
        throw badRequest("storage.namespace is required with IAM credentials");
      }
      checkRegions(oci.getRegionLocations(), OciRegionLocation::getRegion);
    }
  }

  private static void checkAzure(AzureStorageSpec azure) {
    AzureCredentials credentials = azure.getCredentials();
    boolean sas = credentials instanceof AzureSasTokenCredentials;
    if (sas) {
      requireSecret(
          ((AzureSasTokenCredentials) credentials).getSasToken(), "storage.credentials.sas_token");
    }
    List<AzureRegionLocation> regions = azure.getRegionLocations();
    checkRegions(regions, AzureRegionLocation::getRegion);
    if (regions != null) {
      for (AzureRegionLocation region : regions) {
        if (sas && StringUtils.isEmpty(region.getSasToken())) {
          throw badRequest(
              "storage.region_locations.sas_token of region "
                  + region.getRegion()
                  + " is required with SAS_TOKEN credentials");
        }
        if (!sas && region.getSasToken() != null) {
          throw badRequest(
              "storage.region_locations.sas_token applies only to SAS_TOKEN credentials");
        }
      }
    }
  }

  private static void requireSecret(String secret, String field) {
    if (StringUtils.isEmpty(secret)) {
      throw badRequest(field + " is required");
    }
  }

  private static <T> void checkRegions(List<T> regions, Function<T, String> region) {
    if (regions == null) {
      return;
    }
    Set<String> seen = new HashSet<>();
    for (T location : regions) {
      if (!seen.add(region.apply(location))) {
        throw badRequest(
            "storage.region_locations has region " + region.apply(location) + " more than once");
      }
    }
  }

  private PlatformServiceException nameConflict(UUID cUUID, String name) {
    ObjectNode error = Json.newObject();
    error.put("code", NAME_CONFLICT);
    error.put("message", String.format("A configuration named %s already exists", name));
    CustomerConfig existing = CustomerConfig.get(cUUID, name);
    // The name is unique among all the customer's configs, not only storage ones.
    if (existing != null && existing.getType() == ConfigType.STORAGE) {
      error.put("storage_config_uuid", existing.getConfigUUID().toString());
      error.put(
          "storage_config_state",
          StorageConfigMapper.INSTANCE.toStorageConfigInfo(existing).getState().toString());
    }
    return new PlatformServiceException(CONFLICT, error);
  }

  private static PlatformServiceException badRequest(String message) {
    return new PlatformServiceException(BAD_REQUEST, message);
  }
}
