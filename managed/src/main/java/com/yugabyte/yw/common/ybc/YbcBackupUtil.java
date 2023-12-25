// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.ybc;

import play.libs.Json;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.PRECONDITION_FAILED;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static java.util.stream.Collectors.joining;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.fasterxml.jackson.annotation.JsonEnumDefaultValue;
import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonSubTypes;
import com.fasterxml.jackson.annotation.JsonTypeInfo;
import com.fasterxml.jackson.annotation.JsonTypeInfo.As;
import com.fasterxml.jackson.annotation.JsonTypeInfo.Id;
import com.fasterxml.jackson.databind.DeserializationFeature;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.net.HostAndPort;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.BackupUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.StorageUtil;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.BackupUtil.RegionLocations;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.services.YbcClientService;
import com.yugabyte.yw.common.ybc.YbcBackupUtil.YbcBackupResponse.ResponseCloudStoreSpec.BucketLocation;
import com.yugabyte.yw.common.ybc.YbcBackupUtil.YbcBackupResponse.SnapshotObjectDetails.TableData;
import com.yugabyte.yw.controllers.handlers.UniverseInfoHandler;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.forms.RestoreBackupParams.BackupStorageInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageNFSData;

import io.ebean.annotation.EnumValue;

import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.function.BiFunction;
import java.util.function.Function;
import java.util.stream.Collectors;
import javax.validation.ConstraintViolation;
import javax.validation.Valid;
import javax.validation.Validation;
import javax.validation.Validator;
import javax.validation.constraints.NotNull;
import javax.validation.constraints.Size;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.collections4.MapUtils;
import org.apache.commons.lang3.StringUtils;
import org.apache.commons.lang3.tuple.ImmutablePair;
import org.apache.commons.lang3.tuple.Pair;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.CommonTypes.TableType;
import org.yb.CommonTypes.YQLDatabase;
import org.yb.client.YbcClient;
import org.yb.ybc.BackupServiceTaskCreateRequest;
import org.yb.ybc.BackupServiceTaskProgressRequest;
import org.yb.ybc.BackupServiceTaskResultRequest;
import org.yb.ybc.CloudStoreConfig;
import org.yb.ybc.CloudStoreSpec;
import org.yb.ybc.TableBackup;
import org.yb.ybc.TableBackupSpec;
import org.yb.ybc.UserChangeSpec;
import org.yb.ybc.BackupServiceTaskExtendedArgs;
import org.yb.ybc.NamespaceType;
import org.yb.ybc.CloudType;

@Singleton
@Slf4j
public class YbcBackupUtil {

  // Time to wait (in millisec) between each poll to ybc.
  public static final int WAIT_EACH_ATTEMPT_MS = 15000;
  public static final int MAX_TASK_RETRIES = 10;
  public static final String DEFAULT_REGION_STRING = "default_region";
  public static final String YBC_SUCCESS_MARKER_TASK_SUFFIX = "_success_marker";

  @Inject UniverseInfoHandler universeInfoHandler;
  @Inject YbcClientService ybcService;
  @Inject BackupUtil backupUtil;
  @Inject CustomerConfigService configService;
  @Inject EncryptionAtRestManager encryptionAtRestManager;

  public static final Logger LOG = LoggerFactory.getLogger(BackupUtil.class);

  public enum SnapshotObjectType {
    @EnumValue("NAMESPACE")
    NAMESPACE,
    @EnumValue("TABLE")
    TABLE,
    @JsonEnumDefaultValue
    @EnumValue("default_type")
    DEFAULT_TYPE;

    public static class Constants {
      public static final String NAMESPACE = "NAMESPACE";
      public static final String TABLE = "TABLE";
    }
  }

  @JsonIgnoreProperties(ignoreUnknown = true)
  public static class YbcBackupResponse {
    @JsonAlias("backup_size")
    @NotNull
    public String backupSize;

    @JsonAlias("backup_config_data")
    public String extendedArgsString;

    @JsonAlias("cloud_store_spec")
    @NotNull
    @Valid
    public ResponseCloudStoreSpec responseCloudStoreSpec;

    @JsonAlias("snapshot_details")
    @NotNull
    @Valid
    public List<SnapshotObjectDetails> snapshotObjectDetails;

