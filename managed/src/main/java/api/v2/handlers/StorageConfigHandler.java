// Copyright (c) YugabyteDB, Inc.

package api.v2.handlers;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.CONFLICT;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.mvc.Http.Status.NOT_FOUND;
import static play.mvc.Http.Status.UNPROCESSABLE_ENTITY;

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
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.ImmutableMap;
import com.google.common.util.concurrent.ThreadFactoryBuilder;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigType;
import com.yugabyte.yw.models.helpers.CustomerConfigValidator;
import io.ebean.DuplicateKeyException;
import java.time.Duration;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.function.Function;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import play.libs.Json;

/**
 * v2 storage configs: typed specs over the v1 {@link CustomerConfig} of type STORAGE.
 *
 * <p>Status codes, unlike v1: 400 for an invalid spec, 409 with a code and the existing config's
 * UUID for a name conflict (also when the DB unique constraint catches a concurrent create), 422
 * with a code when the storage check fails or times out, and 404 for a missing config.
 */
@Slf4j
@Singleton
public class StorageConfigHandler {

  public static final String NAME_CONFLICT = "STORAGE_CONFIG_NAME_CONFLICT";
  public static final String VALIDATION_FAILED = "STORAGE_VALIDATION_FAILED";
  public static final String VALIDATION_TIMEOUT = "STORAGE_VALIDATION_TIMEOUT";

  // Field names in the v1 validators' errors (keys of the v1 data, prefixed by "data.") and the
  // v2 property they come from.
  private static final Map<String, String> V2_FIELDS =
      ImmutableMap.<String, String>builder()
          .put("BACKUP_LOCATION", "storage.backup_location")
          .put("REGION_LOCATIONS", "storage.region_locations")
          .put("REGION", "storage.region_locations")
          .put("LOCATION", "storage.region_locations")
          .put("AWS_HOST_BASE", "storage.host_base")
          .put("AWS_ACCESS_KEY_ID", "storage.credentials.access_key_id")
          .put("AWS_SECRET_ACCESS_KEY", "storage.credentials.secret_access_key")
          .put("IAM_INSTANCE_PROFILE", "storage.credentials")
          .put("GCS_CREDENTIALS_JSON", "storage.credentials.credentials_json")
          .put("USE_GCP_IAM", "storage.credentials")
          .put("AZURE_STORAGE_SAS_TOKEN", "storage.credentials.sas_token")
          .put("USE_AZURE_IAM", "storage.credentials")
          .put("AZURE_CLIENT_ID", "storage.credentials.client_id")
          .put("NFS_BUCKET", "storage.bucket")
          .put("OCI_REGION", "storage.region")
          .put("OCI_NAMESPACE", "storage.namespace")
          .put("USE_OCI_IAM", "storage.credentials")
          .put("OCI_S3_ACCESS_KEY_ID", "storage.credentials.access_key_id")
          .put("OCI_S3_SECRET_ACCESS_KEY", "storage.credentials.secret_access_key")
          .put("OCI_S3_HOST_BASE", "storage.credentials.host_base")
          .build();

  private final CustomerConfigValidator configValidator;
  private final RuntimeConfGetter confGetter;
  private final ExecutorService validationExecutor;

  @Inject
  public StorageConfigHandler(
      CustomerConfigValidator configValidator,
      RuntimeConfGetter confGetter,
      PlatformExecutorFactory executorFactory) {
    this.configValidator = configValidator;
    this.confGetter = confGetter;
    // The storage checks block on cloud SDK calls that have no overall deadline of their own, so
    // they run here and the request waits for them only up to the validation timeout.
    this.validationExecutor =
        executorFactory.createFixedExecutor(
            "storage_config_validation",
            8,
            new ThreadFactoryBuilder().setNameFormat("storage-config-validation-%d").build());
  }

  public StorageConfig createStorageConfig(UUID cUUID, StorageConfigSpec spec) {
    CustomerConfig config = validate(cUUID, spec);
    config.generateUUID();
    try {
      config.save();
    } catch (DuplicateKeyException e) {
      // A concurrent request created a config with the same name after the check in validate.
      throw nameConflict(cUUID, config.getConfigName());
    }
    return StorageConfigMapper.INSTANCE.toStorageConfig(config);
  }

  public void validateStorageConfig(UUID cUUID, StorageConfigSpec spec) {
    validate(cUUID, spec);
  }

  public StorageConfig getStorageConfig(UUID cUUID, UUID scUUID) {
    CustomerConfig config = CustomerConfig.get(cUUID, scUUID);
    if (config == null || config.getType() != ConfigType.STORAGE) {
      throw new PlatformServiceException(NOT_FOUND, "No storage config with UUID " + scUUID);
    }
    return StorageConfigMapper.INSTANCE.toStorageConfig(config);
  }

  /** Runs every check of a create and returns the v1 config to save. */
  private CustomerConfig validate(UUID cUUID, StorageConfigSpec spec) {
    checkSpec(spec.getStorage());
    String name = spec.getName().trim();
    if (CustomerConfig.get(cUUID, name) != null) {
      throw nameConflict(cUUID, name);
    }
    CustomerConfig config = StorageConfigMapper.INSTANCE.toV1CustomerConfig(cUUID, spec);
    // Field constraints of the v1 data that the v2 schema doesn't express, such as the maximum
    // length of a backup location: an invalid request, so 400 as thrown.
    configValidator.validateConfigData(config);
    checkStorage(config);
    return config;
  }

