// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.backuprestore.ybc;

import static java.util.stream.Collectors.joining;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.mvc.Http.Status.PRECONDITION_FAILED;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.fasterxml.jackson.annotation.JsonEnumDefaultValue;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.annotation.JsonSubTypes;
import com.fasterxml.jackson.annotation.JsonTypeInfo;
import com.fasterxml.jackson.annotation.JsonTypeInfo.As;
import com.fasterxml.jackson.annotation.JsonTypeInfo.Id;
import com.fasterxml.jackson.databind.DeserializationFeature;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.StorageUtil;
import com.yugabyte.yw.common.StorageUtilFactory;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.backuprestore.BackupHelper;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.common.backuprestore.BackupUtil.PerBackupLocationKeyspaceTables;
import com.yugabyte.yw.common.backuprestore.BackupUtil.PerLocationBackupInfo;
import com.yugabyte.yw.common.backuprestore.BackupUtil.TablespaceResponse;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil.TablesMetadata.TableDetails;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil.TablesMetadata.TableDetails.IndexTable;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil.YbcBackupResponse.ResponseCloudStoreSpec.BucketLocation;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil.YbcBackupResponse.SnapshotObjectDetails.NamespaceData;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil.YbcBackupResponse.SnapshotObjectDetails.SnapshotObjectData;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil.YbcBackupResponse.SnapshotObjectDetails.TableData;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.gflags.AutoFlagUtil;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.controllers.handlers.UniverseInfoHandler;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.RestoreBackupParams.BackupStorageInfo;
import com.yugabyte.yw.forms.RestorePreflightResponse;
import com.yugabyte.yw.models.Backup.BackupCategory;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.backuprestore.Tablespace;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageNFSData;
import io.ebean.annotation.EnumValue;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.function.BiFunction;
import java.util.stream.Collectors;
import javax.validation.ConstraintViolation;
import javax.validation.Valid;
import javax.validation.Validation;
import javax.validation.Validator;
import javax.validation.constraints.NotNull;
import javax.validation.constraints.Size;
import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.NonNull;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.codec.DecoderException;
import org.apache.commons.codec.binary.Hex;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.collections4.MapUtils;
import org.apache.commons.lang3.StringUtils;
import org.apache.commons.lang3.tuple.ImmutablePair;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.CommonTypes.TableType;
import org.yb.CommonTypes.YQLDatabase;
import org.yb.ybc.BackupServiceTaskCreateRequest;
import org.yb.ybc.BackupServiceTaskExtendedArgs;
import org.yb.ybc.BackupServiceTaskProgressRequest;
import org.yb.ybc.BackupServiceTaskResultRequest;
import org.yb.ybc.CloudStoreConfig;
import org.yb.ybc.CloudStoreSpec;
import org.yb.ybc.CloudType;
import org.yb.ybc.NamespaceType;
import org.yb.ybc.TableBackup;
import org.yb.ybc.TableBackupSpec;
import org.yb.ybc.TableRestoreSpec;
import org.yb.ybc.UserChangeSpec;
import play.libs.Json;

@Singleton
@Slf4j
public class YbcBackupUtil {

  // Time to wait (in millisec) between each poll to ybc.
  public static final int WAIT_EACH_ATTEMPT_MS = 15000;
  public static final int MAX_TASK_RETRIES = 10;
  public static final String DEFAULT_REGION_STRING = "default_region";
  public static final String YBC_SUCCESS_MARKER_TASK_SUFFIX = "_success_marker";
  public static final String YBC_SUCCESS_MARKER_FILE_NAME = "success";
  public static final String YBDB_AUTOFLAG_BACKUP_SUPPORT_VERSION = "2.19.3.0-b1";

  private final AutoFlagUtil autoFlagUtil;
  private final UniverseInfoHandler universeInfoHandler;
  private final CustomerConfigService configService;
  private final EncryptionAtRestManager encryptionAtRestManager;
  private final BackupHelper backupHelper;
  private final StorageUtilFactory storageUtilFactory;

  @Inject
  public YbcBackupUtil(
      AutoFlagUtil autoFlagUtil,
      UniverseInfoHandler universeInfoHandler,
      CustomerConfigService configService,
      EncryptionAtRestManager encryptionAtRestManager,
      BackupHelper backupHelper,
      StorageUtilFactory storageUtilFactory) {
    this.universeInfoHandler = universeInfoHandler;
    this.configService = configService;
    this.encryptionAtRestManager = encryptionAtRestManager;
    this.backupHelper = backupHelper;
    this.storageUtilFactory = storageUtilFactory;
    this.autoFlagUtil = autoFlagUtil;
  }