    @JsonIgnoreProperties(ignoreUnknown = true)
    public static class ResponseCloudStoreSpec {
      @JsonAlias("default_backup_location")
      @NotNull
      @Valid
      public BucketLocation defaultLocation;

      @JsonAlias("region_location_map")
      @Valid
      public Map<String, BucketLocation> regionLocations;

      @JsonIgnoreProperties(ignoreUnknown = true)
      public static class BucketLocation {
        @JsonAlias("bucket")
        @Size(min = 1)
        @NotNull
        public String bucket;

        @JsonAlias("cloud_dir")
        @Size(min = 1)
        @NotNull
        public String cloudDir;

        @JsonAlias("prev_cloud_dir")
        public String prevCloudDir;
      }
    }

    @JsonIgnoreProperties(ignoreUnknown = true)
    public static class SnapshotObjectDetails {
      @NotNull public SnapshotObjectType type;

      @NotNull
      @Valid
      @JsonTypeInfo(
          use = Id.NAME,
          property = "type",
          include = As.EXTERNAL_PROPERTY,
          defaultImpl = SnapshotObjectData.class)
      @JsonSubTypes(
          value = {
            @JsonSubTypes.Type(value = TableData.class, name = SnapshotObjectType.Constants.TABLE),
            @JsonSubTypes.Type(
                value = NamespaceData.class,
                name = SnapshotObjectType.Constants.NAMESPACE)
          })
      public SnapshotObjectData data;

      @JsonIgnoreProperties(ignoreUnknown = true)
      public static class SnapshotObjectData {
        @JsonAlias("name")
        @NotNull
        public String snapshotObjectName;
      }

      @JsonIgnoreProperties(ignoreUnknown = true)
      public static class TableData extends SnapshotObjectData {
        @JsonAlias("table_type")
        @NotNull
        public TableType snapshotNamespaceType;

        @JsonAlias("namespace_name")
        public String snapshotNamespaceName;
      }

      @JsonIgnoreProperties(ignoreUnknown = true)
      public static class NamespaceData extends SnapshotObjectData {
        @JsonAlias("database_type")
        @NotNull
        public YQLDatabase snapshotDatabaseType;
      }
    }
  }

