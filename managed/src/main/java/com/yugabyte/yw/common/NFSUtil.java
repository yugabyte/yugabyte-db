// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.PRECONDITION_FAILED;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.common.backuprestore.BackupUtil.PerLocationBackupInfo;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil.YbcBackupResponse;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil.YbcBackupResponse.ResponseCloudStoreSpec.BucketLocation;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.RestorePreflightParams;
import com.yugabyte.yw.forms.RestorePreflightResponse;
import com.yugabyte.yw.models.Backup.BackupCategory;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageNFSData;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import org.yb.ybc.BackupServiceNfsDirDeleteRequest;
import org.yb.ybc.CloudStoreSpec;

@Singleton
@Slf4j
public class NFSUtil implements StorageUtil {

  public static final String DEFAULT_YUGABYTE_NFS_BUCKET = "yugabyte_backup";
  private static final String YBC_NFS_DIR_FIELDNAME = "YBC_NFS_DIR";

  @Inject NodeUniverseManager nodeUniverseManager;

  @Override
  public CloudStoreSpec createCloudStoreSpec(
      String region,
      String commonDir,
      String previousBackupLocation,
      CustomerConfigData configData) {
    String cloudDir = BackupUtil.appendSlash(commonDir);
    String previousCloudDir = "";
    if (StringUtils.isNotBlank(previousBackupLocation)) {
      previousCloudDir =
          BackupUtil.appendSlash(BackupUtil.getBackupIdentifier(previousBackupLocation, true));
    }
    String storageLocation = getRegionLocationsMap(configData).get(region);
    String bucket = getRegionBucketMap(configData).get(region);
    Map<String, String> credsMap = createCredsMapYbc(storageLocation);
    return YbcBackupUtil.buildCloudStoreSpec(
        bucket, cloudDir, previousCloudDir, credsMap, Util.NFS);
  }

  @Override
  public CloudStoreSpec createRestoreCloudStoreSpec(
      String region, String cloudDir, CustomerConfigData configData, boolean isDsm) {
    String bucket = getRegionBucketMap(configData).get(region);
    String storageLocation = getRegionLocationsMap(configData).get(region);
    Map<String, String> credsMap = new HashMap<>();
    if (isDsm) {
      String location =
          getNfsLocationString(
              cloudDir, bucket, ((CustomerConfigStorageData) configData).backupLocation);
      location = BackupUtil.appendSlash(location);
      credsMap = createCredsMapYbc(((CustomerConfigStorageData) configData).backupLocation);
      return YbcBackupUtil.buildCloudStoreSpec(bucket, location, "", credsMap, Util.NFS);
    }
    credsMap = createCredsMapYbc(storageLocation);
    return YbcBackupUtil.buildCloudStoreSpec(bucket, cloudDir, "", credsMap, Util.NFS);
  }

  private Map<String, String> createCredsMapYbc(String storageLocation) {
    Map<String, String> nfsMap = new HashMap<>();
    nfsMap.put(YBC_NFS_DIR_FIELDNAME, storageLocation);
    return nfsMap;
  }

  public Map<String, String> getRegionLocationsMap(CustomerConfigData configData) {
    Map<String, String> regionLocationsMap = new HashMap<>();
    CustomerConfigStorageNFSData nfsData = (CustomerConfigStorageNFSData) configData;
    if (CollectionUtils.isNotEmpty(nfsData.regionLocations)) {
      nfsData.regionLocations.stream()
          .forEach(rL -> regionLocationsMap.put(rL.region, rL.location));
    }
    regionLocationsMap.put(YbcBackupUtil.DEFAULT_REGION_STRING, nfsData.backupLocation);
    return regionLocationsMap;
  }

  private static Map<String, String> getRegionBucketMap(CustomerConfigData configData) {
    Map<String, String> regionBucketMap = new HashMap<>();
    CustomerConfigStorageNFSData nfsData = (CustomerConfigStorageNFSData) configData;
    if (CollectionUtils.isNotEmpty(nfsData.regionLocations)) {
      nfsData.regionLocations.stream().forEach(rL -> regionBucketMap.put(rL.region, rL.nfsBucket));
    }
    regionBucketMap.put(YbcBackupUtil.DEFAULT_REGION_STRING, nfsData.nfsBucket);
    return regionBucketMap;
  }

