package com.yugabyte.yw.models;

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
}
