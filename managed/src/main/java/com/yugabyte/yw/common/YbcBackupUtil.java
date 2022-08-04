// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import play.libs.Json;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.PRECONDITION_FAILED;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static java.util.stream.Collectors.joining;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.net.HostAndPort;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.YbcBackupUtil.YbcBackupResponse.ResponseCloudStoreSpec.BucketLocation;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.services.YbcClientService;
import com.yugabyte.yw.controllers.handlers.UniverseInfoHandler;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.forms.RestoreBackupParams.BackupStorageInfo;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageData;
import com.yugabyte.yw.models.Universe;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.validation.ConstraintViolation;
import javax.validation.Valid;
import javax.validation.Validation;
import javax.validation.Validator;
import javax.validation.constraints.NotNull;
import javax.validation.constraints.Size;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.CollectionUtils;
import org.apache.commons.collections.MapUtils;
import org.apache.commons.lang.StringUtils;
import org.yb.CommonTypes.TableType;
import org.yb.client.YbcClient;
import org.yb.ybc.BackupServiceTaskCreateRequest;
import org.yb.ybc.BackupServiceTaskProgressRequest;
import org.yb.ybc.BackupServiceTaskResultRequest;
import org.yb.ybc.CloudStoreConfig;
import org.yb.ybc.CloudStoreSpec;
import org.yb.ybc.TableBackup;
import org.yb.ybc.TableBackupSpec;

import org.yb.ybc.BackupServiceTaskExtendedArgs;
import org.yb.ybc.NamespaceType;
import org.yb.ybc.CloudType;

@Singleton
@Slf4j
public class YbcBackupUtil {

  // Time to wait (in millisec) between each poll to ybc.
  public static final int WAIT_EACH_ATTEMPT_MS = 15000;
  public static final int MAX_TASK_RETRIES = 10;

