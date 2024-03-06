package com.yugabyte.yw.common;

import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.common.ReleaseManager.ReleaseMetadata;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Release;
import com.yugabyte.yw.models.ReleaseArtifact;
import com.yugabyte.yw.models.ReleaseLocalFile;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageGCSData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data;
import java.io.FileInputStream;
import java.io.InputStream;
import java.net.URL;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.Set;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class ReleaseContainer {
  private ReleaseManager.ReleaseMetadata metadata;
  private Release release;

  private CloudUtilFactory cloudUtilFactory;

  // Internally set the artifact used for getFilePath
  private ReleaseArtifact artifact;

  public ReleaseContainer(Release release, CloudUtilFactory cloudUtilFactory) {
    this.release = release;
    this.cloudUtilFactory = cloudUtilFactory;
  }

  public ReleaseContainer(ReleaseMetadata metadata, CloudUtilFactory cloudUtilFactory) {
    this.metadata = metadata;
    this.cloudUtilFactory = cloudUtilFactory;
  }

  public boolean isLegacy() {
    return metadata != null;
  }

  public String getVersion() {
    if (isLegacy()) {
      return this.metadata.imageTag;
    } else {
      return this.release.getVersion();
    }
  }

  public String getFilePath(Region region) {
    Architecture arch = region.getArchitecture();
    return getFilePath(arch);
  }

  public String getFilePath(Architecture arch) {
    if (isLegacy()) {
      return this.metadata.getFilePath(arch);
    } else {
      ReleaseArtifact artifact = this.release.getArtifactForArchitecture(arch);
      if (artifact == null) {
        throw new RuntimeException("no artifact found for architecture " + arch.name());
      }
      // Save artifact for later;
      this.artifact = artifact;
      if (artifact.getPackageURL() != null) {
        return artifact.getPackageURL();
      } else if (artifact.getPackageFileID() != null) {
        ReleaseLocalFile rlf = ReleaseLocalFile.get(artifact.getPackageFileID());
        if (rlf != null) {
          return rlf.getLocalFilePath();
        }
        throw new RuntimeException("no local file found for architecture " + arch.name());
      } else if (artifact.getGcsFile() != null) {
        return artifact.getGcsFile().path;
      } else if (artifact.getS3File() != null) {
        return artifact.getS3File().path;
      } else {
        throw new RuntimeException(
            "Could not find matching package with architecture " + arch.name());
      }
    }
  }

  public boolean isHttpDownload(String ybPackage) {
    if (isLegacy()) {
      return (this.metadata.http != null && this.metadata.http.paths.x86_64.equals(ybPackage));
    } else {
      if (this.artifact == null) {
        setArtifactMatchingPackage(null);
      }
      return this.artifact.getPackageURL() != null;
    }
  }

  // NOTE: should only be called after `isHttpDownload` returns true
  public String getHttpChecksum() {
    if (isLegacy()) {
      return this.metadata.http.paths.getX86_64_checksum();
    } else {
      return this.artifact.getSha256();
    }
  }

  public boolean isGcsDownload(String ybPackage) {
    if (isLegacy()) {
      return this.metadata.gcs != null && this.metadata.gcs.paths.x86_64.equals(ybPackage);
    } else {
      if (this.artifact == null) {
        setArtifactMatchingPackage(ybPackage);
      }
      return this.artifact.getGcsFile() != null;
    }
  }

  public boolean isS3Download(String ybPackage) {
    if (isLegacy()) {
      return this.metadata.s3 != null && this.metadata.s3.paths.x86_64.equals(ybPackage);
    } else {
      if (this.artifact == null) {
        setArtifactMatchingPackage(ybPackage);
      }
      return this.artifact.getS3File() != null;
    }
  }

  // This only is used for extracting gflag packages, and should find the first non-k8s package
  // available when using the new release format in order to accomplish this.
  public InputStream getTarGZipDBPackageInputStream() throws Exception {
    if (isLegacy()) {
      if (this.metadata.s3 != null) {
        CustomerConfigStorageS3Data configData = new CustomerConfigStorageS3Data();
        configData.awsAccessKeyId = this.metadata.s3.getAccessKeyId();
        configData.awsSecretAccessKey = this.metadata.s3.secretAccessKey;
        return cloudUtilFactory
            .getCloudUtil(Util.S3)
            .getCloudFileInputStream(configData, this.metadata.s3.paths.getX86_64());
      } else if (this.metadata.gcs != null) {
        CustomerConfigStorageGCSData configData = new CustomerConfigStorageGCSData();
        configData.gcsCredentialsJson = this.metadata.gcs.credentialsJson;
        return cloudUtilFactory
            .getCloudUtil(Util.GCS)
            .getCloudFileInputStream(configData, this.metadata.gcs.paths.getX86_64());
      } else if (this.metadata.http != null) {
        return new URL(this.metadata.http.getPaths().getX86_64()).openStream();
      } else {
        if (!Files.exists(Paths.get(this.metadata.filePath))) {
          throw new RuntimeException(
              "Cannot add gFlags metadata for version: "
                  + this.metadata.imageTag
                  + " as no file was present at location: "
                  + this.metadata.filePath);
        }
        return new FileInputStream(this.metadata.filePath);
      }
    } else {
      for (ReleaseArtifact artifact : this.release.getArtifacts()) {
        if (artifact.getPlatform().equals(ReleaseArtifact.Platform.KUBERNETES)) {
          log.trace("Skipping kubernetes artifact");
          continue;
        }
        if (this.artifact.getS3File() != null) {
          CustomerConfigStorageS3Data configData = new CustomerConfigStorageS3Data();
          configData.awsAccessKeyId = this.artifact.getS3File().accessKeyId;
          configData.awsSecretAccessKey = this.artifact.getS3File().secretAccessKey;
          return cloudUtilFactory
              .getCloudUtil(Util.S3)
              .getCloudFileInputStream(configData, this.artifact.getS3File().path);
        } else if (this.artifact.getGcsFile() != null) {
          CustomerConfigStorageGCSData configData = new CustomerConfigStorageGCSData();
          configData.gcsCredentialsJson = this.artifact.getGcsFile().credentialsJson;
          return cloudUtilFactory
              .getCloudUtil(Util.GCS)
              .getCloudFileInputStream(configData, this.artifact.getGcsFile().path);
        } else if (this.artifact.getPackageURL() != null) {
          return new URL(this.artifact.getPackageURL()).openStream();
        } else if (this.artifact.getPackageFileID() != null) {
          ReleaseLocalFile rlf = ReleaseLocalFile.get(this.artifact.getPackageFileID());
          if (!Files.exists(Paths.get(rlf.getLocalFilePath()))) {
            throw new RuntimeException(
                "Cannot add gFlags metadata for version: "
                    + this.release.getVersion()
                    + " as no file was present at location: "
                    + rlf.getLocalFilePath());
          }
          return new FileInputStream(rlf.getLocalFilePath());
        }
      }
      throw new RuntimeException(
          "Artifact for release" + this.release.getVersion() + " has no related files");
    }
  }

  public String getHelmChart() {
    if (isLegacy()) {
      return this.metadata.chartPath;
    } else {
      for (ReleaseArtifact artifact : this.release.getArtifacts()) {
        if (artifact.getPlatform().equals(ReleaseArtifact.Platform.KUBERNETES)) {
          if (artifact.getPackageFileID() != null) {
            ReleaseLocalFile rlf = ReleaseLocalFile.get(artifact.getPackageFileID());
            if (rlf == null) {
              throw new RuntimeException("No artifact found for" + artifact.getPackageFileID());
            }
            return rlf.getLocalFilePath();
          }
        } else {
          throw new RuntimeException("Invalid kubernetes artifact, expected file to be local");
        }
      }
      throw new RuntimeException(
          "No kubernetes artifact found for release " + this.release.getVersion());
    }
  }

  public boolean isAws(Architecture arch) {
    if (isLegacy()) {
      return this.metadata.s3 != null;
    } else {
      ReleaseArtifact artifact = this.release.getArtifactForArchitecture(arch);
      return artifact != null && artifact.getS3File() != null;
    }
  }

  public boolean isGcs(Architecture arch) {
    if (isLegacy()) {
      return this.metadata.gcs != null;
    } else {
      ReleaseArtifact artifact = this.release.getArtifactForArchitecture(arch);
      return artifact != null && artifact.getGcsFile() != null;
    }
  }

  public String getAwsAccessKey(Architecture arch) {
    if (isLegacy()) {
      return this.metadata.s3.accessKeyId;
    } else {
      // It is expected to first call "isAws".
      ReleaseArtifact artifact = this.release.getArtifactForArchitecture(arch);
      if (artifact == null) {
        throw new RuntimeException("No GCS artifact found");
      }
      return artifact.getS3File().accessKeyId;
    }
  }

  public String getAwsSecretKey(Architecture arch) {
    if (isLegacy()) {
      return this.metadata.s3.secretAccessKey;
    } else {
      // It is expected to first call "isAws".
      ReleaseArtifact artifact = this.release.getArtifactForArchitecture(arch);
      if (artifact == null) {
        throw new RuntimeException("No GCS artifact found");
      }
      return artifact.getS3File().secretAccessKey;
    }
  }

  public String getGcsCredentials(Architecture arch) {
    if (isLegacy()) {
      return this.metadata.gcs.credentialsJson;
    } else {
      // It is expected to first call "isGcs".
      ReleaseArtifact artifact = this.release.getArtifactForArchitecture(arch);
      if (artifact == null) {
        throw new RuntimeException("No GCS artifact found");
      }
      return artifact.getGcsFile().credentialsJson;
    }
  }

  public void setState(String stateValue) {
    if (isLegacy()) {
      this.metadata.state = ReleaseManager.ReleaseState.valueOf(stateValue);
    } else {
      this.release.setState(Release.ReleaseState.valueOf(stateValue));
    }
  }

  public boolean hasLocalRelease() {
    if (isLegacy()) {
      return this.metadata.hasLocalRelease();
    } else {
      return this.release.getArtifacts().stream().anyMatch(r -> r.getPackageFileID() != null);
    }
  }

  public Set<String> getLocalReleasePathStrings() {
    if (isLegacy()) {
      return this.metadata.getLocalReleases();
    } else {
      return this.release.getArtifacts().stream()
          .filter(r -> r.getPackageFileID() != null)
          .map(r -> ReleaseLocalFile.get(r.getPackageFileID()).getLocalFilePath())
          .collect(Collectors.toSet());
    }
  }

  public ReleaseManager.ReleaseMetadata getMetadata() {
    return this.metadata;
  }

  private void setArtifactMatchingPackage(String ybPackage) {
    for (ReleaseArtifact artifact : this.release.getArtifacts()) {
      if (artifact.getPackageURL() != null && artifact.getPackageURL().equals(ybPackage)) {
        this.artifact = artifact;
        return;
      } else if (artifact.getPackageFileID() != null
          && ReleaseLocalFile.get(artifact.getPackageFileID())
              .getLocalFilePath()
              .equals(ybPackage)) {
        this.artifact = artifact;
        return;
      } else if (artifact.getGcsFile() != null && artifact.getGcsFile().path.equals(ybPackage)) {
        this.artifact = artifact;
        return;
      } else if (artifact.getS3File() != null && artifact.getS3File().path.equals(ybPackage)) {
        this.artifact = artifact;
        return;
      }
    }
    throw new RuntimeException("Unable to find matching artifact for package " + ybPackage);
  }
}
