package com.yugabyte.yw.common.services;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.Sets;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.ConfigHelper.ConfigType;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.FileData;
import java.io.File;
import java.util.Collection;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.FileUtils;

@Singleton
@Slf4j
public class FileDataService {

  private static final List<String> FILE_DIRECTORY_TO_SYNC =
      ImmutableList.of("keys", "certs", "licenses", "node-agent");

  private final RuntimeConfGetter confGetter;
  private final ConfigHelper configHelper;

  @Inject
  public FileDataService(RuntimeConfGetter confGetter, ConfigHelper configHelper) {
    this.confGetter = confGetter;
    this.configHelper = configHelper;
  }

  public void syncFileData(String storagePath, Boolean ywFileDataSynced) {
    Boolean suppressExceptionsDuringSync =
        confGetter.getGlobalConf(GlobalConfKeys.fsStatelessSuppressError);
    int fileCountThreshold =
        confGetter.getGlobalConf(GlobalConfKeys.fsStatelessMaxFilesCountPersist);
    Boolean disableSyncDBStateToFS =
        confGetter.getGlobalConf(GlobalConfKeys.disableSyncDbToFsStartup);
    try {
      long fileSyncStartTime = System.currentTimeMillis();
      Collection<File> diskFiles = Collections.emptyList();
      List<FileData> dbFiles = FileData.getAll();
      int currentFileCountDB = dbFiles.size();

      for (String fileDirectoryName : FILE_DIRECTORY_TO_SYNC) {
        File ywDir = new File(storagePath + "/" + fileDirectoryName);
        if (ywDir.exists()) {
          Collection<File> diskFile = FileUtils.listFiles(ywDir, null, true);
          diskFiles =
              Stream.of(diskFiles, diskFile)
                  .flatMap(Collection::stream)
                  .collect(Collectors.toList());
        }
      }
      // List of files on disk with relative path to storage.
      Set<String> filesOnDisk =
          diskFiles
              .stream()
              .map(File::getAbsolutePath)
              .map(fileName -> fileName.replace(storagePath, ""))
              .collect(Collectors.toSet());
      Set<FileData> fileDataNames = FileData.getAllNames();
      Set<String> filesInDB =
          fileDataNames.stream().map(FileData::getRelativePath).collect(Collectors.toSet());

      if (!ywFileDataSynced) {
        Set<String> fileOnlyOnDisk = Sets.difference(filesOnDisk, filesInDB);
        if (currentFileCountDB + fileOnlyOnDisk.size() > fileCountThreshold) {
          // We fail in case the count exceeds the threshold.
          throw new RuntimeException(
              "The Maximum files count to be persisted in the DB exceeded the "
                  + "configuration. Update the flag `yb.fs_stateless.max_files_count_persist` "
                  + "to update the limit or try deleting some files");
        }

        // For all files only on disk, update them in the DB.
        for (String file : fileOnlyOnDisk) {
          log.info("Syncing file " + file + " to the DB");
          FileData.writeFileToDB(storagePath + file, storagePath, confGetter);
        }
        log.info("Successfully Written " + fileOnlyOnDisk.size() + " files to DB.");
      }

      if (!disableSyncDBStateToFS) {
        Set<String> fileOnlyInDB = Sets.difference(filesInDB, filesOnDisk);
        // For all files only in the DB, write them to disk.
        for (String file : fileOnlyInDB) {
          log.info("Syncing " + file + " file from the DB to FS");
          FileData.writeFileToDisk(FileData.getFromFile(file), storagePath);
        }
        log.info("Successfully Written " + fileOnlyInDB.size() + " files to disk.");
      }

      if (!ywFileDataSynced) {
        Map<String, Object> ywFileDataSync = new HashMap<>();
        ywFileDataSync.put("synced", "true");
        configHelper.loadConfigToDB(ConfigType.FileDataSync, ywFileDataSync);
      }
      long fileSyncEndTime = System.currentTimeMillis();
      log.debug("Time taken for file sync operation: {}", fileSyncEndTime - fileSyncStartTime);
    } catch (Exception e) {
      if (suppressExceptionsDuringSync) {
        log.error(e.getMessage());
      } else {
        throw e;
      }
    }
  }
}
