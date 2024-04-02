package com.yugabyte.yw.models;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.controllers.apiModels.CreateRelease;
import io.ebean.Finder;
import io.ebean.Model;
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
    if (reqRelease.release_uuid == null) {
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

  public void addArtifact(ReleaseArtifact artifact) {
    artifact.setReleaseUUID(releaseUUID);
  }

  public List<ReleaseArtifact> getArtifacts() {
    return ReleaseArtifact.getForRelease(releaseUUID);
  }
}
