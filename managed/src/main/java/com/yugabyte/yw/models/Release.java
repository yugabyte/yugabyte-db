package com.yugabyte.yw.models;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.controllers.apiModels.CreateRelease;
import io.ebean.DB;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.Transaction;
import io.ebean.annotation.EnumValue;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.EnumType;
import jakarta.persistence.Enumerated;
import jakarta.persistence.Id;
import java.text.DateFormat;
import java.text.ParseException;
import java.util.Date;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import lombok.Getter;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;

@Entity
@Getter
@Setter
@Slf4j
public class Release extends Model {
  @Id private UUID releaseUUID;

  @Column(nullable = false)
  private String version;

  private String releaseTag;

  public enum YbType {
    @EnumValue("yb-db")
    YBDB
  }

  @Enumerated(EnumType.STRING)
  @Column(nullable = false)
  private YbType yb_type;

  private Date releaseDate;

  private String releaseNotes;

  @Column(nullable = false)
  private String releaseType;

  public enum ReleaseState {
    ACTIVE,
    DISABLED,
    DELETED
  }

  @Enumerated(EnumType.STRING)
  @Column(nullable = false)
  private ReleaseState state;

  public static final Finder<UUID, Release> find = new Finder<>(Release.class);

  public static Release create(String version, String releaseType) {
    return create(UUID.randomUUID(), version, releaseType);
  }

  public static Release create(UUID releaseUUID, String version, String releaseType) {
    Release release = new Release();
    release.releaseUUID = releaseUUID;
    release.version = version;
    release.releaseType = releaseType;
    release.yb_type = YbType.YBDB;
    release.state = ReleaseState.ACTIVE;
    release.save();
    return release;
  }

  public static Release createFromRequest(CreateRelease reqRelease) {
    Release release = new Release();
    // Generate UUID if one was not provided.
    if (reqRelease.release_uuid != null) {
      release.releaseUUID = reqRelease.release_uuid;
    } else {
      release.releaseUUID = UUID.randomUUID();
    }

    // Required fields
    release.version = reqRelease.version;
    release.releaseType = reqRelease.release_type;
    release.yb_type = YbType.valueOf(reqRelease.yb_type);

    // Optional fields
    release.releaseTag = reqRelease.release_tag;
    if (reqRelease.release_date != null) {
      DateFormat df = DateFormat.getDateInstance();
      try {
        release.releaseDate = df.parse(reqRelease.release_date);
      } catch (ParseException e) {
        log.warn("unable to parse date format", e);
      }
    }
    release.releaseNotes = reqRelease.release_notes;
    release.state = ReleaseState.ACTIVE;

    release.save();
    return release;
  }

  public static Release get(UUID uuid) {
    return find.byId(uuid);
  }

  public static List<Release> getAll() {
    return find.all();
  }

  public static Release getOrBadRequest(UUID releaseUUID) {
    Release release = get(releaseUUID);
    if (release == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Invalid Release UUID: " + releaseUUID.toString());
    }
    return release;
  }

  public static Release getByVersion(String version) {
    // TODO: Need to map between version and tag.
    return find.query().where().eq("version", version).findOne();
  }

  public void addArtifact(ReleaseArtifact artifact) {
    artifact.setReleaseUUID(releaseUUID);
  }

  public List<ReleaseArtifact> getArtifacts() {
    return ReleaseArtifact.getForRelease(releaseUUID);
  }

  public ReleaseArtifact getArtifactForArchitecture(Architecture arch) {
    return ReleaseArtifact.getForReleaseArchitecture(releaseUUID, arch);
  }

  public ReleaseArtifact getKubernetesArtifact() {
    return ReleaseArtifact.getForReleaseKubernetesArtifact(releaseUUID);
  }

  public void setReleaseTag(String tag) {
    this.releaseTag = tag;
    save();
  }

  public void setReleaseDate(Date date) {
    this.releaseDate = date;
    save();
  }

  public void setReleaseNotes(String notes) {
    this.releaseNotes = notes;
    save();
  }

  public void setState(ReleaseState state) {
    this.state = state;
    save();
  }

  @Override
  public boolean delete() {
    try (Transaction transaction = DB.beginTransaction()) {
      for (ReleaseArtifact artifact : getArtifacts()) {
        if (artifact.getPackageFileID() != null) {
          ReleaseLocalFile rlf = ReleaseLocalFile.get(artifact.getPackageFileID());
          if (!rlf.delete()) {
            return false;
          }
        }
        log.debug("cascading delete to artifact {}", artifact.getArtifactUUID());
        if (!artifact.delete()) {
          return false;
        }
      }
      if (!super.delete()) {
        return false;
      }
      transaction.commit();
    }
    return true;
  }

  public Set<Universe> getUniverses() {
    String formattedVersion = this.version;
    return Universe.universeDetailsIfReleaseExists(formattedVersion);
  }
}
