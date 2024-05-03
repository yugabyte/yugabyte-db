package com.yugabyte.yw.models;

import static play.mvc.Http.Status.BAD_REQUEST;

import autovalue.shaded.com.google.common.annotations.VisibleForTesting;
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
import jakarta.persistence.NonUniqueResultException;
import java.time.DateTimeException;
import java.time.Instant;
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
  // on pg14, we can't make a null value unique, so instead use this hard-coded constant
  public static final String NULL_CONSTANT = "NULL-VALUE-DO-NOT-USE-AS-INPUT";

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
    INCOMPLETE,
    DELETED
  }

  @Enumerated(EnumType.STRING)
  @Column(nullable = false)
  private ReleaseState state;

  public static final Finder<UUID, Release> find = new Finder<>(Release.class);

  public static Release create(String version, String releaseType) {
    return create(UUID.randomUUID(), version, releaseType, null);
  }

  public static Release create(String version, String releaseType, String releaseTag) {
    return create(UUID.randomUUID(), version, releaseType, releaseTag);
  }

  public static Release create(UUID releaseUUID, String version, String releaseType) {
    return create(releaseUUID, version, releaseType, null);
  }

  public static Release create(
      UUID releaseUUID, String version, String releaseType, String releaseTag) {
    if (Release.getByVersion(version) != null) {
      String tagError = "";
      if (releaseTag != null && !releaseTag.isEmpty()) {
        tagError = " with tag " + releaseTag;
      }
      throw new PlatformServiceException(
          BAD_REQUEST, String.format("release version %s%s already exists", version, tagError));
    }
    Release release = new Release();
    release.releaseUUID = releaseUUID;
    release.version = version;
    release.releaseTag = encodeReleaseTag(releaseTag);
    release.releaseType = releaseType;
    release.yb_type = YbType.YBDB;
    release.state = ReleaseState.INCOMPLETE;
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
    // Validate the release doesn't already exist - either by uuid or verison/tag combo
    if (Release.get(release.releaseUUID) != null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "release with uuid " + release.releaseUUID + " already exists");
    }
    if (Release.getByVersion(reqRelease.version) != null) {
      String tagError = "";
      if (reqRelease.release_tag != null && !reqRelease.release_tag.isEmpty()) {
        tagError = " with tag " + reqRelease.release_tag;
      }
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "release version %s with tag %s already exists", reqRelease.version, tagError));
    }

    // Required fields
    release.version = reqRelease.version;
    release.releaseType = reqRelease.release_type;
    release.yb_type = YbType.valueOf(reqRelease.yb_type);

    // Optional fields
    release.releaseTag = encodeReleaseTag(reqRelease.release_tag);
    if (reqRelease.release_date_msecs != null) {
      try {
        release.releaseDate = Date.from(Instant.ofEpochMilli(reqRelease.release_date_msecs));
      } catch (IllegalArgumentException | DateTimeException e) {
        log.warn("unable to parse date format", e);
      }
    }
    release.releaseNotes = reqRelease.release_notes;
    release.state = ReleaseState.INCOMPLETE;

    release.save();
    return release;
  }

  public static Release get(UUID uuid) {
    return find.byId(uuid);
  }

  public static List<Release> getAll() {
    return find.all();
  }

  public static List<Release> getAllWithArtifactType(
      ReleaseArtifact.Platform plat, Architecture arch) {
    List<ReleaseArtifact> artifacts = ReleaseArtifact.getAllPlatformArchitecture(plat, arch);
    return find.query()
        .where()
        .idIn(artifacts.stream().map(a -> a.getReleaseUUID()).toArray())
        .findList();
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
    // We are currently only allowing 1 Release of a given version, even with a tag.
    // If that changes, we should go back to using the bellow line instead of the query statement.
    // return Release.getByVersion(version, null);
    try {
      return find.query().where().eq("version", version).findOne();
    } catch (NonUniqueResultException e) {
      log.warn("Found multiple releases for version {}", version);
      // This is safe, as we have just discovered that multiple releases with that version exist
      return find.query().where().eq("version", version).findList().get(0);
    }
  }

  public static Release getByVersion(String version, String tag) {
    tag = encodeReleaseTag(tag);
    return find.query().where().eq("version", version).eq("release_tag", tag).findOne();
  }

  public void addArtifact(ReleaseArtifact artifact) {
    if (ReleaseArtifact.getForReleaseMatchingType(
            releaseUUID, artifact.getPlatform(), artifact.getArchitecture())
        != null) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "artifact matching platform %s and architecture %s already exists",
              artifact.getPlatform(), artifact.getArchitecture()));
    }
    artifact.saveReleaseUUID(releaseUUID);

    // Move the state from incomplete to active when adding a Linux type. Kubernetes artifacts
    // are not sufficient to make a release move into the "active" state.
    if (artifact.getPlatform() == ReleaseArtifact.Platform.LINUX
        && this.state == ReleaseState.INCOMPLETE) {
      state = ReleaseState.ACTIVE;
      save();
    }
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

  public void saveReleaseTag(String tag) {
    this.releaseTag = encodeReleaseTag(tag);
    save();
  }

  public String getReleaseTag() {
    return decodeReleaseTag(this.releaseTag);
  }

  // Mainly used for test validation, please use `getReleaseTag()` outside of unit tests.
  @VisibleForTesting
  public String getRawReleaseTag() {
    return releaseTag;
  }

  public void saveReleaseDate(Date date) {
    this.releaseDate = date;
    save();
  }

  public void saveReleaseNotes(String notes) {
    this.releaseNotes = notes;
    save();
  }

  public void setState(ReleaseState state) {
    if (this.state == ReleaseState.INCOMPLETE) {
      throw new PlatformServiceException(
          BAD_REQUEST, "cannot update release state from 'INCOMPLETE'");
    }
    this.state = state;
  }

  public void saveState(ReleaseState state) {
    setState(state);
    save();
  }

  @Override
  public boolean delete() {
    try (Transaction transaction = DB.beginTransaction()) {
      for (ReleaseArtifact artifact : getArtifacts()) {
        ReleaseLocalFile rlf = null;
        if (artifact.getPackageFileID() != null) {
          rlf = ReleaseLocalFile.get(artifact.getPackageFileID());
        }
        log.debug(
            "Release {}: cascading delete to artifact {}", releaseUUID, artifact.getArtifactUUID());
        if (!artifact.delete()) {
          log.error(
              String.format(
                  "Release %s: failed to delete artifact %s",
                  releaseUUID, artifact.getArtifactUUID()));
          return false;
        }
        if (rlf != null && !rlf.delete()) {
          log.error(
              String.format(
                  "Release %s: failed to delete ReleaseLocalFile %s:%s",
                  releaseUUID, rlf.getFileUUID(), rlf.getLocalFilePath()));
          return false;
        }
      }
      if (!super.delete()) {
        log.error("failed to delete release " + releaseUUID);
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

  private static String encodeReleaseTag(String releaseTag) {
    if (releaseTag == null || releaseTag.isEmpty()) {
      return NULL_CONSTANT;
    } else if (releaseTag.equals(NULL_CONSTANT)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "cannot set release tag to " + Release.NULL_CONSTANT);
    }
    return releaseTag;
  }

  private static String decodeReleaseTag(String releaseTag) {
    if (releaseTag == null || releaseTag.equals(NULL_CONSTANT)) {
      return null;
    }
    return releaseTag;
  }
}