  public static final Logger LOG = LoggerFactory.getLogger(YbcBackupUtil.class);

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

  @Data
  @AllArgsConstructor
  public static class TablesMetadata {
    private final Map<String, TableDetails> tableDetailsMap;

    @Data
    @RequiredArgsConstructor
    @AllArgsConstructor
    public static class TableDetails {
      private @NonNull UUID tableIdentifier;
      private boolean hasIndexTables = false;
      private Set<IndexTable> indexTableRelations;

      @Data
      @AllArgsConstructor
      public static class IndexTable {
        private UUID indexTableUUID;
        private String indexTableName;
      }

      @JsonIgnore
      public Set<String> getAllIndexTables() {
        if (!hasIndexTables) {
          return new HashSet<>();
        }
        return indexTableRelations.parallelStream()
            .map(iT -> iT.indexTableName)
            .collect(Collectors.toSet());
      }
    }

    @JsonIgnore
    public Set<String> getIndexTables(Set<String> parentTables) {
      Set<String> tablesSet =
          tableDetailsMap.entrySet().parallelStream()
              .filter(
                  tDE ->
                      CollectionUtils.isNotEmpty(parentTables)
                          ? parentTables.contains(tDE.getKey())
                          : true)
              .flatMap(tDE -> tDE.getValue().getAllIndexTables().parallelStream())
              .collect(Collectors.toSet());
      return tablesSet;
    }

    @JsonIgnore
    public Set<String> getAllIndexTables() {
      return getIndexTables(null);
    }

    @JsonIgnore
    public Set<String> getParentTables() {
      return tableDetailsMap.keySet();
    }

    @JsonIgnore
    public Map<String, Set<String>> getTablesWithIndexesMap() {
      Map<String, Set<String>> tablesWithIndexesMap =
          tableDetailsMap.entrySet().parallelStream()
              .filter(tDE -> tDE.getValue().hasIndexTables)
              .collect(
                  Collectors.toMap(Map.Entry::getKey, tDE -> tDE.getValue().getAllIndexTables()));
      return tablesWithIndexesMap;
    }
  }

  @JsonIgnoreProperties(ignoreUnknown = true)
  public static class YbcSuccessBackupConfig {

    @JsonProperty("ybdb_version")
    @Valid
    public String ybdbVersion;

    @JsonProperty("master_auto_flags")
    @Valid
    public Set<String> masterAutoFlags;

    @JsonProperty("tserver_auto_flags")
    @Valid
    public Set<String> tserverAutoFlags;

    @JsonProperty("universe_keys")
    @Valid
    public JsonNode universeKeys;

    @JsonProperty("master_key_metadata")
    @Valid
    public JsonNode masterKeyMetadata;
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

    @JsonAlias("tablespace_info")
    @Valid
    public List<Tablespace> tablespaceInfos;

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

      @JsonIgnore
      public Map<String, BucketLocation> getBucketLocationsMap() {
        Map<String, BucketLocation> regionBucketLocationMap = new HashMap<>();
        regionBucketLocationMap.put(DEFAULT_REGION_STRING, defaultLocation);
        if (MapUtils.isNotEmpty(regionLocations)) {
          regionBucketLocationMap.putAll(regionLocations);
        }
        return regionBucketLocationMap;
      }
    }

    @JsonIgnoreProperties(ignoreUnknown = true)
    public static class SnapshotObjectDetails {
      @NotNull public SnapshotObjectType type;

      @NotNull public String id;

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