  /**
   * Checks what the schema can't: the secrets the credentials need (they aren't required in the
   * schema, because responses leave them out), and region locations.
   */
  private static void checkSpec(StorageSpec storage) {
    if (storage instanceof S3StorageSpec s3) {
      if (s3.getCredentials() instanceof S3AccessKeyCredentials keys) {
        requireSecret(keys.getSecretAccessKey(), "storage.credentials.secret_access_key");
      }
      checkRegions(s3.getRegionLocations(), S3RegionLocation::getRegion);
    } else if (storage instanceof GcsStorageSpec gcs) {
      if (gcs.getCredentials() instanceof GcsServiceAccountKeyCredentials key) {
        requireSecret(key.getCredentialsJson(), "storage.credentials.credentials_json");
      }
      checkRegions(gcs.getRegionLocations(), r -> r.getRegion());
    } else if (storage instanceof AzureStorageSpec azure) {
      checkAzure(azure);
    } else if (storage instanceof NfsStorageSpec nfs) {
      checkRegions(nfs.getRegionLocations(), NfsRegionLocation::getRegion);
    } else if (storage instanceof OciStorageSpec oci) {
      if (oci.getCredentials() instanceof OciS3CompatibleCredentials keys) {
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

  /**
   * Runs the storage validator, which checks the locations and reaches out to the storage, with a
   * deadline. Its failures are answered with 422 and the v2 names of the fields.
   */
  private void checkStorage(CustomerConfig config) {
    Duration timeout = confGetter.getGlobalConf(GlobalConfKeys.storageConfigValidationTimeout);
    Future<?> check =
        validationExecutor.submit(() -> configValidator.validateConfigDataWithValidator(config));
    try {
      check.get(timeout.toMillis(), TimeUnit.MILLISECONDS);
    } catch (TimeoutException e) {
      // The SDK call can't be interrupted reliably; the thread finishes it on its own.
      check.cancel(true);
      ObjectNode error = Json.newObject();
      error.put("code", VALIDATION_TIMEOUT);
      error.put(
          "message",
          String.format(
              "YBA could not check the storage within %s (%s)",
              timeout, GlobalConfKeys.storageConfigValidationTimeout.getKey()));
      throw new PlatformServiceException(UNPROCESSABLE_ENTITY, error);
    } catch (InterruptedException e) {
      check.cancel(true);
      Thread.currentThread().interrupt();
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "Interrupted checking storage");
    } catch (ExecutionException e) {
      if (e.getCause() instanceof PlatformServiceException pse) {
        throw toValidationFailure(pse);
      }
      log.error("Storage config validation of {} failed", config.getConfigName(), e.getCause());
      ObjectNode error = Json.newObject();
      error.put("code", VALIDATION_FAILED);
      error.put("message", String.valueOf(e.getCause().getMessage()));
      throw new PlatformServiceException(UNPROCESSABLE_ENTITY, error);
    }
  }

  /**
   * Turns a v1 validator error, {"data.<V1_FIELD>": [messages], "errorSource": [...]} or a plain
   * message, into a 422 with error.errors keyed by v2 field names.
   */
  @VisibleForTesting
  static PlatformServiceException toValidationFailure(PlatformServiceException e) {
    ObjectNode error = Json.newObject();
    error.put("code", VALIDATION_FAILED);
    ObjectNode errors = error.putObject("errors");
    String firstMessage = null;
    JsonNode v1Errors = e.getContentJson().get("error");
    if (v1Errors != null && v1Errors.isObject()) {
      Iterator<Map.Entry<String, JsonNode>> fields = v1Errors.fields();
      while (fields.hasNext()) {
        Map.Entry<String, JsonNode> field = fields.next();
        if (field.getKey().equals("errorSource")) {
          continue;
        }
        String v2Field = toV2Field(field.getKey());
        ArrayNode messages =
            errors.has(v2Field) ? (ArrayNode) errors.get(v2Field) : errors.putArray(v2Field);
        List<JsonNode> fieldMessages = new ArrayList<>();
        if (field.getValue().isArray()) {
          field.getValue().forEach(fieldMessages::add);
        } else {
          fieldMessages.add(field.getValue());
        }
        for (JsonNode message : fieldMessages) {
          messages.add(message.asText());
          if (firstMessage == null) {
            firstMessage = (v2Field.isEmpty() ? "" : v2Field + ": ") + message.asText();
          }
        }
      }
    } else if (v1Errors != null) {
      firstMessage = v1Errors.asText();
      errors.putArray("storage").add(firstMessage);
    }
    error.put("message", firstMessage != null ? firstMessage : e.getMessage());
    return new PlatformServiceException(UNPROCESSABLE_ENTITY, error);
  }

  private static String toV2Field(String v1Field) {
    if (v1Field.isEmpty()) {
      return "";
    }
    String key = StringUtils.removeStart(v1Field, "data.");
    return V2_FIELDS.getOrDefault(key, "storage");
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
