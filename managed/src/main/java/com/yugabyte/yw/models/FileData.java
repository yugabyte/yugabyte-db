// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.models;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.AccessManager;
import com.yugabyte.yw.common.AppConfigHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.inject.StaticInjectorHolder;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.CreatedTimestamp;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.attribute.PosixFilePermission;
import java.nio.file.attribute.PosixFilePermissions;
import java.util.Base64;
import java.util.Date;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import javax.persistence.EmbeddedId;
import javax.persistence.Entity;
import lombok.Data;
import lombok.EqualsAndHashCode;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.io.FileUtils;
import org.apache.commons.io.FilenameUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.validation.Constraints;

@Entity
@Data
@EqualsAndHashCode(callSuper = false)
public class FileData extends Model {

  public static final Logger LOG = LoggerFactory.getLogger(FileData.class);
  private static final String PUBLIC_KEY_EXTENSION = "pub";

  private static final String UUID_PATTERN =
      "[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}";

  @EmbeddedId private FileDataId file;

  @Constraints.Required private UUID parentUUID;
  // The task creation time.
  @CreatedTimestamp private Date timestamp;

  public String getRelativePath() {
    return this.file.filePath;
  }

  @Constraints.Required private String fileContent;

  public FileData() {
    this.timestamp = new Date();
  }

  private static final Finder<UUID, FileData> find = new Finder<UUID, FileData>(FileData.class) {};

  public static void create(
      UUID parentUUID, String filePath, String fileExtension, String fileContent) {
    FileData entry = new FileData();
    entry.parentUUID = parentUUID;
    entry.file = new FileDataId(filePath, fileExtension);
    entry.fileContent = fileContent;
    entry.save();
  }

  public static List<FileData> getFromParentUUID(UUID parentUUID) {
    return find.query().where().eq("parent_uuid", parentUUID).findList();
  }

  public static FileData getFromFile(String file) {
    return find.query().where().eq("file_path", file).findOne();
  }

  public static List<FileData> getAll() {
    return find.query().findList();
  }

  public static Set<FileData> getAllNames() {
    Set<FileData> fileData = find.query().select("file").findSet();
    if (CollectionUtils.isNotEmpty(fileData)) {
      return fileData;
    }
    return new HashSet<>();
  }

  public static void writeFileToDB(String file) {
    RuntimeConfGetter confGetter =
        StaticInjectorHolder.injector().instanceOf(RuntimeConfGetter.class);
    writeFileToDB(file, AppConfigHelper.getStoragePath(), confGetter);
  }

  public static void writeFileToDB(
      String file, String storagePath, RuntimeConfGetter runtimeConfGetter) {
    try {
      int fileCountThreshold =
          runtimeConfGetter.getGlobalConf(GlobalConfKeys.fsStatelessMaxFilesCountPersist);

      File f = new File(file);
      validateFileSize(f, runtimeConfGetter);

      List<FileData> dbFiles = getAll();
      int currentFileCountDB = dbFiles.size();
      if (currentFileCountDB == fileCountThreshold) {
        throw new RuntimeException(
            "The Maximum files count to be persisted in the DB exceeded the "
                + "configuration. Update the flag `yb.fs_stateless.max_files_count_persist` "
                + "to update the limit or try deleting some files");
      }

      Matcher parentUUIDMatcher = Pattern.compile(UUID_PATTERN).matcher(file);
      UUID parentUUID = null;
      if (parentUUIDMatcher.find()) {
        parentUUID = UUID.fromString((parentUUIDMatcher.group()));
        // Retrieve the last occurrence.
        while (parentUUIDMatcher.find()) {
          parentUUID = UUID.fromString(parentUUIDMatcher.group());
        }
      } else {
        LOG.warn(String.format("File %s is missing parent identifier.", file));
      }

      String filePath = f.getAbsolutePath();
      String fileExtension = FilenameUtils.getExtension(filePath);
      // We just need the path relative to the storage as storage path could change later.
      filePath = filePath.replace(storagePath, "");
      String content = Base64.getEncoder().encodeToString(Files.readAllBytes(Paths.get(file)));
      FileData.create(parentUUID, filePath, fileExtension, content);
    } catch (IOException e) {
      throw new RuntimeException("File " + file + " could not be written to DB.");
    }
  }