        @JsonAlias("indexed_table_id")
        public String indexedTableID;
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
  public static YbcBackupResponse parseYbcBackupResponse(String metadata) {
    ObjectMapper mapper = new ObjectMapper();
    // For custom types in Snapshot Info.
    mapper.enable(DeserializationFeature.READ_UNKNOWN_ENUM_VALUES_USING_DEFAULT_VALUE);
    YbcBackupResponse successMarker = null;
    try {
      successMarker = mapper.readValue(metadata, YbcBackupResponse.class);
      Validator validator = Validation.buildDefaultValidatorFactory().getValidator();
      Map<String, String> validationResponse =
          validator.validate(successMarker).stream()
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

  public static JsonNode getUniverseKeysJsonFromSuccessMarker(String extendedArgs) {
    try {
      ObjectMapper mapper = new ObjectMapper();
      YbcSuccessBackupConfig backupConfig =
          mapper.readValue(extendedArgs, YbcSuccessBackupConfig.class);
      return backupConfig.universeKeys;
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
        storageUtilFactory.getStorageUtil(config.getName()).getRegionLocationsMap(configData);
    List<BackupUtil.RegionLocations> regionLocations = new ArrayList<>();
    regionMap.forEach(
        (r, bL) -> {
          BackupUtil.RegionLocations rL = new BackupUtil.RegionLocations();
          rL.REGION = r;
          rL.LOCATION =
              BackupUtil.getExactRegionLocation(
                  tableParams.storageLocation,
                  regionLocationMap.get(r),
                  config.getName().equals("NFS")
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
   * @param previousTableParams Previous backup's params for incremental backup.
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
        BackupUtil.getLocationMap(previousTableParams);
    CloudStoreConfig cloudStoreConfig =
        createBackupConfig(config, specificCloudDir, keyspacePreviousLocationsMap);
    BackupServiceTaskExtendedArgs extendedArgs = getExtendedArgsForBackup(backupTableParams);

    BackupServiceTaskCreateRequest.Builder backupServiceTaskCreateRequestBuilder =
        backupServiceTaskCreateBuilder(taskID, namespaceType, extendedArgs);
    backupServiceTaskCreateRequestBuilder.setCsConfig(cloudStoreConfig);
    if (!backupTableParams.allTables
        && CollectionUtils.isNotEmpty(backupTableParams.tableNameList)) {
      backupServiceTaskCreateRequestBuilder.setTbs(getTableBackupSpec(backupTableParams));
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
    BackupServiceTaskExtendedArgs extendedArgs = getExtendedArgsForRestore(backupStorageInfo);
    BackupServiceTaskCreateRequest.Builder backupServiceTaskCreateRequestBuilder =
        backupServiceTaskCreateBuilder(taskId, namespaceType, extendedArgs);
    CustomerConfig config = configService.getOrBadRequest(customerUUID, storageConfigUUID);
    CloudStoreConfig cloudStoreConfig = createRestoreConfig(config, successMarker);
    backupServiceTaskCreateRequestBuilder.setCsConfig(cloudStoreConfig);
    addRestoreSpec(backupServiceTaskCreateRequestBuilder, backupStorageInfo, successMarker);
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

  // Static method for use outside backup/restore tasks.
  public static BackupServiceTaskCreateRequest createDsmRequest(
      CloudStoreSpec cloudStoreSpec, String taskId, NamespaceType nsType) {
    BackupServiceTaskExtendedArgs extendedArgs = BackupServiceTaskExtendedArgs.newBuilder().build();
    BackupServiceTaskCreateRequest.Builder backupServiceTaskCreateRequestBuilder =
        backupServiceTaskCreateBuilder(taskId, nsType, extendedArgs);
    CloudStoreConfig cloudStoreConfig =
        CloudStoreConfig.newBuilder().setDefaultSpec(cloudStoreSpec).build();
    return backupServiceTaskCreateRequestBuilder.setCsConfig(cloudStoreConfig).setDsm(true).build();
  }

  private static BackupServiceTaskCreateRequest.Builder backupServiceTaskCreateBuilder(
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

  private void addRestoreSpec(
      BackupServiceTaskCreateRequest.Builder backupServiceTaskCreateRequestBuilder,
      BackupStorageInfo bSInfo,
      YbcBackupResponse successMarker) {
    String keyspace = bSInfo.keyspace;
    if (bSInfo.selectiveTableRestore && CollectionUtils.isNotEmpty(bSInfo.tableNameList)) {
      Set<String> restorableTablesList =
          getTableListFromSuccessMarker(successMarker, bSInfo.backupType, true).getParentTables();
      if (CollectionUtils.isEqualCollection(restorableTablesList, bSInfo.tableNameList)) {
        // No need for selective restore here.
        backupServiceTaskCreateRequestBuilder.setNs(keyspace);
      } else {
        backupServiceTaskCreateRequestBuilder.setTrs(getTableRestoreSpec(successMarker, bSInfo));
      }
    } else {
      backupServiceTaskCreateRequestBuilder.setNs(keyspace);
    }
  }

  /**
   * Get table restore spec for restore using BackupStorageInfo's tableNameList and keyspace. For
   * selective restore, also add index tables relation here. Expects that the tableNameList from
   * params is not null here.
   *
   * @param successMarker The YbcBackupResponse object
   * @param bSInfo The BackupStorageInfo object
   * @return The generated TableRestoreSpec
   */
  public static TableRestoreSpec getTableRestoreSpec(
      YbcBackupResponse successMarker, BackupStorageInfo bSInfo) {
    if (CollectionUtils.isEmpty(bSInfo.tableNameList)) {
      throw new RuntimeException("Table restore attempted on empty table list");
    }
    TableRestoreSpec.Builder tableRestoreSpecBuilder = TableRestoreSpec.newBuilder();
    TablesMetadata tables = getTableListFromSuccessMarker(successMarker, bSInfo.backupType, false);
    Set<String> tablesToRestore = new HashSet<>(bSInfo.tableNameList);
    tablesToRestore.addAll(tables.getIndexTables(tablesToRestore));
    return tableRestoreSpecBuilder
        .setKeyspace(bSInfo.keyspace)
        .addAllTable(tablesToRestore)
        .build();
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
    String configType = config.getName();
    CustomerConfigData configData = config.getDataObject();
    CloudStoreSpec defaultSpec = null;
    CloudStoreConfig.Builder cloudStoreConfigBuilder = CloudStoreConfig.newBuilder();
    StorageUtil storageUtil = storageUtilFactory.getStorageUtil(configType);
    defaultSpec =
        storageUtil.createCloudStoreSpec(
            DEFAULT_REGION_STRING,
            commonSuffix,
            keyspacePreviousLocationsMap.get(DEFAULT_REGION_STRING),
            configData);
    cloudStoreConfigBuilder.setDefaultSpec(defaultSpec);
    Map<String, String> regionLocationMap =
        storageUtilFactory.getStorageUtil(config.getName()).getRegionLocationsMap(configData);
    Map<String, CloudStoreSpec> regionSpecMap = new HashMap<>();
    if (MapUtils.isNotEmpty(regionLocationMap)) {
      regionLocationMap.forEach(
          (r, bL) -> {
            regionSpecMap.put(
                r,
                storageUtil.createCloudStoreSpec(
                    r, commonSuffix, keyspacePreviousLocationsMap.get(r), configData));
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

    StorageUtil storageUtil = storageUtilFactory.getStorageUtil(config.getName());
    YbcBackupResponse.ResponseCloudStoreSpec.BucketLocation defaultBucketLocation =
        successMarker.responseCloudStoreSpec.defaultLocation;
    CloudStoreSpec defaultSpec =
        storageUtil.createRestoreCloudStoreSpec(
            DEFAULT_REGION_STRING, defaultBucketLocation.cloudDir, configData, false);

    CloudStoreConfig.Builder csConfigBuilder =
        CloudStoreConfig.newBuilder().setDefaultSpec(defaultSpec);

    Map<String, CloudStoreSpec> regionSpecMap = new HashMap<>();
    if (MapUtils.isNotEmpty(successMarker.responseCloudStoreSpec.regionLocations)) {
      successMarker.responseCloudStoreSpec.regionLocations.forEach(
          (r, bL) -> {
            regionSpecMap.put(
                r, storageUtil.createRestoreCloudStoreSpec(r, bL.cloudDir, configData, false));
          });
    }
    if (MapUtils.isNotEmpty(regionSpecMap)) {
      csConfigBuilder.putAllRegionSpecMap(regionSpecMap);
    }
    return csConfigBuilder.build();
  }

  public CloudStoreConfig createDsmConfig(CustomerConfig config, String defaultBackupLocation) {
    CustomerConfigData configData = config.getDataObject();
    StorageUtil storageUtil = storageUtilFactory.getStorageUtil(config.getName());
    CloudStoreSpec defaultSpec =
        storageUtil.createDsmCloudStoreSpec(defaultBackupLocation, configData);

    CloudStoreConfig.Builder csConfigBuilder =
        CloudStoreConfig.newBuilder().setDefaultSpec(defaultSpec);
    return csConfigBuilder.build();
  }

  public static TablesMetadata getTableListFromSuccessMarker(YbcBackupResponse successMarker) {
    return getTableListFromSuccessMarker(successMarker, null);
  }

  public static TablesMetadata getTableListFromSuccessMarker(
      YbcBackupResponse successMarker, TableType tableType) {
    return getTableListFromSuccessMarker(successMarker, null, false);
  }

  // Convert non "-" hyphen containing UUID string to UUID.
  private static UUID getUUIDFromString(String uuidString) {
    try {
      byte[] uuidData = Hex.decodeHex(uuidString.toCharArray());
      return new UUID(
          ByteBuffer.wrap(uuidData, 0, 8).getLong(), ByteBuffer.wrap(uuidData, 8, 8).getLong());
    } catch (DecoderException e) {
      throw new RuntimeException("Unable to parse uuid string to UUID");
    }
  }

  // @formatter:off
  /**
   * Generate Table list from success marker file YBC. A sample response with index tables: <pre>
   * {@code {
   *    "emp" : {
   *      "tableIdentifier" : "1ce3234b-e6b2-4e08-8946-fdd9a152905b",
   *      "hasIndexTables" : true,
   *      "indexTableRelations" : [ {
   *        "indexTableUUID" : "62ebe319-b183-4dd7-a8e7-16e6567bea0a",
   *        "indexTableName" : "emp_by_userid"
   *       } ]
   *    },
   *    "items" : {
   *      "tableIdentifier" : "5060ca20-4c19-499f-9488-e0763cd94be5",
   *      "hasIndexTables" : false
   *    },
   *    "cassandrakeyvalue" : {
   *      "tableIdentifier" : "468da871-d007-4bb2-b2f4-7779f6ceb91e",
   *      "hasIndexTables" : false
   *    }
   * }
   * </pre>
   *
   * @param successMarker The YbcBackupResponse object
   * @param tableType The table type PGSQL/YQL
   * @param filterIndexTables Whether to filter out index tables in response
   */
  // @formatter:on
  public static TablesMetadata getTableListFromSuccessMarker(
      YbcBackupResponse successMarker, TableType tableType, boolean filterIndexTables) {

    // Map to return
    Map<String, TableDetails> tablesToReturn = new ConcurrentHashMap<>();
    // Intermediate map which stores parent table info
    Map<String, String> parentTablesMap = new ConcurrentHashMap<>();

    // Get parent tables first
    successMarker.snapshotObjectDetails.parallelStream()
        .filter(
            sOD ->
                sOD.type.equals(SnapshotObjectType.TABLE)
                    && (tableType != null
                        ? ((TableData) sOD.data).snapshotNamespaceType.equals(tableType)
                        : true)
                    && ((TableData) sOD.data).indexedTableID == null)
        .forEach(
            sOD -> {
              String tableIdentifier = sOD.id;
              UUID tableUUID = getUUIDFromString(tableIdentifier);
              String tableName = sOD.data.snapshotObjectName;
              tablesToReturn.put(tableName, new TableDetails(tableUUID));
              parentTablesMap.put(tableIdentifier, tableName);
            });

    // Add index tables if required
    if (!filterIndexTables) {
      successMarker.snapshotObjectDetails.parallelStream()
          .filter(
              sOD ->
                  sOD.type.equals(SnapshotObjectType.TABLE)
                      && (tableType != null
                          ? ((TableData) sOD.data).snapshotNamespaceType.equals(tableType)
                          : true)
                      && (((TableData) sOD.data).indexedTableID != null))
          .forEach(
              sOD -> {
                String parentTableName = parentTablesMap.get(((TableData) sOD.data).indexedTableID);
                UUID indexTableUUID = getUUIDFromString(sOD.id);
                String indexTableName = sOD.data.snapshotObjectName;
                TableDetails tDetails = tablesToReturn.get(parentTableName);
                if (!tablesToReturn.get(parentTableName).hasIndexTables) {
                  tDetails.indexTableRelations = new HashSet<>();
                  tDetails.hasIndexTables = true;
                }
                tablesToReturn
                    .get(parentTableName)
                    .indexTableRelations
                    .add(new IndexTable(indexTableUUID, indexTableName));
              });
    }
    return new TablesMetadata(tablesToReturn);
  }

  public static void validateConfigWithSuccessMarker(
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
    try {
      YbcSuccessBackupConfig config = new YbcSuccessBackupConfig();
      Universe universe = Universe.getOrBadRequest(tableParams.getUniverseUUID());
      String ybdbSoftwareVersion =
          universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
      config.ybdbVersion = ybdbSoftwareVersion;
      if (Util.compareYbVersions(
              ybdbSoftwareVersion,
              YBDB_AUTOFLAG_BACKUP_SUPPORT_VERSION,
              true /* suppressFormatError */)
          >= 0) {
        config.masterAutoFlags =
            autoFlagUtil.getPromotedAutoFlags(
                universe,
                UniverseTaskBase.ServerType.MASTER,
                AutoFlagUtil.LOCAL_PERSISTED_AUTO_FLAG_CLASS);
        config.tserverAutoFlags =
            autoFlagUtil.getPromotedAutoFlags(
                universe,
                UniverseTaskBase.ServerType.TSERVER,
                AutoFlagUtil.LOCAL_PERSISTED_AUTO_FLAG_CLASS);
      }
      ObjectNode universeKeyHistory =
          encryptionAtRestManager.backupUniverseKeyHistory(tableParams.getUniverseUUID());
      if (universeKeyHistory != null) {
        config.universeKeys = universeKeyHistory.get("universe_keys");
        config.masterKeyMetadata = universeKeyHistory.get("master_key_metadata");
      }
      BackupServiceTaskExtendedArgs.Builder extendedArgsBuilder =
          BackupServiceTaskExtendedArgs.newBuilder();
      ObjectMapper mapper = new ObjectMapper();
      extendedArgsBuilder.setBackupConfigData(mapper.writeValueAsString(config));
      if (tableParams.useTablespaces) {
        extendedArgsBuilder.setUseTablespaces(true);
      }
      return extendedArgsBuilder.build();
    } catch (Exception e) {
      log.error("Error while fetching extended args for backup: ", e);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          String.format(
              "%s Unable to generate backup keys metadata. error : %s",
              getBaseLogMessage(tableParams.backupUuid, tableParams.getKeyspace()),
              e.getMessage()));
    }
  }

  /**
   * Verifies that universe has already promoted the provided set of master and tserver auto flags.
   *
   * @param universe
   * @param masterAutoFlags
   * @param tserverAutoFlags
   * @return
   * @throws IOException
   */
  public boolean validateAutoFlagCompatibility(
      Universe universe, Set<String> masterAutoFlags, Set<String> tserverAutoFlags)
      throws IOException {
    if (!CollectionUtils.isEmpty(masterAutoFlags)) {
      Set<String> targetMasterAutoFlags =
          autoFlagUtil.getPromotedAutoFlags(
              universe,
              UniverseTaskBase.ServerType.MASTER,
              AutoFlagUtil.LOCAL_PERSISTED_AUTO_FLAG_CLASS);
      for (String flag : masterAutoFlags) {
        if (!targetMasterAutoFlags.contains(flag)) {
          throw new PlatformServiceException(
              BAD_REQUEST,
              "Cannot restore backup as " + flag + " is missing on target universe master server.");
        }
      }
    }
    if (!CollectionUtils.isEmpty(tserverAutoFlags)) {
      Set<String> targetTServerAutoFlags =
          autoFlagUtil.getPromotedAutoFlags(
              universe,
              UniverseTaskBase.ServerType.MASTER,
              AutoFlagUtil.LOCAL_PERSISTED_AUTO_FLAG_CLASS);
      for (String flag : tserverAutoFlags) {
        if (!targetTServerAutoFlags.contains(flag)) {
          throw new PlatformServiceException(
              BAD_REQUEST,
              "Cannot restore backup as "
                  + flag
                  + " is missing on target universe tserver server.");
        }
      }
    }
    return true;
  }

  public BackupServiceTaskExtendedArgs getExtendedArgsForRestore(
      BackupStorageInfo backupStorageInfo) {
    BackupServiceTaskExtendedArgs.Builder extendedArgsBuilder =
        BackupServiceTaskExtendedArgs.newBuilder();
    if (backupStorageInfo.isUseTablespaces()) {
      extendedArgsBuilder.setUseTablespaces(true);
    }
    if (StringUtils.isNotBlank(backupStorageInfo.newOwner)) {
      extendedArgsBuilder.setUserSpec(
          UserChangeSpec.newBuilder()
              .setNewUsername(backupStorageInfo.newOwner)
              .setOldUsername(backupStorageInfo.oldOwner)
              .build());
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

  /**
   * Comprehensive validate restore overwrite using backup metadata, so that even partial restores
   * do not have overwrites, over multiple sub-parts of same keyspace type restore. Note that this
   * method only validates against restore request, and returns a map of TableType and keyspace
   * tables list if applicable. The map can then be used further to validate against Universe
   * content.
   *
   * @param backupStorageInfoList List of BackupStorageInfo objects, to extract tables/keyspaces to
   *     restore.
   * @param perLocationBackupInfoMap Map of string<->PerLocationBackupInfo which is the
   *     keyspace/table metadata for each of the locations.
   */
  public static Map<TableType, Map<String, Set<String>>> generateMapToRestoreNonRedisYBC(
      List<BackupStorageInfo> backupStorageInfoList,
      Map<String, PerLocationBackupInfo> perLocationBackupInfoMap) {
    Map<TableType, Map<String, Set<String>>> restoreMap = new HashMap<>();
    restoreMap.put(TableType.PGSQL_TABLE_TYPE, new HashMap<String, Set<String>>());
    restoreMap.put(TableType.YQL_TABLE_TYPE, new HashMap<String, Set<String>>());
    backupStorageInfoList.stream()
        // Filter out REDIS, we don't validate anything for it.
        .filter(bSI -> !bSI.backupType.equals(TableType.REDIS_TABLE_TYPE))
        .forEach(
            bSI -> {
              String location = bSI.storageLocation;
              String keyspace = bSI.keyspace;
              PerLocationBackupInfo bInfo = perLocationBackupInfoMap.get(location);

              // Verify no unknown tables request for backup
              if (bSI.backupType.equals(TableType.YQL_TABLE_TYPE)
                  && CollectionUtils.isNotEmpty(bSI.tableNameList)
                  && !bInfo
                      .getPerBackupLocationKeyspaceTables()
                      .getTableNameList()
                      .containsAll(bSI.tableNameList)) {
                throw new PlatformServiceException(
                    PRECONDITION_FAILED,
                    String.format(
                        "Unknown tables to restore found for keyspace: %s, backup location: %s",
                        keyspace, location));
              }
              // If keyspace seen for the first time.
              if (!restoreMap.get(bSI.backupType).containsKey(keyspace)) {
                restoreMap.get(bSI.backupType).put(keyspace, new HashSet<String>());
                if (bSI.backupType.equals(TableType.YQL_TABLE_TYPE)) {
                  Set<String> tablesToAdd =
                      getTablesToAddToRestoreMap(bInfo.getPerBackupLocationKeyspaceTables(), bSI);

                  restoreMap.get(bSI.backupType).get(keyspace).addAll(tablesToAdd);
                }
              } else {
                // For YSQL: If found DB again, throw error since there is a repetition request
                // For YCQL: If found keyspace again, check at table level whether there is a
                // repetition of request
                if (bSI.backupType.equals(TableType.PGSQL_TABLE_TYPE)
                    || !bSI.selectiveTableRestore
                    || CollectionUtils.isEmpty(bSI.tableNameList)
                    || CollectionUtils.containsAny(
                        restoreMap.get(TableType.YQL_TABLE_TYPE).get(keyspace),
                        bSI.tableNameList)) {
                  throw new PlatformServiceException(
                      PRECONDITION_FAILED,
                      String.format("Overwrite of data attempted for keyspace %s", keyspace));
                }
                Set<String> tablesToAdd =
                    getTablesToAddToRestoreMap(bInfo.getPerBackupLocationKeyspaceTables(), bSI);
                restoreMap.get(TableType.YQL_TABLE_TYPE).get(keyspace).addAll(tablesToAdd);
              }
            });
    return restoreMap;
  }

  private static Set<String> getTablesToAddToRestoreMap(
      PerBackupLocationKeyspaceTables perLocationBackupInfo, BackupStorageInfo bSI) {
    Set<String> tablesToAdd = new HashSet<>();

    // For table names provided and selective restore case.
    if (CollectionUtils.isNotEmpty(bSI.tableNameList) && bSI.selectiveTableRestore) {
      Set<String> tableNames = new HashSet<>(bSI.tableNameList);
      tablesToAdd.addAll(bSI.tableNameList);
      // Add index info if available
      tablesToAdd.addAll(perLocationBackupInfo.getIndexesOfTables(tableNames));
    } else {
      tablesToAdd.addAll(perLocationBackupInfo.getAllTables());
    }
    return tablesToAdd;
  }

  // @formatter:off
  /**
   * Using provided backup-location and corresponding YbcBackupResponse object, generate
   * RestorePreflightReponse object. A sample response: <pre>
   * {@code {
   *     "hasKMSHistory" : false,
   *     "backupCategory" : "YB_CONTROLLER",
   *     "perLocationBackupInfoMap" : {
   *      "s3://vkumar-gp-2/test/univ-70ba557a-1c1e-48d7-ac28-a26cd1d06ad7/..." : {
   *         "isYSQLBackup" : false,
   *         "isSelectiveRestoreSupported" : true,
   *         "backupLocation" : "s3://vkumar-gp-2/test/univ-70ba557a-1c1e-48d7-ac28-a26cd1...",
   *         "perBackupLocationKeyspaceTables" : {
   *           "originalKeyspace" : "ybdemo_keyspace",
   *           "tableNameList" : [ "emp", "items", "cassandrakeyvalue" ],
   *           "tablesWithIndexesMap" : {
   *             "emp" : [ "emp_by_userid" ]
   *           }
   *         }
   *       }
   *     }
   * }
   * </pre>
   *
   * @param ybcSuccessMarkerMap The map of backup_location<->YbcBackupResponse object
   * @param selectiveRestoreYbcCheck Boolean flag of whether the given YBC version supports
   *     selective restore or not
   * @param filterIndexes Boolean flag whether to to filter indexes in preflight tables response
   */
  // @formatter:on
  public static RestorePreflightResponse generateYBCRestorePreflightResponseUsingMetadata(
      Map<String, YbcBackupResponse> ybcSuccessMarkerMap,
      boolean selectiveRestoreYbcCheck,
      boolean filterIndexes,
      Map<String, TablespaceResponse> tablespaceResponsesMap) {
    RestorePreflightResponse.RestorePreflightResponseBuilder restorePreflightResponseBuilder =
        RestorePreflightResponse.builder();

    // Populate success marker map
    restorePreflightResponseBuilder.successMarkerMap(ybcSuccessMarkerMap);

    // Populate category as YB_CONTROLLER
    restorePreflightResponseBuilder.backupCategory(BackupCategory.YB_CONTROLLER);

    // Populate KMS param here
    boolean hasKMSHistory =
        ybcSuccessMarkerMap.values().stream()
            .anyMatch(
                sM -> {
                  JsonNode universeKeys =
                      getUniverseKeysJsonFromSuccessMarker(sM.extendedArgsString);
                  return universeKeys != null && !universeKeys.isNull();
                });
    restorePreflightResponseBuilder.hasKMSHistory(hasKMSHistory);

    // Populate namespace type, name, tables list( if applicable ) etc. here
    Map<String, PerLocationBackupInfo> perLocationBackupInfoMap =
        ybcSuccessMarkerMap.entrySet().stream()
            .collect(
                Collectors.toMap(
                    Map.Entry::getKey,
                    e -> {
                      PerLocationBackupInfo.PerLocationBackupInfoBuilder
                          perLocationBackupInfoBuilder = PerLocationBackupInfo.builder();
                      perLocationBackupInfoBuilder.backupLocation(e.getKey());
                      YbcBackupResponse sMarker = e.getValue();
                      SnapshotObjectData namespaceDetails =
                          sMarker.snapshotObjectDetails.parallelStream()
                              .filter(sOD -> sOD.type.equals(SnapshotObjectType.NAMESPACE))
                              .findAny()
                              .get()
                              .data;

                      // Check database type
                      Boolean isYSQLBackup =
                          ((NamespaceData) namespaceDetails)
                              .snapshotDatabaseType.equals(YQLDatabase.YQL_DATABASE_PGSQL);
                      perLocationBackupInfoBuilder.isYSQLBackup(isYSQLBackup);
                      PerBackupLocationKeyspaceTables.PerBackupLocationKeyspaceTablesBuilder
                          perBackupKeyspaceTablesBuilder =
                              PerBackupLocationKeyspaceTables.builder();

                      // Populate tablespaces related info
                      perLocationBackupInfoBuilder.tablespaceResponse(
                          tablespaceResponsesMap.get(e.getKey()));

                      // Populate keyspace name
                      perBackupKeyspaceTablesBuilder.originalKeyspace(
                          namespaceDetails.snapshotObjectName);

                      // If not YSQL, add table names and selective restore boolean.
                      if (!isYSQLBackup) {
                        TablesMetadata tablesMetadata =
                            getTableListFromSuccessMarker(
                                sMarker, TableType.YQL_TABLE_TYPE, filterIndexes);
                        Set<String> parentTables = tablesMetadata.getParentTables();
                        perBackupKeyspaceTablesBuilder.tableNameList(
                            new ArrayList<String>(parentTables));
                        if (!filterIndexes) {
                          perBackupKeyspaceTablesBuilder.tablesWithIndexesMap(
                              tablesMetadata.getTablesWithIndexesMap());
                        }
                        perLocationBackupInfoBuilder.isSelectiveRestoreSupported(
                            selectiveRestoreYbcCheck);
                      }
                      perLocationBackupInfoBuilder.perBackupLocationKeyspaceTables(
                          perBackupKeyspaceTablesBuilder.build());
                      return perLocationBackupInfoBuilder.build();
                    }));
    restorePreflightResponseBuilder.perLocationBackupInfoMap(perLocationBackupInfoMap);
    return restorePreflightResponseBuilder.build();
  }
}