  @Inject UniverseInfoHandler universeInfoHandler;
  @Inject YbcClientService ybcService;
  @Inject BackupUtil backupUtil;
  @Inject CustomerConfigService configService;
  @Inject EncryptionAtRestManager encryptionAtRestManager;

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
  }

  /**
   * Parse metadata object from YB-Controller backup result
   *
   * @param metadata
   * @return YbcBackupResponse object
   */
  public YbcBackupResponse parseYbcBackupResponse(String metadata) {
    ObjectMapper mapper = new ObjectMapper();
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
          INTERNAL_SERVER_ERROR, "Error parsing success marker string.");
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
    Map<String, String> regionLocationMap =
        StorageUtil.getStorageUtil(config.name).getRegionLocationsMap(config.getDataObject());
    List<BackupUtil.RegionLocations> regionLocations = new ArrayList<>();
    regionMap.forEach(
        (r, bL) -> {
          BackupUtil.RegionLocations rL = new BackupUtil.RegionLocations();
          rL.REGION = r;
          rL.LOCATION = BackupUtil.getExactRegionLocation(tableParams, regionLocationMap.get(r));
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
    return String.format("%s_%s_%s", uuid.toString(), backupType, keyspace);
  }

  /**
   * Creates backup task request compatible with YB-Controller
   *
   * @param backupTableParams
   * @return BackupServiceTaskCreateRequest object
   */
  public BackupServiceTaskCreateRequest createYbcBackupRequest(
      BackupTableParams backupTableParams) {
    CustomerConfig config =
        configService.getOrBadRequest(
            backupTableParams.customerUuid, backupTableParams.storageConfigUUID);
    String taskID =
        getYbcTaskID(
            backupTableParams.backupUuid,
            backupTableParams.backupType.name(),
            backupTableParams.getKeyspace());

    // Redundant for now.
    boolean setCompression = false;
    String encryptionPassphrase = "";

    NamespaceType namespaceType = getNamespaceType(backupTableParams.backupType);
    String specificCloudDir =
        BackupUtil.getBackupIdentifier(
            backupTableParams.universeUUID, backupTableParams.storageLocation);
    CloudStoreConfig cloudStoreConfig = createCloudStoreConfig(config, specificCloudDir, false);
    BackupServiceTaskExtendedArgs extendedArgs = getExtendedArgsForBackup(backupTableParams);

    BackupServiceTaskCreateRequest.Builder backupServiceTaskCreateRequestBuilder =
        BackupServiceTaskCreateRequest.newBuilder()
            .setTaskId(taskID)
            .setCompression(setCompression)
            .setCsConfig(cloudStoreConfig)
            .setEncryptionPassphrase(encryptionPassphrase)
            .setNsType(namespaceType)
            .setExtendedArgs(extendedArgs);
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
      RestoreBackupParams restoreBackupParams,
      BackupStorageInfo backupStorageInfo,
      String taskId,
      boolean isSuccessFileOnly) {
    CustomerConfig config =
        configService.getOrBadRequest(
            restoreBackupParams.customerUUID, restoreBackupParams.storageConfigUUID);

    String specificCloudDir =
        BackupUtil.getBackupIdentifier(
            restoreBackupParams.universeUUID, backupStorageInfo.storageLocation);

    // Redundant for now.
    boolean setCompression = false;
    String encryptionPassphrase = "";

    NamespaceType namespaceType = getNamespaceType(backupStorageInfo.backupType);
    String keyspace = backupStorageInfo.keyspace;
    BackupServiceTaskExtendedArgs extendedArgs = BackupServiceTaskExtendedArgs.newBuilder().build();

    BackupServiceTaskCreateRequest.Builder backupServiceTaskCreateRequestBuilder =
        BackupServiceTaskCreateRequest.newBuilder()
            .setTaskId(taskId)
            .setCompression(setCompression)
            .setEncryptionPassphrase(encryptionPassphrase)
            .setNsType(namespaceType)
            .setExtendedArgs(extendedArgs);

    CloudStoreConfig cloudStoreConfig = null;
    if (isSuccessFileOnly) {
      cloudStoreConfig = createCloudStoreConfig(config, specificCloudDir, true);
      backupServiceTaskCreateRequestBuilder.setDsm(true).setCsConfig(cloudStoreConfig);
    } else {
      cloudStoreConfig = createCloudStoreConfig(config, specificCloudDir, false);
      backupServiceTaskCreateRequestBuilder.setNs(keyspace).setCsConfig(cloudStoreConfig);
    }

    return backupServiceTaskCreateRequestBuilder.build();
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
      String bucket, String cloudDir, Map<String, String> credsMap, String configType) {
    CloudStoreSpec cloudStoreSpec =
        CloudStoreSpec.newBuilder()
            .putAllCreds(credsMap)
            .setBucket(bucket)
            .setCloudDir(cloudDir)
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
  public CloudStoreConfig createCloudStoreConfig(
      CustomerConfig config, String commonSuffix, boolean isSuccessFileOnly) {
    String configType = config.name;
    CustomerConfigData configData = config.getDataObject();
    CloudStoreSpec defaultSpec = null;
    CloudStoreConfig.Builder cloudStoreConfigBuilder = CloudStoreConfig.newBuilder();
    StorageUtil storageUtil = StorageUtil.getStorageUtil(configType);
    defaultSpec =
        storageUtil.createCloudStoreSpec(
            ((CustomerConfigStorageData) configData).backupLocation, commonSuffix, configData);
    cloudStoreConfigBuilder.setDefaultSpec(defaultSpec);
    if (isSuccessFileOnly) {
      return cloudStoreConfigBuilder.build();
    }
    Map<String, String> regionLocationMap =
        StorageUtil.getStorageUtil(config.name).getRegionLocationsMap(configData);
    Map<String, CloudStoreSpec> regionSpecMap = new HashMap<>();
    if (MapUtils.isNotEmpty(regionLocationMap)) {
      regionLocationMap.forEach(
          (r, bL) -> {
            regionSpecMap.put(r, storageUtil.createCloudStoreSpec(bL, commonSuffix, configData));
          });
    }
    if (MapUtils.isNotEmpty(regionSpecMap)) {
      cloudStoreConfigBuilder.putAllRegionSpecMap(regionSpecMap);
    }
    return cloudStoreConfigBuilder.build();
  }

  public void validateConfigWithSuccessMarker(
      YbcBackupResponse successMarker, CloudStoreConfig config) throws PlatformServiceException {
    CloudStoreSpec defaultSpec = config.getDefaultSpec();
    if (!(StringUtils.equals(
            successMarker.responseCloudStoreSpec.defaultLocation.bucket, defaultSpec.getBucket())
        && StringUtils.equals(
            successMarker.responseCloudStoreSpec.defaultLocation.cloudDir,
            defaultSpec.getCloudDir()))) {
      throw new PlatformServiceException(
          PRECONDITION_FAILED, "Default location validation failed.");
    }
    if (MapUtils.isNotEmpty(successMarker.responseCloudStoreSpec.regionLocations)) {
      // config.getRegionSpecMapOrThrow throws exception for key not found.
      try {
        successMarker.responseCloudStoreSpec.regionLocations.forEach(
            (r, rS) -> {
              if (!(config.containsRegionSpecMap(r)
                  && StringUtils.equals(
                      rS.cloudDir, config.getRegionSpecMapOrThrow(r).getCloudDir())
                  && StringUtils.equals(
                      rS.bucket, config.getRegionSpecMapOrThrow(r).getBucket()))) {
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
   * Get YB-Controller client
   *
   * @param universeUUID
   */
  public YbcClient getYbcClient(UUID universeUUID) throws PlatformServiceException {
    Universe universe = Universe.getOrBadRequest(universeUUID);
    String leaderIP = getMasterLeaderAddress(universe);
    String certificate = universe.getCertificateNodetoNode();
    Integer ybcPort = universe.getUniverseDetails().communicationPorts.ybControllerrRpcPort;
    YbcClient ybcClient = ybcService.getNewClient(leaderIP, ybcPort, certificate);
    if (ybcClient == null) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Could not create Yb-controller client.");
    }
    return ybcClient;
  }

  public String getMasterLeaderAddress(Universe universe) {
    HostAndPort hostPort = universeInfoHandler.getMasterLeaderIP(universe);
    String leaderIP = hostPort.getHost();
    return leaderIP;
  }
}
