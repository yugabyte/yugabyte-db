package com.yugabyte.yw.models;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.PlatformServiceException;
import io.ebean.Finder;
import io.ebean.Model;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import java.util.UUID;
import lombok.Getter;
import lombok.Setter;

@Entity
@Getter
@Setter
public class ReleaseLocalFile extends Model {
  @Id private UUID fileUUID;

  @Column private String localFilePath;

  public static final Finder<UUID, ReleaseLocalFile> find = new Finder<>(ReleaseLocalFile.class);

  public static ReleaseLocalFile create(String localFilePath) {
    return ReleaseLocalFile.create(UUID.randomUUID(), localFilePath);
  }

  public static ReleaseLocalFile create(UUID fileUUID, String localFilePath) {
    ReleaseLocalFile rlf = new ReleaseLocalFile();
    rlf.fileUUID = fileUUID;
    rlf.localFilePath = localFilePath;
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
}