  private static void validateFileSize(File f, RuntimeConfGetter runtimeConfGetter) {
    long maxAllowedFileSize =
        runtimeConfGetter.getGlobalConf(GlobalConfKeys.fsStatelessMaxFileSizeBytes);
    if (f.exists()) {
      if (maxAllowedFileSize < f.length()) {
        String msg =
            "The File size is too big. Check the file or try updating the flag "
                + "`yb.fs_stateless.max_file_size_bytes` for updating the limit";
        throw new PlatformServiceException(BAD_REQUEST, msg);
      }
    }
  }

  public static void writeFileToDisk(FileData fileData, String storagePath) {
    String relativeFilePath = fileData.getRelativePath();
    Path directoryPath =
        Paths.get(storagePath, relativeFilePath.substring(0, relativeFilePath.lastIndexOf("/")));
    if (storagePath == null) {
      storagePath = AppConfigHelper.getStoragePath();
    }
    Path absoluteFilePath = Paths.get(storagePath, relativeFilePath);
    Util.getOrCreateDir(directoryPath);
    byte[] fileContent = Base64.getDecoder().decode(fileData.getFileContent().getBytes());
    try {
      Files.write(absoluteFilePath, fileContent);
      Set<PosixFilePermission> permissions =
          PosixFilePermissions.fromString(AccessManager.PEM_PERMISSIONS);
      if (fileData.getFile().fileExtension.equals(PUBLIC_KEY_EXTENSION)) {
        permissions = PosixFilePermissions.fromString(AccessManager.PUB_PERMISSIONS);
      }
      Files.setPosixFilePermissions(absoluteFilePath, permissions);
    } catch (IOException e) {
      throw new RuntimeException("Could not write to file: " + fileData.getRelativePath(), e);
    }
    return;
  }

  public static void deleteFiles(String dirPath, Boolean deleteDiskDirectory) {
    if (dirPath == null) {
      return;
    }

    String storagePath = AppConfigHelper.getStoragePath();
    String relativeDirPath = dirPath.replace(storagePath, "");
    File directory = new File(dirPath);

    for (final File fileEntry : directory.listFiles()) {
      if (fileEntry.isDirectory()) {
        deleteFiles(dirPath + File.separator + fileEntry.getName(), deleteDiskDirectory);
        continue;
      }
      FileData file = FileData.getFromFile(relativeDirPath + File.separator + fileEntry.getName());
      if (file != null) {
        file.delete();
      }
    }
    if (deleteDiskDirectory && directory.isDirectory()) {
      try {
        FileUtils.deleteDirectory(directory);
      } catch (IOException e) {
        LOG.error("Failed to delete directory: " + directory + " with error: ", e);
      }
    }
  }

  public static boolean deleteFileFromDB(String filePath) {
    filePath = getRelativePath(filePath);
    FileData fileData = getFromFile(filePath);
    if (fileData == null) return false;
    return fileData.delete();
  }

  public static void addToBackup(List<String> filesToBackup) throws IOException {
    if (filesToBackup == null) return;
    for (String filePath : filesToBackup) {
      FileData.upsertFileInDB(filePath);
    }
  }

  /**
   * Updates the latest record if there is at least one with the specified filePath already, else
   * inserts the record.
   *
   * @param filePath
   * @return true if updated, false if inserted
   * @throws IOException
   */
  public static boolean upsertFileInDB(String filePath) throws IOException {
    String relativeFilePath = getRelativePath(filePath);
    FileData fileData = getLatest(relativeFilePath);
    if (fileData == null) writeFileToDB(filePath);
    else {
      File file = new File(filePath);
      RuntimeConfGetter confGetter =
          StaticInjectorHolder.injector().instanceOf(RuntimeConfGetter.class);
      validateFileSize(file, confGetter);
      fileData.setFileContent(getContents(Files.readAllBytes(new File(filePath).toPath())));
      fileData.setTimestamp(new Date());
      fileData.update();
      return true;
    }
    return false;
  }

  public static FileData getLatest(String fileRelativePath) {
    return find.query()
        .where()
        .eq("file_path", fileRelativePath)
        .order()
        .desc("timestamp")
        .setMaxRows(1)
        .findOne();
  }

  private static String getRelativePath(String filePath) {
    return filePath.replace(AppConfigHelper.getStoragePath(), "");
  }

  private static String getContents(byte[] fileContents) {
    return Base64.getEncoder().encodeToString(fileContents);
  }

  public static byte[] getDecodedData(String filePath) {
    String relFilePath = getRelativePath(filePath);
    FileData fileData = FileData.getFromFile(relFilePath);
    byte[] decodedData = Base64.getDecoder().decode(fileData.getFileContent().getBytes());
    return decodedData;
  }
}
