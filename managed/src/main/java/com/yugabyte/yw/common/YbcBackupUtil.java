// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.net.HostAndPort;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.CloudUtil;
import com.yugabyte.yw.common.YbcBackupUtil.YbcBackupResponse.ResponseCloudStoreSpec;
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
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageNFSData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageWithRegionsData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageWithRegionsData.RegionLocation;
import com.yugabyte.yw.models.Universe;
import java.io.IOException;
import java.time.Duration;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.CollectionUtils;
import org.apache.commons.collections.MapUtils;
import org.yb.CommonTypes.TableType;
import org.yb.client.YbcClient;
import org.yb.ybc.BackupServiceTaskCreateRequest;
import org.yb.ybc.BackupServiceTaskProgressResponse;
import org.yb.ybc.BackupServiceTaskProgressRequest;
import org.yb.ybc.BackupServiceTaskResultRequest;
import org.yb.ybc.CloudStoreConfig;
import org.yb.ybc.CloudStoreSpec;
import org.yb.ybc.TableBackup;
import org.yb.ybc.TableBackupSpec;
import org.yb.ybc.BackupServiceTaskExtendedArgs;
import org.yb.ybc.NamespaceType;
import org.yb.ybc.CloudType;
import org.yb.ybc.ControllerStatus;

@Singleton
@Slf4j
public class YbcBackupUtil {

  private static final String CLOUD_STORE_SPEC = "cloud_store_spec";
  private static final String DEFAULT_BACKUP_LOCATION = "default_backup_location";
  private static final String BUCKET = "bucket";
  private static final String CLOUD_DIR = "cloud_dir";
  private static final String PREV_CLOUD_DIR = "prev_cloud_dir";
  private static final String REGION_LOCATION_MAP = "region_location_map";

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
    public String backupSize;

    @JsonAlias("cloud_store_spec")
    public ResponseCloudStoreSpec responseCloudStoreSpec;

    @JsonIgnoreProperties(ignoreUnknown = true)
    public static class ResponseCloudStoreSpec {
      @JsonAlias("default_backup_location")
      public BucketLocation defaultLocation;

      @JsonAlias("region_location_map")
      public Map<String, BucketLocation> regionLocations;

      @JsonIgnoreProperties(ignoreUnknown = true)
      public static class BucketLocation {
        @JsonAlias("bucket")
        public String bucket;

        @JsonAlias("cloud_dir")
        public String cloudDir;
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
    try {
      return mapper.readValue(metadata, YbcBackupResponse.class);
    } catch (IOException e) {
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "Error parsing backup response");
    }
  }