  public void validateDirectory(CustomerConfigData customerConfigData, Universe universe) {
    for (NodeDetails node : universe.getTServersInPrimaryCluster()) {
      String backupDirectory = ((CustomerConfigStorageNFSData) customerConfigData).backupLocation;
      if (!backupDirectory.endsWith("/")) {
        backupDirectory += "/";
      }
      if (!nodeUniverseManager.isDirectoryWritable(backupDirectory, node, universe)) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "NFS Storage Config Location: "
                + backupDirectory
                + " is not writable in Node: "
                + node.getNodeName());
      }
    }
  }

  private String getNfsLocationString(String backupLocation, String bucket, String configLocation) {
    String location =
        StringUtils.removeStart(
            backupLocation, BackupUtil.getPathWithPrefixSuffixJoin(configLocation, bucket));
    location = StringUtils.removeStart(location, "/");
    return location;
  }

  @Override
  public void validateStorageConfigOnUniverse(CustomerConfig config, Universe universe) {
    validateDirectory(config.getDataObject(), universe);
  }

  @Override
  public boolean checkFileExists(
      CustomerConfigData configData,
      Set<String> locations,
      String fileName,
      UUID universeUUID,
      boolean checkExistsOnAll) {
    List<String> absoluteLocationsList =
        locations.parallelStream()
            .map(l -> BackupUtil.getPathWithPrefixSuffixJoin(l, fileName))
            .collect(Collectors.toList());
    Map<String, Boolean> locationsFileCheckResultMap =
        bulkCheckFilesExistWithAbsoluteLocations(
            Universe.getOrBadRequest(universeUUID), absoluteLocationsList);
    if (checkExistsOnAll) {
      return locationsFileCheckResultMap.values().stream().allMatch(b -> b.equals(true));
    }
    return locationsFileCheckResultMap.values().contains(true);
  }

  // Method accepts list of absolute file locations, performs a search on primary cluster node for
  // the files, and returns a map of file<->boolean exists for each file.
  // The files check itself happen together( 1 SSH call ).
  public Map<String, Boolean> bulkCheckFilesExistWithAbsoluteLocations(
      Universe universe, List<String> absoluteLocationsList) {
    Map<String, Boolean> bulkCheckFileExistsMap = new HashMap<>();
    NodeDetails node = universe.getLiveTServersInPrimaryCluster().get(0);
    String identifierUUID = UUID.randomUUID().toString();
    String sourceFilesToCheckFilename = identifierUUID + "-" + "bulk_check_files_node";
    String sourceFilesToCheckPath =
        BackupUtil.getPathWithPrefixSuffixJoin(
            nodeUniverseManager.getLocalTmpDir(), sourceFilesToCheckFilename);
    String targetLocalFilename = identifierUUID + "-" + "bulk_check_files_output_node";
    String targetLocalFilepath =
        BackupUtil.getPathWithPrefixSuffixJoin(
            nodeUniverseManager.getLocalTmpDir(), targetLocalFilename);
    File sourceFilesToCheckFile = null;

    try {
      sourceFilesToCheckFile = new File(sourceFilesToCheckPath);
      PrintWriter pWriter = new PrintWriter(sourceFilesToCheckFile);
      absoluteLocationsList.stream().forEach(l -> pWriter.println(l));
      pWriter.close();
      ShellResponse response =
          nodeUniverseManager.bulkCheckFilesExist(
              node,
              universe,
              nodeUniverseManager.getRemoteTmpDir(node, universe),
              sourceFilesToCheckPath,
              targetLocalFilepath);
      if (response.code != 0) {
        throw new RuntimeException(response.getMessage());
      }
      sourceFilesToCheckFile.delete();
      bulkCheckFileExistsMap = parseBulkCheckOutputFile(targetLocalFilepath);
      if (bulkCheckFileExistsMap.keySet().size() != absoluteLocationsList.size()) {
        throw new RuntimeException(
            String.format(
                "I/O mismatch in generating bulk check file exist result on node %s",
                node.getNodeName()));
      }
      return bulkCheckFileExistsMap;
    } catch (Exception e) {
      throw new RuntimeException(
          String.format(
              "Error checking existence of file locations provided on node %s", node.getNodeName()),
          e);
    } finally {
      if (sourceFilesToCheckFile != null && sourceFilesToCheckFile.exists()) {
        sourceFilesToCheckFile.delete();
      }
    }
  }

  private Map<String, Boolean> parseBulkCheckOutputFile(String filePath) {
    Map<String, Boolean> bulkCheckFileExistsMap = new HashMap<>();
    File targetLocalFile = null;
    try {
      targetLocalFile = new File(filePath);
      BufferedReader bReader = new BufferedReader(new FileReader(targetLocalFile));
      bReader
          .lines()
          .forEach(
              fLine -> {
                String[] splitLine = fLine.trim().split("\\s+");
                bulkCheckFileExistsMap.put(splitLine[0], splitLine[1].equals("0") ? false : true);
              });
      bReader.close();
      targetLocalFile.delete();
      return bulkCheckFileExistsMap;
    } catch (IOException e) {
      throw new RuntimeException("Error parsing bulk check output for NFS locations.");
    } finally {
      if (targetLocalFile != null && targetLocalFile.exists()) {
        targetLocalFile.delete();
      }
    }
  }

  @Override
  public RestorePreflightResponse generateYBBackupRestorePreflightResponseWithoutBackupObject(
      RestorePreflightParams preflightParams, CustomerConfigData configData) {
    return generateYBBackupRestorePreflightResponseWithoutBackupObject(preflightParams);
  }

  public RestorePreflightResponse generateYBBackupRestorePreflightResponseWithoutBackupObject(
      RestorePreflightParams preflightParams) {
    Universe universe = Universe.getOrBadRequest(preflightParams.getUniverseUUID());
    RestorePreflightResponse.RestorePreflightResponseBuilder restorePreflightResponseBuilder =
        RestorePreflightResponse.builder();
    restorePreflightResponseBuilder.backupCategory(BackupCategory.YB_BACKUP_SCRIPT);
    boolean isSelectiveRestoreSupported = false;

    // Bundle up exact file locations for search in NFS locations( reduce SSH )
    List<String> absoluteLocationsList = new ArrayList<>();
    preflightParams
        .getBackupLocations()
        .forEach(
            bL -> {
              absoluteLocationsList.add(
                  BackupUtil.getPathWithPrefixSuffixJoin(bL, BackupUtil.SNAPSHOT_PB));
              absoluteLocationsList.add(
                  BackupUtil.getPathWithPrefixSuffixJoin(
                      bL.substring(0, bL.lastIndexOf('/')), BackupUtil.BACKUP_KEYS_JSON));
              absoluteLocationsList.add(
                  BackupUtil.getPathWithPrefixSuffixJoin(bL, BackupUtil.YSQL_DUMP));
            });
    Map<String, Boolean> cloudLocationsFileExistenceMap =
        bulkCheckFilesExistWithAbsoluteLocations(universe, absoluteLocationsList);

    // Check SnapshotInfoPB file exists on all locations.
    boolean snapshotFileDoesNotExist =
        cloudLocationsFileExistenceMap.entrySet().parallelStream()
            .anyMatch(
                bLEntry ->
                    bLEntry.getKey().endsWith(BackupUtil.SNAPSHOT_PB)
                        && bLEntry.getValue().equals(false));
    if (snapshotFileDoesNotExist) {
      throw new RuntimeException("No snapshot export file 'SnapshotInfoPB' found: Bad backup.");
    }

    // Check KMS history
    boolean hasKMSHistory =
        cloudLocationsFileExistenceMap.entrySet().parallelStream()
            .filter(
                bLEntry ->
                    bLEntry.getKey().endsWith(BackupUtil.BACKUP_KEYS_JSON)
                        && bLEntry.getValue().equals(true))
            .findAny()
            .isPresent();
    restorePreflightResponseBuilder.hasKMSHistory(hasKMSHistory);

    Map<String, PerLocationBackupInfo> perLocationBackupInfoMap = new HashMap<>();
    preflightParams.getBackupLocations().stream()
        .forEach(
            bL -> {
              PerLocationBackupInfo.PerLocationBackupInfoBuilder perLocationBackupInfoBuilder =
                  PerLocationBackupInfo.builder();
              perLocationBackupInfoBuilder.isSelectiveRestoreSupported(isSelectiveRestoreSupported);
              if (cloudLocationsFileExistenceMap.get(
                  BackupUtil.getPathWithPrefixSuffixJoin(bL, BackupUtil.YSQL_DUMP))) {
                perLocationBackupInfoBuilder.isYSQLBackup(true);
              }
              perLocationBackupInfoMap.put(bL, perLocationBackupInfoBuilder.build());
            });
    restorePreflightResponseBuilder.perLocationBackupInfoMap(perLocationBackupInfoMap);
    return restorePreflightResponseBuilder.build();
  }

  @Override
  public void validateStorageConfigOnSuccessMarker(
      CustomerConfigData configData, YbcBackupResponse successMarker) {
    Map<String, String> configRegionBucketMap = getRegionBucketMap(configData);
    Map<String, BucketLocation> successMarkerBucketLocationMap =
        successMarker.responseCloudStoreSpec.getBucketLocationsMap();
    successMarkerBucketLocationMap.entrySet().stream()
        .forEach(
            sME -> {
              if (!configRegionBucketMap.containsKey(sME.getKey())) {
                throw new PlatformServiceException(
                    PRECONDITION_FAILED,
                    String.format("Storage config does not contain region %s", sME.getKey()));
              }
              String configBucket = configRegionBucketMap.get(sME.getKey());
              if (!configBucket.equals(sME.getValue().bucket)) {
                throw new PlatformServiceException(
                    PRECONDITION_FAILED,
                    String.format(
                        "Unknown bucket %s found for region %s, wanted: %s",
                        configBucket, sME.getKey(), sME.getValue().bucket));
              }
            });
  }

  public List<BackupServiceNfsDirDeleteRequest> getBackupServiceNfsDirDeleteRequest(
      List<BackupTableParams> bParamsList, CustomerConfigData configData) {
    List<BackupServiceNfsDirDeleteRequest> nfsDirDelRequestList = new ArrayList<>();
    Map<String, String> regionBucketMap = getRegionBucketMap(configData);
    Map<String, String> regionLocationsMap = getRegionLocationsMap(configData);
    bParamsList.stream()
        .forEach(
            bP -> {
              String cloudDir = BackupUtil.getBackupIdentifier(bP.storageLocation, true);
              Map<String, String> backupRegionLocationMap = BackupUtil.getLocationMap(bP);
              backupRegionLocationMap.keySet().stream()
                  .forEach(
                      r -> {
                        if (!regionBucketMap.containsKey(r) || !regionLocationsMap.containsKey(r)) {
                          throw new RuntimeException(
                              "NFS backup delete failed: Config does not contain all regions used"
                                  + " in backup.");
                        }
                        String nfsDir = regionLocationsMap.get(r);
                        String bucket = regionBucketMap.get(r);
                        BackupServiceNfsDirDeleteRequest backupServiceNfsDirDeleteRequest =
                            BackupServiceNfsDirDeleteRequest.newBuilder()
                                .setCloudDir(cloudDir)
                                .setBucket(bucket)
                                .setNfsDir(nfsDir)
                                .build();
                        nfsDirDelRequestList.add(backupServiceNfsDirDeleteRequest);
                      });
            });
    return nfsDirDelRequestList;
  }
}
