package com.yugabyte.yw.models;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.common.PlatformServiceException;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.EnumValue;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.EnumType;
import jakarta.persistence.Enumerated;
import jakarta.persistence.Id;
import java.util.List;
import java.util.UUID;
import lombok.Getter;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Slf4j
@Getter
@Setter
@Entity
public class ReleaseArtifact extends Model {
  @Id private UUID artifactUUID = UUID.randomUUID();

  @Column(name = "release")
  private UUID releaseUUID;

  @Column private String sha256;

  public enum Platform {
    @EnumValue("linux")
    LINUX,
    @EnumValue("kubernetes")
    KUBERNETES;
  }

  @Column private Platform platform;

  @Column
  @Enumerated(EnumType.STRING)
  private PublicCloudConstants.Architecture architecture;

  @Column private String signature;

  @Column private UUID packageFileID;

  public void savePackageFileID(UUID fileID) {
    this.packageFileID = fileID;
    save();
  }

  @Column private String packageURL;

  public void savePackageURL(String url) {
    this.packageURL = url;
    save();
  }

  public static class GCSFile {
    public String path;
    public String credentialsJson;
  }

  private GCSFile gcsFile;

  @Column(name = "gcs_file")
  private String gcsFileJson;

  public void saveGCSFile(GCSFile gcsFile) {
    this.gcsFileJson = Json.stringify(Json.toJson(gcsFile));
    this.gcsFile = gcsFile;
    save();
  }

  public static class S3File {
    public String path;
    public String accessKeyId;
    public String secretAccessKey;
  }

  private S3File s3File;

  @Column(name = "s3_file")
  private String s3FileJson;

  public void saveS3File(S3File s3File) {
    this.s3FileJson = Json.stringify(Json.toJson(s3File));
    this.s3File = s3File;
    save();
  }

  public static final Finder<UUID, ReleaseArtifact> find = new Finder<>(ReleaseArtifact.class);

  public static ReleaseArtifact create(
      String sha256,
      Platform platform,
      PublicCloudConstants.Architecture architecture,
      UUID packageFileID) {
    return create(sha256, platform, architecture, packageFileID, null);
  }

