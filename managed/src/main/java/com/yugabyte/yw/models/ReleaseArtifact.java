package com.yugabyte.yw.models;

import com.yugabyte.yw.cloud.PublicCloudConstants;
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
import play.libs.Json;

@Entity
public class ReleaseArtifact extends Model {
  @Getter @Id private UUID artifactUUID = UUID.randomUUID();

  @Column(name = "release")
  @Getter
  private UUID releaseUUID;

  @Column @Getter private String sha256;

  public enum Platform {
    @EnumValue("linux")
    LINUX,
    @EnumValue("kubernetes")
    KUBERNETES;
  }

  @Column @Getter private Platform platform;

  @Column
  @Enumerated(EnumType.STRING)
  @Getter
  private PublicCloudConstants.Architecture architecture;

  @Column @Getter private String signature;

  @Column @Getter private UUID packageFileID;

  @Column @Getter private String packageURL;

  public static class GCSFile {
    public String path;
    public String credentialsJson;
  }

  @Getter private GCSFile gcsFile;

  @Column(name = "gcs_file")
  private String gcsFileJson;

  public void setGCSFile(GCSFile gcsFile) {
    this.gcsFileJson = Json.stringify(Json.toJson(gcsFile));
    this.gcsFile = gcsFile;
    save();
  }

  public static class S3File {
    public String path;
    public String accessKeyId;
    public String secretAccessKey;
  }

  @Getter private S3File s3File;

  @Column(name = "s3_file")
  private String s3FileJson;

  public void setS3File(S3File s3File) {
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
      throw new RuntimeException(
          String.format("invalid platform/architecture pair {}-{}", platform, architecture));
    }
    ReleaseArtifact artifact = new ReleaseArtifact();
    artifact.sha256 = sha256;
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
      throw new RuntimeException(
          String.format("invalid platform/architecture pair {}-{}", platform, architecture));
    }
    ReleaseArtifact artifact = new ReleaseArtifact();
    artifact.sha256 = sha256;
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
      throw new RuntimeException(
          String.format("invalid platform/architecture pair {}-{}", platform, architecture));
    }
    ReleaseArtifact artifact = new ReleaseArtifact();
    artifact.sha256 = sha256;
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
      throw new RuntimeException(
          String.format("invalid platform/architecture pair {}-{}", platform, architecture));
    }
    ReleaseArtifact artifact = new ReleaseArtifact();
    artifact.sha256 = sha256;
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
    if (artifact.gcsFileJson != null) {
      artifact.gcsFile = Json.fromJson(Json.parse(artifact.gcsFileJson), GCSFile.class);
    }
    if (artifact.s3FileJson != null) {
      artifact.s3File = Json.fromJson(Json.parse(artifact.s3FileJson), S3File.class);
    }
    return artifact;
  }

  public static List<ReleaseArtifact> getForRelease(UUID releaseUUID) {
    return find.query().where().eq("release", releaseUUID).findList();
  }

  public void setReleaseUUID(UUID releaseUuid) {
    this.releaseUUID = releaseUuid;
    save();
  }

  private static boolean validatePlatformArchitecture(
      Platform platform, PublicCloudConstants.Architecture architecture) {
    return (platform == Platform.LINUX && architecture != null)
        || (platform == Platform.KUBERNETES && architecture == null);
  }
}