  /**
   * Parse metadata object from YB-Controller backup result
   *
   * @param metadata
   * @return YbcBackupResponse object
   */
  public YbcBackupResponse parseYbcBackupResponse(String metadata) {
    ObjectMapper mapper = new ObjectMapper();
    // For custom types in Snapshot Info.
    mapper.enable(DeserializationFeature.READ_UNKNOWN_ENUM_VALUES_USING_DEFAULT_VALUE);
    YbcBackupResponse successMarker = null;
    try {
      successMarker = mapper.readValue(metadata, YbcBackupResponse.class);
      Validator validator = Validation.buildDefaultValidatorFactory().getValidator();
      Map<String, String> validationResponse =
          validator
              .validate(successMarker)
              .stream()
              .collect(
                  Collectors.groupingBy(
                      e -> e.getPropertyPath().toString(),
                      Collectors.mapping(ConstraintViolation::getMessage, joining())));
      if (MapUtils.isEmpty(validationResponse)) {
        return successMarker;
      } else {
        JsonNode errJson = Json.toJson(validationResponse);
        throw new PlatformServiceException(PRECONDITION_FAILED, errJson);
      }
    } catch (IOException e) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          String.format("Error parsing success marker string. %s", e.getMessage()));
    } catch (Exception e) {
      throw new PlatformServiceException(PRECONDITION_FAILED, e.getMessage());
    }
  }

  public JsonNode getUniverseKeysJsonFromSuccessMarker(String extendedArgs) {
    ObjectMapper mapper = new ObjectMapper();
    JsonNode args = null;
    try {
      args = mapper.readTree(extendedArgs);
      return args.get("universe_keys");
    } catch (Exception e) {
      log.error("Could not fetch universe keys from success marker");
      return null;
    }
  }

  /**
   * Extract region locations from YB-Controller's region spec map, and format it.
   *
   * @param regionMap
   * @param tableParams
   * @return List of region-locations for multi-region backups.
   */
  public List<BackupUtil.RegionLocations> extractRegionLocationFromMetadata(
      Map<String, BucketLocation> regionMap, BackupTableParams tableParams) {
    CustomerConfig config =
        configService.getOrBadRequest(tableParams.customerUuid, tableParams.storageConfigUUID);
    CustomerConfigStorageData configData = (CustomerConfigStorageData) config.getDataObject();
    Map<String, String> regionLocationMap =
        StorageUtil.getStorageUtil(config.name).getRegionLocationsMap(configData);
    List<BackupUtil.RegionLocations> regionLocations = new ArrayList<>();
    regionMap.forEach(
        (r, bL) -> {
          BackupUtil.RegionLocations rL = new BackupUtil.RegionLocations();
          rL.REGION = r;
          rL.LOCATION =
              BackupUtil.getExactRegionLocation(
                  tableParams.storageLocation,
                  regionLocationMap.get(r),
                  config.name.equals("NFS")
                      ? ((CustomerConfigStorageNFSData) configData).nfsBucket
                      : "");
          regionLocations.add(rL);
        });
    return regionLocations;
  }

  public BackupServiceTaskProgressRequest createYbcBackupTaskProgressRequest(String taskID) {
    BackupServiceTaskProgressRequest backupProgressRequest =
        BackupServiceTaskProgressRequest.newBuilder().setTaskId(taskID).build();
    return backupProgressRequest;
  }

  public BackupServiceTaskResultRequest createYbcBackupResultRequest(String taskID) {
    BackupServiceTaskResultRequest backupResultRequest =
        BackupServiceTaskResultRequest.newBuilder().setTaskId(taskID).build();
    return backupResultRequest;
  }

  public String getYbcTaskID(UUID uuid, String backupType, String keyspace) {
    return getYbcTaskID(uuid, backupType, keyspace, null);
  }

  public String getYbcTaskID(UUID uuid, String backupType, String keyspace, UUID paramsIdentifier) {
    if (paramsIdentifier != null) {
      return String.format(
          "%s_%s_%s_%s", uuid.toString(), backupType, keyspace, paramsIdentifier.toString());
    } else {
      return String.format("%s_%s_%s", uuid.toString(), backupType, keyspace);
    }
  }

  /**
   * Creates backup task request compatible with YB-Controller
   *
   * @param backupTableParams
   * @return BackupServiceTaskCreateRequest object
   */
  public BackupServiceTaskCreateRequest createYbcBackupRequest(
      BackupTableParams backupTableParams) {
    return createYbcBackupRequest(backupTableParams, null);
  }

  /**
   * Creates backup task request compatible with YB-Controller
   *
   * @param backupTableParams This backup's params.
   * @param previousTableParam Previous backup's params for incremental backup.
   * @return BackupServiceTaskCreateRequest object
   */
  public BackupServiceTaskCreateRequest createYbcBackupRequest(
      BackupTableParams backupTableParams, BackupTableParams previousTableParams) {
    CustomerConfig config =
        configService.getOrBadRequest(
            backupTableParams.customerUuid, backupTableParams.storageConfigUUID);
    String taskID =
        getYbcTaskID(
            backupTableParams.backupUuid,
            backupTableParams.backupType.name(),
            backupTableParams.getKeyspace(),
            backupTableParams.backupParamsIdentifier);

    NamespaceType namespaceType = getNamespaceType(backupTableParams.backupType);
    String specificCloudDir =
        BackupUtil.getBackupIdentifier(backupTableParams.storageLocation, true);

    // For previous backup location( default + regional)
    Map<String, String> keyspacePreviousLocationsMap =
        backupUtil.getLocationMap(previousTableParams);
    CloudStoreConfig cloudStoreConfig =
        createBackupConfig(config, specificCloudDir, keyspacePreviousLocationsMap);
    BackupServiceTaskExtendedArgs extendedArgs = getExtendedArgsForBackup(backupTableParams);

    BackupServiceTaskCreateRequest.Builder backupServiceTaskCreateRequestBuilder =
        backupServiceTaskCreateBuilder(taskID, namespaceType, extendedArgs);
    backupServiceTaskCreateRequestBuilder.setCsConfig(cloudStoreConfig);
    if (CollectionUtils.isNotEmpty(backupTableParams.tableNameList)) {
      TableBackupSpec tableBackupSpec = getTableBackupSpec(backupTableParams);
      backupServiceTaskCreateRequestBuilder.setTbs(tableBackupSpec);
    } else {
      backupServiceTaskCreateRequestBuilder.setNs(backupTableParams.getKeyspace());
    }
    BackupServiceTaskCreateRequest backupServiceTaskCreateRequest =
        backupServiceTaskCreateRequestBuilder.build();
    return backupServiceTaskCreateRequest;
  }

  public BackupServiceTaskCreateRequest createYbcRestoreRequest(
      UUID customerUUID,
      UUID storageConfigUUID,
      BackupStorageInfo backupStorageInfo,
      String taskId,
      YbcBackupResponse successMarker) {
    NamespaceType namespaceType = getNamespaceType(backupStorageInfo.backupType);
    String keyspace = backupStorageInfo.keyspace;
    BackupServiceTaskExtendedArgs.Builder extendedArgs = BackupServiceTaskExtendedArgs.newBuilder();
    if (StringUtils.isNotBlank(backupStorageInfo.newOwner)) {
      extendedArgs.setUserSpec(
          UserChangeSpec.newBuilder()
              .setNewUsername(backupStorageInfo.newOwner)
              .setOldUsername(backupStorageInfo.oldOwner)
              .build());
    }
    BackupServiceTaskCreateRequest.Builder backupServiceTaskCreateRequestBuilder =
        backupServiceTaskCreateBuilder(taskId, namespaceType, extendedArgs.build());
    CustomerConfig config = configService.getOrBadRequest(customerUUID, storageConfigUUID);
    CloudStoreConfig cloudStoreConfig = createRestoreConfig(config, successMarker);
    backupServiceTaskCreateRequestBuilder.setNs(keyspace).setCsConfig(cloudStoreConfig);
    return backupServiceTaskCreateRequestBuilder.build();
  }

  public BackupServiceTaskCreateRequest createDsmRequest(
      UUID customerUUID, UUID storageConfigUUID, String taskId, BackupStorageInfo storageInfo) {
    BackupServiceTaskExtendedArgs extendedArgs = BackupServiceTaskExtendedArgs.newBuilder().build();
    BackupServiceTaskCreateRequest.Builder backupServiceTaskCreateRequestBuilder =
        backupServiceTaskCreateBuilder(
            taskId, getNamespaceType(storageInfo.backupType), extendedArgs);
    CustomerConfig config = configService.getOrBadRequest(customerUUID, storageConfigUUID);
    CloudStoreConfig cloudStoreConfig = createDsmConfig(config, storageInfo.storageLocation);
    backupServiceTaskCreateRequestBuilder.setDsm(true).setCsConfig(cloudStoreConfig);
    return backupServiceTaskCreateRequestBuilder.build();
  }

  public BackupServiceTaskCreateRequest createDsmRequest(
      UUID customerUUID, UUID storageConfigUUID, String taskId, BackupTableParams tableParams) {
    BackupStorageInfo storageInfo = new BackupStorageInfo();
    storageInfo.backupType = tableParams.backupType;
    storageInfo.storageLocation = tableParams.storageLocation;
    return createDsmRequest(customerUUID, storageConfigUUID, taskId, storageInfo);
  }

  public BackupServiceTaskCreateRequest.Builder backupServiceTaskCreateBuilder(
      String taskId, NamespaceType nsType, BackupServiceTaskExtendedArgs exArgs) {
    // Redundant for now.
    boolean setCompression = false;
    String encryptionPassphrase = "";

    BackupServiceTaskCreateRequest.Builder backupServiceTaskCreateRequestBuilder =
        BackupServiceTaskCreateRequest.newBuilder()
            .setTaskId(taskId)
            .setCompression(setCompression)
            .setEncryptionPassphrase(encryptionPassphrase)
            .setNsType(nsType)
            .setExtendedArgs(exArgs);
    return backupServiceTaskCreateRequestBuilder;
  }

  /**
   * Generates table spec object for YCQL table backups
   *
   * @param backupTableParams
   * @return TableBackupSpec object
   */
  public TableBackupSpec getTableBackupSpec(BackupTableParams backupTableParams) {
    TableBackupSpec.Builder tableBackupSpecBuilder = TableBackupSpec.newBuilder();
    String keyspace = backupTableParams.getKeyspace();
    List<TableBackup> tableBackupList = new ArrayList<>();
    for (String tableName : backupTableParams.tableNameList) {
      TableBackup tableBackup =
          TableBackup.newBuilder().setKeyspace(keyspace).setTable(tableName).build();
      tableBackupList.add(tableBackup);
    }
    TableBackupSpec tableBackupSpec = tableBackupSpecBuilder.addAllTables(tableBackupList).build();
    return tableBackupSpec;
  }

  public NamespaceType getNamespaceType(TableType tableType) {
    switch (tableType) {
      case PGSQL_TABLE_TYPE:
        return NamespaceType.YSQL;
      case YQL_TABLE_TYPE:
        return NamespaceType.YCQL;
      default:
        return NamespaceType.UNRECOGNIZED;
    }
  }

  public static CloudStoreSpec buildCloudStoreSpec(
      String bucket,
      String cloudDir,
      String prevCloudDir,
      Map<String, String> credsMap,
      String configType) {
    CloudStoreSpec cloudStoreSpec =
        CloudStoreSpec.newBuilder()
            .putAllCreds(credsMap)
            .setBucket(bucket)
            .setCloudDir(cloudDir)
            .setPrevCloudDir(prevCloudDir)
            .setType(getCloudType(configType))
            .build();
    return cloudStoreSpec;
  }

  public double computePercentageComplete(Long completedOps, Long totalOps) {
    return completedOps * 100.0 / totalOps;
  }

  /**
   * Create cloud store config for YB-Controller backup task.
   *
   * @param config
   * @param commonSuffix
   * @return CloudStoreConfig object for YB-Controller task.
   */
  public CloudStoreConfig createBackupConfig(CustomerConfig config, String commonSuffix) {
    return createBackupConfig(config, commonSuffix, new HashMap<>());
  }

  /**
   * Create cloud store config for YB-Controller backup task with previous backup location.
   *
   * @param config
   * @param commonSuffix
   * @return CloudStoreConfig object for YB-Controller task.
   */
  public CloudStoreConfig createBackupConfig(
      CustomerConfig config,
      String commonSuffix,
      Map<String, String> keyspacePreviousLocationsMap) {
    String configType = config.name;
    CustomerConfigData configData = config.getDataObject();
    CloudStoreSpec defaultSpec = null;
    CloudStoreConfig.Builder cloudStoreConfigBuilder = CloudStoreConfig.newBuilder();
    StorageUtil storageUtil = StorageUtil.getStorageUtil(configType);
    defaultSpec =
        storageUtil.createCloudStoreSpec(
            ((CustomerConfigStorageData) configData).backupLocation,
            commonSuffix,
            keyspacePreviousLocationsMap.get(DEFAULT_REGION_STRING),
            configData);
    cloudStoreConfigBuilder.setDefaultSpec(defaultSpec);
    Map<String, String> regionLocationMap =
        StorageUtil.getStorageUtil(config.name).getRegionLocationsMap(configData);
    Map<String, CloudStoreSpec> regionSpecMap = new HashMap<>();
    if (MapUtils.isNotEmpty(regionLocationMap)) {
      regionLocationMap.forEach(
          (r, bL) -> {
            regionSpecMap.put(
                r,
                storageUtil.createCloudStoreSpec(
                    bL, commonSuffix, keyspacePreviousLocationsMap.get(r), configData));
          });
    }
    if (MapUtils.isNotEmpty(regionSpecMap)) {
      cloudStoreConfigBuilder.putAllRegionSpecMap(regionSpecMap);
    }
    return cloudStoreConfigBuilder.build();
  }

  public CloudStoreConfig createRestoreConfig(
      CustomerConfig config, YbcBackupResponse successMarker) {
    CustomerConfigData configData = config.getDataObject();

    StorageUtil storageUtil = StorageUtil.getStorageUtil(config.name);
    YbcBackupResponse.ResponseCloudStoreSpec.BucketLocation defaultBucketLocation =
        successMarker.responseCloudStoreSpec.defaultLocation;
    CloudStoreSpec defaultSpec =
        storageUtil.createRestoreCloudStoreSpec(
            ((CustomerConfigStorageData) configData).backupLocation,
            defaultBucketLocation.cloudDir,
            configData,
            false);

    CloudStoreConfig.Builder csConfigBuilder =
        CloudStoreConfig.newBuilder().setDefaultSpec(defaultSpec);

    Map<String, String> regionLocationMap =
        StorageUtil.getStorageUtil(config.name).getRegionLocationsMap(configData);
    Map<String, CloudStoreSpec> regionSpecMap = new HashMap<>();
    if (MapUtils.isNotEmpty(successMarker.responseCloudStoreSpec.regionLocations)) {
      successMarker.responseCloudStoreSpec.regionLocations.forEach(
          (r, bL) -> {
            if (regionLocationMap.containsKey(r)) {
              regionSpecMap.put(
                  r,
                  storageUtil.createRestoreCloudStoreSpec(
                      regionLocationMap.get(r), bL.cloudDir, configData, false));
            }
          });
    }
    if (MapUtils.isNotEmpty(regionSpecMap)) {
      csConfigBuilder.putAllRegionSpecMap(regionSpecMap);
    }
    return csConfigBuilder.build();
  }

  public CloudStoreConfig createDsmConfig(CustomerConfig config, String defaultBackupLocation) {
    CustomerConfigData configData = config.getDataObject();
    StorageUtil storageUtil = StorageUtil.getStorageUtil(config.name);
    CloudStoreSpec defaultSpec =
        storageUtil.createRestoreCloudStoreSpec(defaultBackupLocation, "", configData, true);

    CloudStoreConfig.Builder csConfigBuilder =
        CloudStoreConfig.newBuilder().setDefaultSpec(defaultSpec);
    return csConfigBuilder.build();
  }

  public List<String> getTableListFromSuccessMarker(YbcBackupResponse successMarker) {
    return getTableListFromSuccessMarker(successMarker, null);
  }

  public List<String> getTableListFromSuccessMarker(
      YbcBackupResponse successMarker, TableType tableType) {
    List<String> ycqlTableList =
        successMarker
            .snapshotObjectDetails
            .stream()
            .filter(
                sOD ->
                    sOD.type.equals(SnapshotObjectType.TABLE)
                        && (tableType != null
                            ? ((TableData) sOD.data).snapshotNamespaceType.equals(tableType)
                            : true))
            .map(sOD -> sOD.data.snapshotObjectName)
            .collect(Collectors.toList());
    return ycqlTableList;
  }

  public boolean validateYCQLTableListOverwrites(
      YbcBackupResponse successMarker, UUID universeUUID, String keyspace) {
    Universe universe = Universe.getOrBadRequest(universeUUID);
    List<String> existentTables =
        backupUtil
            .getTableInfosOrEmpty(universe)
            .stream()
            .filter(
                tIL ->
                    tIL.getNamespace().getName().equals(keyspace)
                        && tIL.getTableType().equals(TableType.YQL_TABLE_TYPE))
            .map(tIL -> tIL.getName())
            .collect(Collectors.toList());
    List<String> restoreTables =
        getTableListFromSuccessMarker(successMarker, TableType.YQL_TABLE_TYPE);
    return !CollectionUtils.containsAny(existentTables, restoreTables);
  }

  public void validateConfigWithSuccessMarker(
      YbcBackupResponse successMarker, CloudStoreConfig config, boolean forPrevDir)
      throws PlatformServiceException {
    BiFunction<YbcBackupResponse.ResponseCloudStoreSpec.BucketLocation, CloudStoreSpec, Boolean>
        compareAndValidate =
            forPrevDir
                ? SuccessMarkerConfigValidator.compareForPrevCloudDir
                : SuccessMarkerConfigValidator.compareForCloudDir;
    if (!compareAndValidate.apply(
        successMarker.responseCloudStoreSpec.defaultLocation, config.getDefaultSpec())) {
      throw new PlatformServiceException(
          PRECONDITION_FAILED, "Default location validation failed.");
    }
    if (MapUtils.isNotEmpty(successMarker.responseCloudStoreSpec.regionLocations)) {
      // config.getRegionSpecMapOrThrow throws exception for key not found.
      try {
        successMarker.responseCloudStoreSpec.regionLocations.forEach(
            (r, rS) -> {
              if (!(config.containsRegionSpecMap(r)
                  && compareAndValidate.apply(rS, config.getRegionSpecMapOrThrow(r)))) {
                throw new PlatformServiceException(
                    PRECONDITION_FAILED, "Region mapping validation failed.");
              }
            });
      } catch (Exception e) {
        throw new PlatformServiceException(
            PRECONDITION_FAILED, "Region mapping validation failed.");
      }
    }
  }

  private interface SuccessMarkerConfigValidator {
    BiFunction<YbcBackupResponse.ResponseCloudStoreSpec.BucketLocation, CloudStoreSpec, Boolean>
        compareForCloudDir =
            (sm, cs) ->
                StringUtils.equals(sm.bucket, cs.getBucket())
                    && StringUtils.equals(sm.cloudDir, cs.getCloudDir());
    BiFunction<YbcBackupResponse.ResponseCloudStoreSpec.BucketLocation, CloudStoreSpec, Boolean>
        compareForPrevCloudDir =
            (sm, cs) ->
                StringUtils.equals(sm.bucket, cs.getBucket())
                    && StringUtils.equals(sm.cloudDir, cs.getPrevCloudDir());
  }

  /**
   * Create extended args for YB-Controller backup, adds universe key details if present, optional
   * use tablespace parameter if true.
   *
   * @param tableParams
   * @return Extended args object for YB-Controller
   */
  public BackupServiceTaskExtendedArgs getExtendedArgsForBackup(BackupTableParams tableParams) {
    BackupServiceTaskExtendedArgs.Builder extendedArgsBuilder =
        BackupServiceTaskExtendedArgs.newBuilder();
    try {
      ObjectNode universeKeyHistory =
          encryptionAtRestManager.backupUniverseKeyHistory(tableParams.universeUUID);
      if (universeKeyHistory != null) {
        ObjectMapper mapper = new ObjectMapper();
        String backupKeys = mapper.writeValueAsString(universeKeyHistory);
        extendedArgsBuilder.setBackupConfigData(backupKeys);
      }
    } catch (Exception e) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          String.format(
              "%s Unable to generate backup keys metadata. error : ",
              getBaseLogMessage(tableParams.backupUuid, tableParams.getKeyspace()),
              e.getMessage()));
    }
    if (tableParams.useTablespaces) {
      extendedArgsBuilder.setUseTablespaces(true);
    }
    return extendedArgsBuilder.build();
  }

  public String getBaseLogMessage(UUID backupUUID, String keyspace) {
    return getBaseLogMessage(backupUUID, keyspace, null);
  }

  public String getBaseLogMessage(UUID backupUUID, String keyspace, UUID paramsIdentifier) {
    if (paramsIdentifier != null) {
      return String.format(
          "Backup %s - Keyspace %s - ParamsIdentifier %s :",
          backupUUID.toString(), keyspace, paramsIdentifier.toString());
    }
    return String.format("Backup %s - Keyspace %s :", backupUUID.toString(), keyspace);
  }

  public static CloudType getCloudType(String configType) {
    switch (configType) {
      case Util.S3:
        return CloudType.S3;
      case Util.GCS:
        return CloudType.GOOGLE;
      case Util.AZ:
        return CloudType.AZURE;
      case Util.NFS:
        return CloudType.NFS;
      default:
        throw new PlatformServiceException(BAD_REQUEST, "Invalid bucket type provided");
    }
  }

  /**
   * Return the keyspace to table params mapping from the given table params list.
   *
   * @param backupList
   * @return The mapping
   */
  public Map<ImmutablePair<TableType, String>, BackupTableParams> getBackupKeyspaceToParamsMap(
      List<BackupTableParams> backupList) {
    Map<ImmutablePair<TableType, String>, BackupTableParams> keyspaceToParamsMap = new HashMap<>();
    backupList.forEach(
        bL -> {
          keyspaceToParamsMap.put(ImmutablePair.of(bL.backupType, bL.getKeyspace()), bL);
        });
    return keyspaceToParamsMap;
  }
}
