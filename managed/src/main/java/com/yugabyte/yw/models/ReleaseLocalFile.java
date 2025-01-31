package com.yugabyte.yw.models;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.PlatformServiceException;
import io.ebean.Finder;
import io.ebean.Model;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import java.io.File;
import java.util.List;
import java.util.UUID;
import lombok.Getter;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Entity
@Getter
@Setter
public class ReleaseLocalFile extends Model {
  @Id private UUID fileUUID;

  @Column private String localFilePath;

  public void setLocalFilePath(String localFilePath) {
    this.localFilePath = localFilePath;
    save();
  }

  @Column private boolean isUpload = false;

  public static final Finder<UUID, ReleaseLocalFile> find = new Finder<>(ReleaseLocalFile.class);

  public static ReleaseLocalFile create(String localFilePath) {
    return ReleaseLocalFile.create(UUID.randomUUID(), localFilePath, false);
  }

  public static ReleaseLocalFile create(UUID fileUUID, String localFilePath, boolean isUpload) {
    ReleaseLocalFile rlf = new ReleaseLocalFile();
    rlf.fileUUID = fileUUID;
    rlf.localFilePath = localFilePath;
    rlf.isUpload = isUpload;
    rlf.save();
    return rlf;
  }

  public static ReleaseLocalFile get(UUID fileUUID) {
    return find.byId(fileUUID);
  }

  public static ReleaseLocalFile getOrBadRequest(UUID fileUUID) {
    ReleaseLocalFile rlf = ReleaseLocalFile.get(fileUUID);
    if (rlf == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Cannot find Release Local File " + fileUUID);
    }
    return rlf;
  }

  public static List<ReleaseLocalFile> getAll() {
    return find.all();
  }

  public static List<ReleaseLocalFile> getLocalFiles() {
    return find.query().where().eq("isUpload", false).findList();
  }

  public static List<ReleaseLocalFile> getUploadedFiles() {
    return find.query().where().eq("isUpload", true).findList();
  }

  public boolean delete() {
    File file = new File(this.localFilePath);
    // Best effort delete the file
    if (!file.delete()) {
      log.error("Failed to delete file {}", file);
    }
    return super.delete();
  }
}
