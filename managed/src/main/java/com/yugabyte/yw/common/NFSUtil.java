// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.mvc.Http.Status.PRECONDITION_FAILED;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.common.backuprestore.BackupUtil.PerLocationBackupInfo;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.RestorePreflightResponse;
import com.yugabyte.yw.forms.backuprestore.AdvancedRestorePreflightParams;
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
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
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

  @Inject RuntimeConfGetter runtimeConfGetter;
  @Inject NodeUniverseManager nodeUniverseManager;

  @Override
  public CloudStoreSpec createCloudStoreSpec(
      String region,
      String commonDir,
      String previousBackupLocation,
      CustomerConfigData configData,
      Universe universe) {
    String cloudDir = StringUtils.isNotBlank(commonDir) ? BackupUtil.appendSlash(commonDir) : "";
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
      String region,
      String cloudDir,
      CustomerConfigData configData,
      boolean isDsm,
      Universe universe) {
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
    for (NodeDetails node : universe.getRunningTserversInPrimaryCluster()) {
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
  public void checkStoragePrefixValidity(
      CustomerConfigData configData,
      @Nullable String region,
      String backupLocation,
      boolean checkBucket) {
    region = StringUtils.isBlank(region) ? YbcBackupUtil.DEFAULT_REGION_STRING : region;
    String configLocation = getRegionLocationsMap(configData).get(region);
    if (checkBucket) {
      String bucket = getRegionBucketMap(configData).get(region);
      configLocation = BackupUtil.getPathWithPrefixSuffixJoin(configLocation, bucket);
    }
    if (!StringUtils.startsWith(backupLocation, configLocation)) {
      throw new PlatformServiceException(
          PRECONDITION_FAILED,
          String.format(
              "Matching failed for config-location %s and backup-location %s",
              configLocation, backupLocation));
    }
  }

  @Override
  public void validateStorageConfigOnUniverseNonRpc(CustomerConfig config, Universe universe) {
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
        locations.stream()
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
    NodeDetails node = universe.getRunningTserversInPrimaryCluster().get(0);
    String identifierUUID = UUID.randomUUID().toString();
    String sourceFilesToCheckFilename = identifierUUID + "-bulk_check_files_node";
    String sourceFilesToCheckPath =
        BackupUtil.getPathWithPrefixSuffixJoin(
            nodeUniverseManager.getLocalTmpDir(), sourceFilesToCheckFilename);
    String targetLocalFilename = identifierUUID + "-bulk_check_files_output_node";
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
      AdvancedRestorePreflightParams preflightParams, CustomerConfigData configData) {
    return generateYBBackupRestorePreflightResponseWithoutBackupObject(preflightParams);
  }

  public RestorePreflightResponse generateYBBackupRestorePreflightResponseWithoutBackupObject(
      AdvancedRestorePreflightParams preflightParams) {
    Universe universe = Universe.getOrBadRequest(preflightParams.getUniverseUUID());
    RestorePreflightResponse.RestorePreflightResponseBuilder restorePreflightResponseBuilder =
        RestorePreflightResponse.builder();
    restorePreflightResponseBuilder.backupCategory(BackupCategory.YB_BACKUP_SCRIPT);

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
        cloudLocationsFileExistenceMap.entrySet().stream()
            .anyMatch(
                bLEntry ->
                    bLEntry.getKey().endsWith(BackupUtil.SNAPSHOT_PB)
                        && bLEntry.getValue().equals(false));
    if (snapshotFileDoesNotExist) {
      throw new RuntimeException("No snapshot export file 'SnapshotInfoPB' found: Bad backup.");
    }

    // Check KMS history
    boolean hasKMSHistory =
        cloudLocationsFileExistenceMap.entrySet().stream()
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
              perLocationBackupInfoBuilder.isSelectiveRestoreSupported(
                  false /* isSelectiveRestoreSupported */);
              if (cloudLocationsFileExistenceMap.get(
                  BackupUtil.getPathWithPrefixSuffixJoin(bL, BackupUtil.YSQL_DUMP))) {
                perLocationBackupInfoBuilder.isYSQLBackup(true /* isYSQLBackup */);
              }
              perLocationBackupInfoMap.put(bL, perLocationBackupInfoBuilder.build());
            });
    restorePreflightResponseBuilder.perLocationBackupInfoMap(perLocationBackupInfoMap);
    return restorePreflightResponseBuilder.build();
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

  @Override
  public boolean uploadYbaBackup(CustomerConfigData configData, File backup, String backupDir) {
    String nfsDirectory = ((CustomerConfigStorageNFSData) configData).backupLocation;

    // Construct the full path to the target directory
    File targetDir = new File(nfsDirectory, backupDir);

    // Ensure the directory exists
    if (!targetDir.exists() && !targetDir.mkdirs()) {
      log.error("Failed to create backup directory: " + targetDir.getAbsolutePath());
      return false;
    }

    // Construct the target file path
    File targetFile = new File(targetDir, backup.getName());
    log.info("Target File Name: {}", targetFile.getAbsolutePath());

    try {
      // Copy the file
      Files.copy(backup.toPath(), targetFile.toPath(), StandardCopyOption.REPLACE_EXISTING);
      log.info("Backup successfully copied to: " + targetFile.getAbsolutePath());
      // Write the marker file to /nfsDirectory/backupDir/.yba_backup_marker
      File markerFile = new File(targetDir, YBA_BACKUP_MARKER);
      markerFile.createNewFile();
      return true;
    } catch (IOException e) {
      log.error("Failed to copy backup: " + e.getMessage());
    }
    return false;
  }

  @Override
  public List<String> getYbaBackupDirs(CustomerConfigData configData) {
    String nfsDirectory = ((CustomerConfigStorageNFSData) configData).backupLocation;
    File nfsDir = new File(nfsDirectory);
    if (!nfsDir.exists() || !nfsDir.isDirectory()) {
      log.warn("NFS directory {} does not exist or is not a directory.", nfsDirectory);
      return new ArrayList<>();
    }
    // Find all directories in the NFS directory that have .yba_backup_marker file
    File[] backupDirs =
        nfsDir.listFiles(
            (dir, name) ->
                new File(dir, name).isDirectory()
                    && new File(dir, name + "/.yba_backup_marker").exists());
    return Arrays.stream(backupDirs).map(File::getName).collect(Collectors.toList());
  }

  @Override
  public String getYbaBackupStorageLocation(CustomerConfigData configData, String backupDir) {
    String nfsDirectory = ((CustomerConfigStorageNFSData) configData).backupLocation;

    // Construct the full path to the target directory
    File targetDir = new File(nfsDirectory, backupDir);
    return targetDir.toString();
  }

  @Override
  public boolean cleanupUploadedBackups(CustomerConfigData configData, String backupDir) {
    try {
      String nfsDirectory = ((CustomerConfigStorageNFSData) configData).backupLocation;

      // Construct the full path to the backup directory
      File targetDir = new File(nfsDirectory, backupDir);

      // Check if the directory exists
      if (!targetDir.exists() || !targetDir.isDirectory()) {
        log.warn(
            "Backup directory {} does not exist or is not a directory.",
            targetDir.getAbsolutePath());
        return true;
      }

      // Define the backup file pattern
      Pattern backupPattern = Pattern.compile("backup_.*\\.tgz");

      // Collect and sort files by last modified date (most recent first)
      List<File> sortedBackups =
          Arrays.stream(Objects.requireNonNull(targetDir.listFiles()))
              .filter(file -> backupPattern.matcher(file.getName()).find())
              .sorted((f1, f2) -> Long.compare(f2.lastModified(), f1.lastModified()))
              .collect(Collectors.toList());

      // Fetch the number of backups to retain
      int numKeepBackups =
          runtimeConfGetter.getGlobalConf(GlobalConfKeys.numCloudYbaBackupsRetention);

      if (sortedBackups.size() <= numKeepBackups) {
        log.info(
            "No backups to delete, only {} backups in {} less than limit {}",
            sortedBackups.size(),
            targetDir.getAbsolutePath(),
            numKeepBackups);
        return true;
      }

      // Identify backups to delete
      List<File> backupsToDelete = sortedBackups.subList(numKeepBackups, sortedBackups.size());

      // Delete old backups
      for (File backup : backupsToDelete) {
        if (!backup.delete()) {
          log.warn("Failed to delete backup: {}", backup.getAbsolutePath());
          return false;
        }
      }

      log.info(
          "Deleted {} old backup(s) from {}", backupsToDelete.size(), targetDir.getAbsolutePath());
    } catch (Exception e) {
      log.warn(
          "Unexpected exception while attempting to clean up NFS YBA backup: {}",
          e.getMessage(),
          e);
      return false;
    }
    return true;
  }

  @Override
  public File downloadYbaBackup(CustomerConfigData configData, String backupDir, Path localDir) {
    try {
      // Get the NFS backup location
      String nfsDirectory = ((CustomerConfigStorageNFSData) configData).backupLocation;

      // Construct the full path to the backup directory on NFS
      File nfsBackupDir = new File(nfsDirectory, backupDir);

      // Check if the backup directory exists and is valid
      if (!nfsBackupDir.exists() || !nfsBackupDir.isDirectory()) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "Backup directory "
                + nfsBackupDir.getAbsolutePath()
                + " does not exist or is not a directory");
      }

      // Define the backup file pattern
      Pattern backupPattern = Pattern.compile("backup_.*\\.tgz");

      // Find the latest backup file in the directory
      File latestBackup =
          Arrays.stream(Objects.requireNonNull(nfsBackupDir.listFiles()))
              .filter(file -> backupPattern.matcher(file.getName()).find())
              .max(Comparator.comparingLong(File::lastModified))
              .orElse(null);

      if (latestBackup == null) {
        throw new PlatformServiceException(
            BAD_REQUEST, "No backup files found in directory " + nfsBackupDir.getAbsolutePath());
      }

      if (!runtimeConfGetter.getGlobalConf(GlobalConfKeys.allowYbaRestoreWithOldBackup)) {
        if (latestBackup.lastModified() < System.currentTimeMillis() - (1000 * 60 * 60 * 24)) {
          throw new PlatformServiceException(
              BAD_REQUEST,
              "YB Anywhere restore is not allowed when backup file is more than 1 day old, enable"
                  + " runtime flag yb.yba_backup.allow_restore_with_old_backup to continue");
        }
      }

      // Ensure the local directory exists
      if (!localDir.toFile().exists() && !localDir.toFile().mkdirs()) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR,
            "Failed to create local directory: " + localDir.toAbsolutePath());
      }

      // Copy the backup file to the local directory
      File localBackupFile = localDir.resolve(latestBackup.getName()).toFile();
      Files.copy(
          latestBackup.toPath(), localBackupFile.toPath(), StandardCopyOption.REPLACE_EXISTING);

      log.info(
          "Copied backup {} to {}",
          latestBackup.getAbsolutePath(),
          localBackupFile.getAbsolutePath());
      return localBackupFile;

    } catch (IOException e) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "IO error occurred while copying backup: " + e.getMessage());
    }
  }

  @Override
  public boolean uploadYBDBRelease(
      CustomerConfigData configData, File release, String backupDir, String version) {
    try {
      // Get the NFS backup location
      String nfsDirectory = ((CustomerConfigStorageNFSData) configData).backupLocation;

      // Construct the destination path for the release file
      Path destinationDir = Paths.get(nfsDirectory, backupDir, "ybdb_releases", version);

      // Ensure the destination directory exists
      File destinationDirFile = destinationDir.toFile();
      if (!destinationDirFile.exists() && !destinationDirFile.mkdirs()) {
        log.error("Failed to create destination directory: {}", destinationDir.toAbsolutePath());
        return false;
      }

      // Construct the full destination file path
      Path destinationFilePath = destinationDir.resolve(release.getName());

      long startTime = System.nanoTime();

      // Copy the release file to the destination
      Files.copy(release.toPath(), destinationFilePath, StandardCopyOption.REPLACE_EXISTING);

      long endTime = System.nanoTime();

      // Calculate duration in seconds
      double durationInSeconds = (endTime - startTime) / 1_000_000_000.0;
      log.info(
          "Copy of {} to NFS path {} completed in {} seconds",
          release.getName(),
          destinationFilePath.toAbsolutePath(),
          durationInSeconds);

    } catch (IOException e) {
      log.error("IO error copying YBDB release {}: {}", release.getName(), e);
      return false;
    } catch (Exception e) {
      log.error("Unexpected error while copying YBDB release {}: {}", release.getName(), e);
      return false;
    }
    return true;
  }

  @Override
  public Set<String> getRemoteReleaseVersions(CustomerConfigData configData, String backupDir) {
    try {
      // Get the NFS backup location
      String nfsDirectory = ((CustomerConfigStorageNFSData) configData).backupLocation;

      // Construct the path to the directory containing releases
      Path releasesDir = Paths.get(nfsDirectory, backupDir, YBDB_RELEASES);

      Set<String> releaseVersions = new HashSet<>();

      // Ensure the releases directory exists
      File releasesDirFile = releasesDir.toFile();
      if (!releasesDirFile.exists() || !releasesDirFile.isDirectory()) {
        log.warn("Releases directory does not exist or is not a directory: {}", releasesDir);
        return releaseVersions;
      }

      // List all files and directories in the releases directory
      File[] files = releasesDirFile.listFiles();
      if (files == null) {
        log.warn("No files or directories found in NFS location: {}", releasesDir);
        return releaseVersions;
      }

      for (File file : files) {
        if (file.isDirectory()) {
          String relativePath = Paths.get(nfsDirectory).relativize(file.toPath()).toString();
          String version = extractReleaseVersion(relativePath, backupDir, "");
          if (version != null) {
            releaseVersions.add(version);
          } else {
            log.debug("Skipping non-version directory: {}", file.getName());
          }
        }
      }

      return releaseVersions;
    } catch (Exception e) {
      log.error("Unexpected exception while listing releases in NFS: {}", e.getMessage(), e);
    }
    return new HashSet<>();
  }

  @Override
  public boolean downloadRemoteReleases(
      CustomerConfigData configData,
      Set<String> releaseVersions,
      String releasesPath,
      String backupDir) {
    for (String version : releaseVersions) {
      Path versionPath;
      try {
        // Create the local directory for the release version
        versionPath = Files.createDirectories(Path.of(releasesPath, version));
      } catch (Exception e) {
        log.error(
            "Error creating local releases directory for version {}: {}", version, e.getMessage());
        return false;
      }

      try {
        // Get the NFS backup location
        String nfsDirectory = ((CustomerConfigStorageNFSData) configData).backupLocation;

        // Construct the NFS directory path for the current version
        Path nfsVersionPath = Path.of(nfsDirectory, backupDir, YBDB_RELEASES, version);

        // Validate the NFS version directory exists
        File nfsVersionDirFile = nfsVersionPath.toFile();
        if (!nfsVersionDirFile.exists() || !nfsVersionDirFile.isDirectory()) {
          log.warn("Version directory does not exist on NFS: {}", nfsVersionPath);
          continue;
        }

        // List all files in the NFS version directory
        File[] files = nfsVersionDirFile.listFiles();
        if (files == null || files.length == 0) {
          log.warn("No files found in NFS directory: {}", nfsVersionPath);
          continue;
        }

        // Copy each release file from NFS to the local directory
        for (File file : files) {
          if (file.isFile()) {
            Path localRelease = versionPath.resolve(file.getName());
            log.info(
                "Copying release from NFS path {} to local path {}",
                file.getAbsolutePath(),
                localRelease);
            Files.copy(file.toPath(), localRelease, StandardCopyOption.REPLACE_EXISTING);
          }
        }

      } catch (IOException e) {
        log.error(
            "Error occurred while copying release files for version {}: {}",
            version,
            e.getMessage());
        return false;
      } catch (Exception e) {
        log.error("Unexpected exception while processing version {}: {}", version, e.getMessage());
        return false;
      }
    }

    return true;
  }
}
