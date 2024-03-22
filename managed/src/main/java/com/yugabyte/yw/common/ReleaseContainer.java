package com.yugabyte.yw.common;

import com.typesafe.config.Config;
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
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Set;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import play.mvc.Http.Status;

@Slf4j
public class ReleaseContainer {
  private static final String DOWNLOAD_HELM_CHART_HTTP_TIMEOUT_PATH =
      "yb.releases.download_helm_chart_http_timeout";
  private static final String YB_RELEASES_PATH = "yb.releases.path";

  private ReleaseManager.ReleaseMetadata metadata;
  private Release release;

  private CloudUtilFactory cloudUtilFactory;
  private Config appConfig;

  // Internally set the artifact used for getFilePath
  private ReleaseArtifact artifact;

  public ReleaseContainer(Release release, CloudUtilFactory cloudUtilFactory, Config appConfig) {
    this.release = release;
    this.cloudUtilFactory = cloudUtilFactory;
    this.appConfig = appConfig;
  }

  public ReleaseContainer(
      ReleaseMetadata metadata, CloudUtilFactory cloudUtilFactory, Config appConfig) {
    this.metadata = metadata;
    this.cloudUtilFactory = cloudUtilFactory;
    this.appConfig = appConfig;
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
      return this.artifact.getFormattedSha256();
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
      for (ReleaseArtifact releaseArtifact : this.release.getArtifacts()) {
        if (releaseArtifact.getPlatform().equals(ReleaseArtifact.Platform.KUBERNETES)) {
          log.trace("Skipping kubernetes artifact");
          continue;
        }
        if (releaseArtifact.getS3File() != null) {
          CustomerConfigStorageS3Data configData = new CustomerConfigStorageS3Data();
          configData.awsAccessKeyId = releaseArtifact.getS3File().accessKeyId;
          configData.awsSecretAccessKey = releaseArtifact.getS3File().secretAccessKey;
          return cloudUtilFactory
              .getCloudUtil(Util.S3)
              .getCloudFileInputStream(configData, releaseArtifact.getS3File().path);
        } else if (releaseArtifact.getGcsFile() != null) {
          CustomerConfigStorageGCSData configData = new CustomerConfigStorageGCSData();
          configData.gcsCredentialsJson = releaseArtifact.getGcsFile().credentialsJson;
          return cloudUtilFactory
              .getCloudUtil(Util.GCS)
              .getCloudFileInputStream(configData, releaseArtifact.getGcsFile().path);
        } else if (releaseArtifact.getPackageURL() != null) {
          return new URL(releaseArtifact.getPackageURL()).openStream();
        } else if (releaseArtifact.getPackageFileID() != null) {
          ReleaseLocalFile rlf = ReleaseLocalFile.get(releaseArtifact.getPackageFileID());
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
      ReleaseArtifact artifact = this.release.getKubernetesArtifact();
      if (artifact == null) {
        throw new RuntimeException("no kubernetes artifacts found for release " + getVersion());
      }
      if (artifact.getPackageFileID() == null) {
        downloadYbHelmChart(this.release.getVersion(), appConfig.getString(YB_RELEASES_PATH));
        artifact = this.release.getKubernetesArtifact(); // Refetch the artifact.
      }
      if (artifact.getPackageFileID() != null) {
        ReleaseLocalFile rlf = ReleaseLocalFile.get(artifact.getPackageFileID());
        if (rlf == null) {
          throw new RuntimeException("No artifact found for" + artifact.getPackageFileID());
        }
        return rlf.getLocalFilePath();
      }
    }
    throw new RuntimeException(
        "No kubernetes artifact found for release " + this.release.getVersion());
  }

  public void downloadYbHelmChart(String version, String ybReleasesPath) {
    Path chartPath =
        Paths.get(ybReleasesPath, version, String.format("yugabyte-%s-helm.tar.gz", version));
    log.debug("Chart Path is {}", chartPath);
    String checksum = null;
    String configType = null;
    String urlPath = null;

    ReleaseArtifact artifact = null;
    // Helm chart can be downloaded only from one path.
    if (isLegacy()) {
      if (metadata.s3 != null && metadata.s3.paths.helmChart != null) {
        configType = Util.S3;
        urlPath = metadata.s3.paths.helmChart;
        checksum = metadata.s3.paths.helmChartChecksum;
      } else if (metadata.gcs != null && metadata.gcs.paths.helmChart != null) {
        configType = Util.GCS;
        urlPath = metadata.gcs.paths.helmChart;
        checksum = metadata.gcs.paths.helmChartChecksum;
      } else if (metadata.http != null && metadata.http.paths.helmChart != null) {
        configType = Util.HTTP;
        urlPath = metadata.http.paths.helmChart;
        checksum = metadata.http.paths.helmChartChecksum;
      } else {
        log.warn("cannot download helmchart for {}. no url found", version);
        return;
      }
    } else {
      artifact = release.getKubernetesArtifact();
      if (artifact == null) {
        log.warn(
            "cannot download helmchart for %s. No kubernetes artifact found", release.getVersion());
        return;
      }
      checksum = artifact.getFormattedSha256();
      if (artifact.getS3File() != null) {
        configType = Util.S3;
        urlPath = artifact.getS3File().path;
      } else if (artifact.getGcsFile() != null) {
        configType = Util.GCS;
        urlPath = artifact.getGcsFile().path;
      } else if (artifact.getPackageURL() != null) {
        configType = Util.HTTP;
        urlPath = artifact.getPackageURL();
      } else {
        log.warn("cannot download helmchart for {}. no url found", version);
        return;
      }
    }
    try {
      switch (configType) {
        case Util.S3:
          CustomerConfigStorageS3Data s3ConfigData = new CustomerConfigStorageS3Data();
          s3ConfigData.awsAccessKeyId = getAwsAccessKey(null);
          s3ConfigData.awsSecretAccessKey = getAwsSecretKey(null);
          cloudUtilFactory
              .getCloudUtil(Util.S3)
              .downloadCloudFile(s3ConfigData, urlPath, chartPath);
          break;
        case Util.GCS:
          CustomerConfigStorageGCSData gcsConfigData = new CustomerConfigStorageGCSData();
          gcsConfigData.gcsCredentialsJson = getGcsCredentials(null);
          cloudUtilFactory
              .getCloudUtil(Util.GCS)
              .downloadCloudFile(gcsConfigData, urlPath, chartPath);
          break;
        case Util.HTTP:
          int timeoutMs =
              this.appConfig.getMilliseconds(DOWNLOAD_HELM_CHART_HTTP_TIMEOUT_PATH).intValue();
          org.apache.commons.io.FileUtils.copyURLToFile(
              new URL(urlPath), chartPath.toFile(), timeoutMs, timeoutMs);
          break;
        default:
          throw new Exception("invalid download type " + configType);
      }
      if (!StringUtils.isBlank(checksum)) {
        checksum = checksum.toLowerCase();
        String[] checksumParts = checksum.split(":", 2);
        if (checksumParts.length < 2) {
          throw new PlatformServiceException(
              Status.BAD_REQUEST,
              String.format(
                  "Checksum must have a format of `[checksum algorithem]:[checksum value]`."
                      + " Got `%s`",
                  checksum));
        }
        String checksumAlgorithm = checksumParts[0];
        String checksumValue = checksumParts[1];
        String computedChecksum = Util.computeFileChecksum(chartPath, checksumAlgorithm);
        if (!checksumValue.equals(computedChecksum)) {
          throw new PlatformServiceException(
              Status.BAD_REQUEST,
              String.format(
                  "Computed checksum of file %s with algorithm %s is `%s` but user input"
                      + " checksum is `%s`",
                  chartPath, checksumAlgorithm, computedChecksum, checksumValue));
        }
      }
      if (isLegacy()) {
        this.metadata.chartPath = chartPath.toString();
      } else {
        ReleaseLocalFile rlf = ReleaseLocalFile.create(chartPath.toString());
        artifact.setPackageFileID(rlf.getFileUUID());
      }
    } catch (Exception e) {
      log.error("failed to download helmchart from " + urlPath, e);
      throw new RuntimeException("failed to download helm chart", e);
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

  public boolean isActive() {
    if (isLegacy()) {
      return this.metadata.state == ReleaseManager.ReleaseState.ACTIVE;
    } else {
      return this.release.getState() == Release.ReleaseState.ACTIVE;
    }
  }
}
