// Copyright (c) YugabyteDB, Inc.

package api.v2.handlers;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.CONFLICT;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.mvc.Http.Status.NOT_FOUND;
import static play.mvc.Http.Status.SERVICE_UNAVAILABLE;
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
import api.v2.models.StorageConfigApiFilter;
import api.v2.models.StorageConfigPagedQuerySpec;
import api.v2.models.StorageConfigPagedResp;
import api.v2.models.StorageConfigSpec;
import api.v2.models.StorageConfigUsage;
import api.v2.models.StorageSpec;
import api.v2.utils.NormalizedPaginationSpec;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.util.concurrent.ThreadFactoryBuilder;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigType;
import com.yugabyte.yw.models.helpers.CustomerConfigValidator;
import io.ebean.DuplicateKeyException;
import io.ebean.ExpressionList;
import io.ebean.PagedList;
import java.time.Duration;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Future;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.function.Function;
import java.util.stream.Collectors;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.apache.pekko.Done;
import org.apache.pekko.actor.CoordinatedShutdown;
import play.libs.Json;

/**
 * v2 storage configs: typed specs over the v1 {@link CustomerConfig} of type STORAGE.
 *
 * <p>Status codes, unlike v1: 400 for an invalid spec, 409 with a code and the existing config's
 * UUID for a name conflict (also when the DB unique constraint catches a concurrent create), 422
 * with a code when the storage check fails or times out, 503 when the provider is unavailable or
 * every storage check thread is busy, and 404 for a missing config.
 */
@Slf4j
@Singleton
public class StorageConfigHandler {

  public static final String NAME_CONFLICT = "STORAGE_CONFIG_NAME_CONFLICT";
  public static final String CONFIG_INVALID = StorageValidationErrors.CONFIG_INVALID;
  public static final String VALIDATION_FAILED = StorageValidationErrors.VALIDATION_FAILED;
  public static final String PROVIDER_UNAVAILABLE = StorageValidationErrors.PROVIDER_UNAVAILABLE;
  public static final String VALIDATION_TIMEOUT = "STORAGE_VALIDATION_TIMEOUT";
  public static final String VALIDATION_BUSY = "STORAGE_VALIDATION_BUSY";

  private static final String POOL_NAME = "storage_config_validation";

  // Matches no config, for an in_use filter that nothing satisfies.
  private static final UUID NO_CONFIG = new UUID(0, 0);

  private final CustomerConfigValidator configValidator;
  private final RuntimeConfGetter confGetter;
  private final ExecutorService validationExecutor;

  @Inject
  public StorageConfigHandler(
      CustomerConfigValidator configValidator,
      RuntimeConfGetter confGetter,
      PlatformExecutorFactory executorFactory,
      CoordinatedShutdown coordinatedShutdown) {
    this.configValidator = configValidator;
    this.confGetter = confGetter;
    // The storage checks block on cloud SDK calls that have no overall deadline of their own, so
    // they run here and the request waits for them only up to the validation timeout. The pool has
    // no queue (yb.storage_config_validation): a check that timed out keeps its thread until the
    // SDK call returns, and queued checks would time out behind it without reaching their storage.
    this.validationExecutor =
        executorFactory.createNonBlockingExecutor(
            POOL_NAME,
            new ThreadFactoryBuilder()
                .setNameFormat("storage-config-validation-%d")
                .setDaemon(true)
                .build());
    coordinatedShutdown.addTask(
        CoordinatedShutdown.PhaseServiceStop(),
        "storage-config-validation-shutdown",
        () -> {
          validationExecutor.shutdown();
          return CompletableFuture.completedFuture(Done.done());
        });
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
    StorageConfig storageConfig = StorageConfigMapper.INSTANCE.toStorageConfig(config);
    // Unused, but reported as getStorageConfig reports it.
    storageConfig
        .getInfo()
        .usage(
            StorageConfigUsages.of(cUUID, List.of(config.getConfigUUID()))
                .get(config.getConfigUUID()));
    return storageConfig;
  }

  public void validateStorageConfig(UUID cUUID, StorageConfigSpec spec) {
    validate(cUUID, spec);
  }

