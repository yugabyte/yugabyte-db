// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.models;

import com.yugabyte.yw.common.AccessManager;
import com.yugabyte.yw.common.Util;

import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.CreatedTimestamp;

import org.apache.commons.io.FileUtils;

import java.util.Base64;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import java.util.HashSet;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.Set;

import org.apache.commons.collections.CollectionUtils;
import org.apache.commons.io.FilenameUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.File;
import java.nio.file.attribute.PosixFilePermission;
import java.nio.file.attribute.PosixFilePermissions;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;

import javax.persistence.Entity;
import javax.persistence.EmbeddedId;
import lombok.Data;
import lombok.EqualsAndHashCode;
import play.api.Play;
import play.data.validation.Constraints;

@Entity
@Data
@EqualsAndHashCode(callSuper = false)
public class FileData extends Model {

  public static final Logger LOG = LoggerFactory.getLogger(FileData.class);

  private static final String PUBLIC_KEY_EXTENSION = "pub";
  private static final String YB_STORAGE_PATH = "yb.storage.path";
  private static final String UUID_PATTERN =
      "[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}";

  @EmbeddedId public FileDataId file;

  @Constraints.Required private UUID parentUUID;

  public UUID getParentUUID() {
    return this.parentUUID;
  }

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

  /**
   * Create new FileData entry.
   *
   * @param parentUUID
   * @param filePath
   * @param fileExtension
   * @param fileContent
   * @return Newly Created FileData table entry.
   */
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

  public static String getStoragePath() {
    play.Configuration appConfig = Play.current().injector().instanceOf(play.Configuration.class);
    return appConfig.getString(YB_STORAGE_PATH);
  }

  public static void writeFileToDB(String file) {
    writeFileToDB(file, null);
  }

  public static void writeFileToDB(String file, String storagePath) {
    try {
      File f = new File(file);
      Matcher parentUUIDMatcher = Pattern.compile(UUID_PATTERN).matcher(file);
      if (!parentUUIDMatcher.find()) {
        throw new RuntimeException("File " + file + " is missing parent identifier.");
      }
      UUID parentUUID = UUID.fromString((parentUUIDMatcher.group()));
      // Retrieve the last occurence.
      while (parentUUIDMatcher.find()) {
        parentUUID = UUID.fromString(parentUUIDMatcher.group());
      }
      if (storagePath == null) {
        storagePath = getStoragePath();
      }
      String filePath = f.getAbsolutePath();
      String fileExtension = FilenameUtils.getExtension(filePath);
      // We just need the path relative to the storage since that can be changed
      // later.
      filePath = filePath.replace(storagePath, "");
      String content = Base64.getEncoder().encodeToString(Files.readAllBytes(Paths.get(file)));
      FileData.create(parentUUID, filePath, fileExtension, content);
    } catch (IOException e) {
      throw new RuntimeException("File " + file + " could not be written to DB.");
    }
  }

  public static void writeFileToDisk(FileData fileData) {
    writeFileToDisk(fileData, null);
  }

  public static void writeFileToDisk(FileData fileData, String storagePath) {
    String relativeFilePath = fileData.getRelativePath();
    Path directoryPath =
        Paths.get(storagePath, relativeFilePath.substring(0, relativeFilePath.lastIndexOf("/")));
    if (storagePath == null) {
      storagePath = getStoragePath();
    }
    Path absoluteFilePath = Paths.get(storagePath, relativeFilePath);
    Util.getOrCreateDir(directoryPath);
    byte[] fileContent = Base64.getDecoder().decode(fileData.getFileContent().getBytes());
    try {
      Files.write(absoluteFilePath, fileContent);
      Set<PosixFilePermission> permissions =
          PosixFilePermissions.fromString(AccessManager.PEM_PERMISSIONS);
      if (fileData.file.fileExtension.equals(PUBLIC_KEY_EXTENSION)) {
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

    String storagePath = getStoragePath();
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
}