  /**
   * Extract region locations from YB-Controller's region spec map, and format it.
   *
   * @param regionMap
   * @param tableParams
   * @return List of region-locations for multi-region backups.
   */
  public List<BackupUtil.RegionLocations> extractRegionLocationfromMetadata(
      Map<String, BucketLocation> regionMap, BackupTableParams tableParams) {
    CustomerConfig config =
        configService.getOrBadRequest(tableParams.customerUuid, tableParams.storageConfigUUID);
    CustomerConfigStorageWithRegionsData configWithRegionsData =
        (CustomerConfigStorageWithRegionsData) config.getDataObject();
    Map<String, String> regionLocationMap = new HashMap<>();
    configWithRegionsData
        .regionLocations
        .stream()
        .forEach(rL -> regionLocationMap.put(rL.region, rL.location));
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

  public String getYbcTaskID(UUID backupUUID, String keyspace) {
    return String.format("%s_%s", backupUUID.toString(), keyspace);
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
    String taskID = getYbcTaskID(backupTableParams.backupUuid, backupTableParams.getKeyspace());

    // Redundant for now.
    boolean setCompression = false;
    String encryptionPassphrase = "";

    NamespaceType namespaceType = getNamespaceType(backupTableParams.backupType);
    String specificCloudDir =
        BackupUtil.getBackupIdentifier(
            backupTableParams.universeUUID, backupTableParams.storageLocation);
    CloudStoreConfig cloudStoreConfig = createCloudStoreConfig(config, specificCloudDir);
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
      RestoreBackupParams restoreBackupParams, BackupStorageInfo backupStorageInfo, String taskId) {
    CustomerConfig config =
        configService.getOrBadRequest(
            restoreBackupParams.customerUUID, restoreBackupParams.storageConfigUUID);

    // Redundant for now.
    boolean setCompression = false;
    String encryptionPassphrase = "";

    NamespaceType namespaceType = getNamespaceType(backupStorageInfo.backupType);
    String keyspace = backupStorageInfo.keyspace;
    CloudStoreConfig cloudStoreConfig = createCloudStoreConfig(config, backupStorageInfo);
    BackupServiceTaskExtendedArgs extendedArgs = BackupServiceTaskExtendedArgs.newBuilder().build();

    BackupServiceTaskCreateRequest.Builder backupServiceTaskCreateRequestBuilder =
        BackupServiceTaskCreateRequest.newBuilder()
            .setTaskId(taskId)
            .setCompression(setCompression)
            .setCsConfig(cloudStoreConfig)
            .setEncryptionPassphrase(encryptionPassphrase)
            .setNsType(namespaceType)
            .setNs(keyspace)
            .setExtendedArgs(extendedArgs);

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
  public CloudStoreConfig createCloudStoreConfig(CustomerConfig config, String commonSuffix) {
    String configType = config.name;
    CustomerConfigData configData = config.getDataObject();
    CloudStoreSpec defaultSpec = null;
    Map<String, CloudStoreSpec> regionSpecMap = null;
    CloudStoreConfig.Builder cloudStoreConfigBuilder = CloudStoreConfig.newBuilder();
    StorageUtil storageUtil = StorageUtil.getStorageUtil(configType);
    if (configData instanceof CustomerConfigStorageNFSData) {
      defaultSpec =
          storageUtil.createCloudStoreSpec(
              ((CustomerConfigStorageNFSData) configData).backupLocation, commonSuffix, configData);
    } else {
      defaultSpec =
          storageUtil.createCloudStoreSpec(
              ((CustomerConfigStorageData) configData).backupLocation, commonSuffix, configData);
      CustomerConfigStorageWithRegionsData configDataWithRegions =
          (CustomerConfigStorageWithRegionsData) configData;
      if (CollectionUtils.isNotEmpty(configDataWithRegions.regionLocations)) {
        regionSpecMap = new HashMap<>();
        for (CustomerConfigStorageWithRegionsData.RegionLocation regionLocation :
            configDataWithRegions.regionLocations) {
          regionSpecMap.put(
              regionLocation.region,
              storageUtil.createCloudStoreSpec(regionLocation.location, commonSuffix, configData));
        }
      }
    }

    cloudStoreConfigBuilder.setDefaultSpec(defaultSpec);
    if (MapUtils.isNotEmpty(regionSpecMap)) {
      cloudStoreConfigBuilder.putAllRegionSpecMap(regionSpecMap);
    }
    CloudStoreConfig cloudStoreConfig = cloudStoreConfigBuilder.build();
    return cloudStoreConfig;
  }

  public CloudStoreConfig createCloudStoreConfig(
      CustomerConfig config, BackupStorageInfo backupStorageInfo) {

    JsonNode successJson = readSuccessFile(config, backupStorageInfo.storageLocation);
    if (!validateSuccessJson(config, successJson)) {
      throw new RuntimeException("");
    }
    log.info(successJson.toString());

    CustomerConfigData configData = config.getDataObject();
    Map<String, CloudStoreSpec> regionSpecMap = null;
    CloudStoreConfig.Builder cloudStoreConfigBuilder = CloudStoreConfig.newBuilder();
    JsonNode cloudStoreSpecJson = successJson.get(CLOUD_STORE_SPEC);
    ResponseCloudStoreSpec cloudStoreSpec = null;
    try {
      ObjectMapper mapper = new ObjectMapper();
      cloudStoreSpec = mapper.treeToValue(cloudStoreSpecJson, ResponseCloudStoreSpec.class);
    } catch (Exception ex) {
      throw new RuntimeException("", ex);
    }
    StorageUtil storageUtil = StorageUtil.getStorageUtil(config.name);
    CloudStoreSpec defaultSpec =
        storageUtil.createCloudStoreSpec(
            configData,
            cloudStoreSpec.defaultLocation.bucket,
            cloudStoreSpec.defaultLocation.cloudDir);
    if (!(configData instanceof CustomerConfigStorageNFSData)) {

      if (cloudStoreSpecJson.has(REGION_LOCATION_MAP)) {
        JsonNode regionLocationMapJson = cloudStoreSpecJson.get(REGION_LOCATION_MAP);
        Iterator<Map.Entry<String, JsonNode>> it = regionLocationMapJson.fields();
        regionSpecMap = new HashMap<>();
        while (it.hasNext()) {
          Map.Entry<String, JsonNode> entry = it.next();
          String region = entry.getKey();
          JsonNode regionLocationJsonNode = entry.getValue();
          String backupRegionLocation =
              storageUtil.createDirPath(
                  regionLocationJsonNode.get(BUCKET).asText(),
                  regionLocationJsonNode.get(CLOUD_DIR).asText());
          regionSpecMap.put(
              region,
              storageUtil.createCloudStoreSpec(
                  configData,
                  regionLocationJsonNode.get(BUCKET).asText(),
                  regionLocationJsonNode.get(CLOUD_DIR).asText()));
        }
      }
    }

    cloudStoreConfigBuilder.setDefaultSpec(defaultSpec);
    if (MapUtils.isNotEmpty(regionSpecMap)) {
      cloudStoreConfigBuilder.putAllRegionSpecMap(regionSpecMap);
    }

    return cloudStoreConfigBuilder.build();
  }

  private JsonNode readSuccessFile(CustomerConfig config, String backupLocation) {
    String successFileLocation = backupLocation + "/" + CloudUtil.SUCCESS;
    StorageUtil storageUtil = StorageUtil.getStorageUtil(config.name);
    CustomerConfigData configData = config.getDataObject();
    JsonNode jsonNode = null;
    if (!config.name.equals(Util.NFS)) {
      try {
        jsonNode = ((CloudUtil) storageUtil).readFileFromCloud(successFileLocation, configData);
      } catch (Exception ex) {
        return jsonNode;
      }
    }
    return jsonNode;
  }

  private boolean validateSuccessJson(CustomerConfig config, JsonNode successJson) {
    if (successJson == null) {
      return false;
    }

    if (!successJson.has(CLOUD_STORE_SPEC)) {
      return false;
    }

    JsonNode cloudStoreSpecJson = successJson.get(CLOUD_STORE_SPEC);
    if (!cloudStoreSpecJson.has(DEFAULT_BACKUP_LOCATION)) {
      return false;
    }

    JsonNode defaultBackupJson = cloudStoreSpecJson.get(DEFAULT_BACKUP_LOCATION);
    if (!defaultBackupJson.has(BUCKET) || !defaultBackupJson.has(CLOUD_DIR)) {
      return false;
    }

    if (!config.name.equals(Util.NFS)) {
      if (cloudStoreSpecJson.has(REGION_LOCATION_MAP)) {
        StorageUtil storageUtil = StorageUtil.getStorageUtil(config.name);
        CustomerConfigStorageWithRegionsData customerConfig =
            (CustomerConfigStorageWithRegionsData) config.getDataObject();
        Map<String, String> regionLocationMap = new HashMap<>();
        if (customerConfig.regionLocations != null) {
          List<RegionLocation> regionLocations = customerConfig.regionLocations;
          for (RegionLocation regionLocation : regionLocations) {
            regionLocationMap.put(regionLocation.region, regionLocation.location);
          }
        }

        JsonNode regionLocationMapJson = cloudStoreSpecJson.get(REGION_LOCATION_MAP);
        Iterator<Map.Entry<String, JsonNode>> it = regionLocationMapJson.fields();
        while (it.hasNext()) {
          Map.Entry<String, JsonNode> entry = it.next();
          String region = entry.getKey();
          JsonNode regionLocationJsonNode = entry.getValue();

          if (!regionLocationJsonNode.has(BUCKET) || !regionLocationJsonNode.has(CLOUD_DIR)) {
            return false;
          }

          String bucket = regionLocationJsonNode.get(BUCKET).asText();
          String cloudDir = regionLocationJsonNode.get(CLOUD_DIR).asText();
          if (!regionLocationMap.containsKey(region)) {
            return false;
          }
          if (!regionLocationMap.get(region).contains(bucket)) {
            return false;
          }
        }
      }
    }
    return true;
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