  public StorageConfig getStorageConfig(UUID cUUID, UUID scUUID) {
    CustomerConfig config = CustomerConfig.get(cUUID, scUUID);
    if (config == null || config.getType() != ConfigType.STORAGE) {
      throw new PlatformServiceException(NOT_FOUND, "No storage config with UUID " + scUUID);
    }
    StorageConfig storageConfig = StorageConfigMapper.INSTANCE.toStorageConfig(config);
    storageConfig.getInfo().usage(StorageConfigUsages.of(cUUID, List.of(scUUID)).get(scUUID));
    return storageConfig;
  }

  public StorageConfigPagedResp pageListStorageConfigs(
      UUID cUUID, StorageConfigPagedQuerySpec query) {
    NormalizedPaginationSpec paging = HandlerPagingSupport.normalize(query);
    ExpressionList<CustomerConfig> where =
        CustomerConfig.find
            .query()
            .where()
            .eq("customer_uuid", cUUID)
            .eq("type", ConfigType.STORAGE);
    StorageConfigApiFilter filter = query.getFilter();
    if (filter != null) {
      if (filter.getStorageTypes() != null && !filter.getStorageTypes().isEmpty()) {
        where.in(
            "name",
            filter.getStorageTypes().stream()
                .map(StorageConfigMapper::toV1Subtype)
                .collect(Collectors.toList()));
      }
      if (filter.getName() != null) {
        where.eq("config_name", filter.getName().trim());
      }
      if (filter.getStates() != null && !filter.getStates().isEmpty()) {
        where.in(
            "state",
            filter.getStates().stream()
                .map(StorageConfigMapper::toV1State)
                .collect(Collectors.toList()));
      }
      if (filter.getInUse() != null) {
        // Configs are few per customer, so the in_use filter runs on the candidates the other
        // filters leave (see StorageConfigUsages for how each kind of use is looked up).
        List<UUID> candidates =
            where.query().copy().select("configUUID").findIds().stream()
                .map(UUID.class::cast)
                .collect(Collectors.toList());
        Set<UUID> inUse = StorageConfigUsages.inUse(cUUID, candidates);
        if (filter.getInUse()) {
          where.in("configUUID", inUse.isEmpty() ? Set.of(NO_CONFIG) : inUse);
        } else if (!inUse.isEmpty()) {
          where.notIn("configUUID", inUse);
        }
      }
    }
    PagedList<CustomerConfig> page =
        HandlerPagingSupport.getPagedList(
            where,
            paging,
            String.format("lower(config_name) %s, config_uuid %s", paging.order(), paging.order()));
    Map<UUID, StorageConfigUsage> usages =
        StorageConfigUsages.of(
            cUUID,
            page.getList().stream()
                .map(CustomerConfig::getConfigUUID)
                .collect(Collectors.toList()));
    return HandlerPagingSupport.pagedResponse(
        new StorageConfigPagedResp(),
        page,
        config -> {
          StorageConfig storageConfig = StorageConfigMapper.INSTANCE.toStorageConfig(config);
          storageConfig.getInfo().usage(usages.get(config.getConfigUUID()));
          return storageConfig;
        });
  }

  /** Runs every check of a create and returns the v1 config to save. */
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
    checkStorage(config);
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

  /**
   * Runs the storage validator, which checks the locations and reaches out to the storage, with a
   * deadline. See {@link StorageValidationErrors} for how its failures are answered.
   */
  private void checkStorage(CustomerConfig config) {
    Duration timeout = confGetter.getGlobalConf(GlobalConfKeys.storageConfigValidationTimeout);
    Future<?> check;
    try {
      check =
          validationExecutor.submit(() -> configValidator.validateConfigDataWithValidator(config));
    } catch (RejectedExecutionException e) {
      ObjectNode error = Json.newObject();
      error.put("code", VALIDATION_BUSY);
      error.put(
          "message",
          String.format("All storage checks are busy (yb.%s.max_threads), retry later", POOL_NAME));
      throw new PlatformServiceException(SERVICE_UNAVAILABLE, error);
    }
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
      throw StorageValidationErrors.toV2Error(e.getCause());
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