  public static ReleaseArtifact create(
      String sha256,
      Platform platform,
      PublicCloudConstants.Architecture architecture,
      UUID packageFileID,
      String signature) {
    if (!validatePlatformArchitecture(platform, architecture)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format("invalid platform/architecture pair %s-%s", platform, architecture));
    }
    ReleaseArtifact artifact = new ReleaseArtifact();
    artifact.sha256 = ReleaseArtifact.sha256Format(sha256);
    artifact.platform = platform;
    artifact.architecture = architecture;
    artifact.packageFileID = packageFileID;
    artifact.signature = signature;
    artifact.save();
    return artifact;
  }

  public static ReleaseArtifact create(
      String sha256,
      Platform platform,
      PublicCloudConstants.Architecture architecture,
      String packageURL) {
    return create(sha256, platform, architecture, packageURL, null);
  }

  public static ReleaseArtifact create(
      String sha256,
      Platform platform,
      PublicCloudConstants.Architecture architecture,
      String packageURL,
      String signature) {
    if (!validatePlatformArchitecture(platform, architecture)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format("invalid platform/architecture pair %s-%s", platform, architecture));
    }
    ReleaseArtifact artifact = new ReleaseArtifact();
    artifact.sha256 = sha256Format(sha256);
    artifact.platform = platform;
    artifact.architecture = architecture;
    artifact.packageURL = packageURL;
    artifact.signature = signature;
    artifact.save();
    return artifact;
  }

  public static ReleaseArtifact create(
      String sha256,
      Platform platform,
      PublicCloudConstants.Architecture architecture,
      GCSFile gcsFile) {
    return create(sha256, platform, architecture, gcsFile, null);
  }

  public static ReleaseArtifact create(
      String sha256,
      Platform platform,
      PublicCloudConstants.Architecture architecture,
      GCSFile gcsFile,
      String signature) {
    if (!validatePlatformArchitecture(platform, architecture)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format("invalid platform/architecture pair %s-%s", platform, architecture));
    }
    ReleaseArtifact artifact = new ReleaseArtifact();
    artifact.sha256 = sha256Format(sha256);
    artifact.platform = platform;
    artifact.architecture = architecture;
    artifact.gcsFile = gcsFile;
    artifact.gcsFileJson = Json.stringify(Json.toJson(gcsFile));
    artifact.signature = signature;
    artifact.save();
    return artifact;
  }

  public static ReleaseArtifact create(
      String sha256,
      Platform platform,
      PublicCloudConstants.Architecture architecture,
      S3File s3File) {
    return create(sha256, platform, architecture, s3File, null);
  }

  public static ReleaseArtifact create(
      String sha256,
      Platform platform,
      PublicCloudConstants.Architecture architecture,
      S3File s3File,
      String signature) {
    if (!validatePlatformArchitecture(platform, architecture)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format("invalid platform/architecture pair %s-%s", platform, architecture));
    }
    ReleaseArtifact artifact = new ReleaseArtifact();

    artifact.sha256 = ReleaseArtifact.sha256Format(sha256);
    artifact.platform = platform;
    artifact.architecture = architecture;
    artifact.s3File = s3File;
    artifact.s3FileJson = Json.stringify(Json.toJson(s3File));
    artifact.signature = signature;
    artifact.save();
    return artifact;
  }

  public static ReleaseArtifact get(UUID artifactUuid) {
    ReleaseArtifact artifact = find.byId(artifactUuid);
    if (artifact != null) {
      artifact.fillJsonText();
    }
    return artifact;
  }

  public static List<ReleaseArtifact> getForRelease(UUID releaseUUID) {
    List<ReleaseArtifact> artifacts = find.query().where().eq("release", releaseUUID).findList();
    artifacts.forEach(a -> a.fillJsonText());
    return artifacts;
  }

  public static ReleaseArtifact getForReleaseArchitecture(UUID releaseUUID, Architecture arch) {
    ReleaseArtifact artifact =
        find.query().where().eq("release", releaseUUID).eq("architecture", arch).findOne();
    if (artifact != null) {
      artifact.fillJsonText();
    }
    return artifact;
  }

  public static ReleaseArtifact getForReleaseKubernetesArtifact(UUID releaseUUID) {
    ReleaseArtifact artifact =
        find.query()
            .where()
            .eq("release", releaseUUID)
            .eq("platform", Platform.KUBERNETES)
            .findOne();
    if (artifact != null) {
      artifact.fillJsonText();
    }
    return artifact;
  }

  public static ReleaseArtifact getForReleaseMatchingType(
      UUID releaseUUID, Platform plat, Architecture arch) {
    ReleaseArtifact artifact =
        find.query()
            .where()
            .eq("release", releaseUUID)
            .eq("platform", plat)
            .eq("architecture", arch)
            .findOne();
    if (artifact != null) {
      artifact.fillJsonText();
    }
    return artifact;
  }

  public static List<ReleaseArtifact> getForReleaseLocalFile(UUID releaseUUID) {
    List<ReleaseArtifact> artifacts =
        find.query().where().eq("release", releaseUUID).isNotNull("package_file_id").findList();
    artifacts.forEach(a -> a.fillJsonText());
    return artifacts;
  }

  public static List<ReleaseArtifact> getAllPlatformArchitecture(
      Platform platform, Architecture architecture) {
    List<ReleaseArtifact> artifacts =
        find.query().where().eq("platform", platform).eq("architecture", architecture).findList();
    artifacts.forEach(a -> a.fillJsonText());
    return artifacts;
  }

  public void saveReleaseUUID(UUID releaseUuid) {
    this.releaseUUID = releaseUuid;
    save();
  }

  public void setSha256(String sha256) {
    // Shortcut setting to null;
    if (sha256 == null) {
      this.sha256 = sha256;
      return;
    }
    // This only happens when migrating from legacy releases, where md5 was supported.
    try {
      this.sha256 = sha256Format(sha256);
    } catch (RuntimeException e) {
      log.error("invalid sha256", e);
      return;
    }
  }

  public void saveSha256(String sha256) {
    setSha256(sha256);
    save();
  }

  // In most cases, please use getFormattedSha256. This is only useful when needing to check the
  // underlying sha256 value we store in the DB - mainly for testing on API update requests.
  public String getSha256() {
    return sha256;
  }

  // When using a release artifact with a universe, getFormattedSha256 should be used. It will
  // ensure the sha256 value is returned in the expected format for ansible and other libraries.
  public String getFormattedSha256() {
    // Legacy releases may use md5 or sha1 algorithms still, so when those releases are migrated,
    // we still need to handle that. They always come in the form md5: or sha1:
    // Because both md5 and sha1 are not good for checking the validity of the file, we will just
    // return null instead
    if (sha256 == null
        || sha256.toLowerCase().startsWith("md5:")
        || sha256.toLowerCase().startsWith("sha1:")) {
      return null;
    }
    if (!sha256.toLowerCase().startsWith("sha256:")) {
      return String.format("sha256:%s", sha256);
    }
    return sha256;
  }

  public boolean isKubernetes() {
    return platform.equals(Platform.KUBERNETES);
  }

  private static boolean validatePlatformArchitecture(
      Platform platform, PublicCloudConstants.Architecture architecture) {
    return (platform == Platform.LINUX && architecture != null)
        || (platform == Platform.KUBERNETES && architecture == null);
  }

  private void fillJsonText() {
    if (gcsFileJson != null) {
      gcsFile = Json.fromJson(Json.parse(gcsFileJson), GCSFile.class);
    }
    if (s3FileJson != null) {
      s3File = Json.fromJson(Json.parse(s3FileJson), S3File.class);
    }
  }

  private static String sha256Format(String sha256) {
    if (sha256 == null) {
      return sha256;
    }
    if (sha256.toLowerCase().startsWith("sha256:")) {
      sha256 = sha256.substring(7);
    }
    return sha256;
  }
}
