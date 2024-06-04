package com.yugabyte.yw.common;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.dataformat.yaml.YAMLFactory;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.common.ReleaseManager.ReleaseMetadata;
import com.yugabyte.yw.common.utils.FileUtils;
import com.yugabyte.yw.models.Release;
import com.yugabyte.yw.models.ReleaseArtifact;
import com.yugabyte.yw.models.ReleaseLocalFile;
import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.URL;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.LocalDateTime;
import java.time.ZoneId;
import java.time.format.DateTimeFormatter;
import java.time.format.DateTimeParseException;
import java.util.Date;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.compress.archivers.tar.TarArchiveEntry;
import org.apache.commons.compress.archivers.tar.TarArchiveInputStream;
import org.apache.commons.compress.compressors.gzip.GzipCompressorInputStream;
import play.libs.Json;

@Singleton
@Slf4j
public class ReleasesUtils {

  @Inject private Config appConfig;
  @Inject private ConfigHelper configHelper;

  public final String RELEASE_PATH_CONFKEY = "yb.releases.artifacts.upload_path";

  public final String YB_PACKAGE_REGEX =
      "yugabyte-(?:ee-)?(.*)-(alma|centos|linux|el8|darwin)(.*).tar.gz";
  // We can see helm packages as either yugabyte-<version>.tgz or yugabyte-version-helm.tgz
  public final String YB_HELM_PACKAGE_REGEX =
      "yugabyte-(?:ee-)?(?:(?:(.*?)(?:-helm))|(\\d+\\.\\d+\\.\\d+)).(?:tar.gz|tgz)";
  // Match release form 2.16.1.2 and return 2.16 or 2024.1.0.0 and return 2024
  public final String YB_VERSION_TYPE_REGEX = "(2\\.\\d+|\\d\\d\\d\\d\\.\\d+)";

  public final String YB_TAG_REGEX =
      "yugabyte-(?:ee-)?(?:.*)-(?!b\\d+)(.*)-(?:alma|centos|linux|el8|darwin)(?:.*).tar.gz";

  // The first version where YBDB must include a metadata.json file
  public final String YBDB_METADATA_REQUIRED_VERSION = "2024.1.0.0";

  // Should fallback to preview if a version is not in the map
  public final Map<String, String> releaseTypeMap =
      new HashMap<String, String>() {
        {
          put("2.14", "LTS");
          put("2.16", "STS");
          put("2.18", "STS");
          put("2.20", "LTS");
          put("2024.1", "STS");
          put("2024.2", "LTS");
          put("2025.1", "STS");
          put("2025.2", "LTS");
        }
      };

  public static class MetadataParseException extends Exception {
    public MetadataParseException(String errMsg) {
      super(errMsg);
    }
  }

  public static class ExtractedMetadata {
    public String version;
    public String releaseTag;
    public Release.YbType yb_type;
    public String sha256;
    public ReleaseArtifact.Platform platform;
    public Architecture architecture;
    public String release_type;
    public Date release_date;
    public String release_notes;
    public String minimumYbaVersion;
  }

  public ExtractedMetadata metadataFromPath(Path releaseFilePath) {
    String sha256 = null;
    try {
      sha256 = Util.computeFileChecksum(releaseFilePath, "SHA256");
    } catch (Exception e) {
      log.error("could not compute sha256", e);
    }
    try {
      if (isHelmChart(releaseFilePath.toString())) {
        return metadataFromHelmChart(
            new BufferedInputStream(Files.newInputStream(releaseFilePath)));
      }
      ExtractedMetadata em =
          versionMetadataFromInputStream(
              new BufferedInputStream(new FileInputStream(releaseFilePath.toFile())));
      em.sha256 = sha256;
      em.releaseTag = tagFromName(releaseFilePath.toString());
      return em;
    } catch (MetadataParseException e) {
      // Fallback to file name validation
      log.warn("falling back to file name metadata parsing for file " + releaseFilePath.toString());
      ExtractedMetadata em = metadataFromName(releaseFilePath.getFileName().toString());
      em.sha256 = sha256;
      return em;
    } catch (IOException e) {
      log.error("failed to open file " + releaseFilePath.toString(), e);
      throw new RuntimeException("failed to open file", e);
    }
  }

  public ExtractedMetadata versionMetadataFromURL(URL url) {
    try {
      if (isHelmChart(url.getFile())) {
        return metadataFromHelmChart(new BufferedInputStream(url.openStream()));
      }
      return versionMetadataFromInputStream(new BufferedInputStream(url.openStream()));
    } catch (MetadataParseException e) {
      // Fallback to file name validation
      log.warn("falling back to file name metadata parsing for url " + url.toString(), e);
      ExtractedMetadata em = metadataFromName(url.getFile());
      em.releaseTag = tagFromName(url.toString());
      return em;
    } catch (IOException e) {
      log.error("failed to open url " + url.toString());
      throw new RuntimeException("failed to open url", e);
    }
  }

  // Everything but the sha256
  private ExtractedMetadata versionMetadataFromInputStream(InputStream inputStream)
      throws MetadataParseException {
    ExtractedMetadata metadata = new ExtractedMetadata();
    try (GzipCompressorInputStream gzIn = new GzipCompressorInputStream(inputStream);
        TarArchiveInputStream tarInput = new TarArchiveInputStream(gzIn)) {
      TarArchiveEntry entry;
      while ((entry = tarInput.getNextEntry()) != null) {
        if (entry.getName().endsWith("version_metadata.json")) {
          log.trace("found version_metadata.json");
          // We can reasonably assume that the version metadata json is small enough to read in
          // oneshot
          byte[] fileContent = new byte[(int) entry.getSize()];
          tarInput.read(fileContent, 0, fileContent.length);
          log.trace("read version_metadata.json string: {}", new String(fileContent));
          JsonNode node = Json.parse(fileContent);
          metadata.minimumYbaVersion = getAndValidateYbaMinimumVersion(node);
          metadata.yb_type = Release.YbType.YBDB;

          // Populate required fields from version metadata. Bad Request if required fields do not
          // exist.
          if (node.has("version_number") && node.has("build_number")) {
            metadata.version =
                String.format(
                    "%s-b%s",
                    node.get("version_number").asText(), node.get("build_number").asText());
          } else {
            throw new MetadataParseException("no version_number or build_number found");
          }
          if (node.has("platform")) {
            metadata.platform =
                ReleaseArtifact.Platform.valueOf(node.get("platform").asText().toUpperCase());
          } else {
            throw new MetadataParseException("no platform found");
          }
          // TODO: release type should be mandatory
          if (node.has("release_type")) {
            metadata.release_type = node.get("release_type").asText();
          } else {
            log.warn("no release type, attempt to parse version for type");
            metadata.release_type = releaseTypeFromVersion(metadata.version);
          }
          // Only Linux platform has architecture. K8S expects null value for architecture.
          if (metadata.platform.equals(ReleaseArtifact.Platform.LINUX)) {
            if (node.has("architecture")) {
              metadata.architecture = Architecture.valueOf(node.get("architecture").asText());
            } else {
              throw new MetadataParseException("no 'architecture' for linux platform");
            }
          }

          // Populate optional sections if available.
          String rawDate = null;
          if (node.has("release_date")) {
            rawDate = node.get("release_date").asText();
          } else if (node.has("build_timestamp")) {
            rawDate = node.get("build_timestamp").asText();
            log.debug("using build timestamp {} as release date for {}", rawDate, metadata.version);
          }
          if (rawDate != null) {
            try {
              DateTimeFormatter formatter = DateTimeFormatter.ofPattern("dd MMM yyyy HH:mm:ss z");
              LocalDateTime dateTime = LocalDateTime.parse(rawDate, formatter);
              metadata.release_date = Date.from(dateTime.atZone(ZoneId.of("UTC")).toInstant());
              // best effort parse.
            } catch (DateTimeParseException e) {
              log.warn("invalid date format", e);
            }
          }

          // and finally get the release notes if its available.
          if (node.has("release_notes")) {
            metadata.release_notes = node.get("release_notes").asText();
          }
          return metadata;
        }
      } // end of while loop
      log.error("No version_metadata found in given input stream");
      throw new MetadataParseException("no version_metadata found");
    } catch (java.io.IOException e) {
      log.error("failed reading the local file", e);
      throw new MetadataParseException("failed to read metadata");
    }
  }

  // Cleanup all untracked uploads that do not have a related "release local file"
  public void cleanupUntracked() {
    String dir = appConfig.getString(RELEASE_PATH_CONFKEY);
    File uploadDir = new File(dir);
    if (!uploadDir.exists()) {
      if (!uploadDir.mkdirs()) {
        throw new RuntimeException("failed to create directory " + dir);
      }
      log.debug("no artifact upload directory, skipping clean");
      return;
    }
    for (File uploadedDir : new File(dir).listFiles()) {
      // If its not a directory, delete it
      if (!uploadedDir.isDirectory()) {
        log.debug("deleting file " + uploadedDir.getName());
        if (!uploadedDir.delete()) {
          log.error("failed to delete extra file " + uploadedDir.getAbsolutePath());
        }
        continue;
      }
      try {
        UUID dirUUID = UUID.fromString(uploadedDir.getName());
        if (ReleaseLocalFile.get(dirUUID) == null) {
          log.debug("deleting untracked local file " + dirUUID);
          if (!FileUtils.deleteDirectory(uploadedDir)) {
            log.error("failed to delete " + uploadedDir.getAbsolutePath());
          }
          continue;
        }
      } catch (IllegalArgumentException e) {
        log.debug("deleting non-uuid directory " + uploadedDir.getName());
        if (!uploadedDir.delete()) {
          log.error("failed to delete " + uploadedDir.getAbsolutePath());
        }
        continue;
      }
    }
  }

  public ExtractedMetadata metadataFromName(String fileName) {
    Pattern ybPackagePattern = Pattern.compile(YB_PACKAGE_REGEX);
    Pattern ybHelmChartPattern = Pattern.compile(YB_HELM_PACKAGE_REGEX);

    Matcher ybPackage = ybPackagePattern.matcher(fileName);
    Matcher helmPackage = ybHelmChartPattern.matcher(fileName);

    ExtractedMetadata em = new ExtractedMetadata();
    em.yb_type = Release.YbType.YBDB;
    if (ybPackage.find()) {
      em.platform = ReleaseArtifact.Platform.LINUX;
      em.version = ybPackage.group(1);
      if (fileName.contains("x86_64")) {
        em.architecture = Architecture.x86_64;
      } else if (fileName.contains("aarch64")) {
        em.architecture = Architecture.aarch64;
      } else {
        throw new RuntimeException("could not determine architecture from name" + fileName);
      }
    } else if (helmPackage.find()) {
      em.platform = ReleaseArtifact.Platform.KUBERNETES;
      em.version = helmPackage.group(1);
      em.architecture = null;
    } else {
      throw new RuntimeException("failed to parse package " + fileName);
    }
    em.release_type = releaseTypeFromVersion(em.version);
    return em;
  }

  private boolean isHelmChart(String fileName) {
    Pattern ybPackagePattern = Pattern.compile(YB_PACKAGE_REGEX);
    if (ybPackagePattern.matcher(fileName).find()) {
      return false;
    }
    Pattern ybHelmChartPattern = Pattern.compile(YB_HELM_PACKAGE_REGEX);
    Matcher helmPackage = ybHelmChartPattern.matcher(fileName);
    return helmPackage.find();
  }

  // Basic class to load chart yaml into. Only contains the fields we care about
  @JsonIgnoreProperties(ignoreUnknown = true)
  static class BasicChartYaml {
    public String appVersion;
  }

  private ExtractedMetadata metadataFromHelmChart(InputStream helmStream) {
    ExtractedMetadata metadata = new ExtractedMetadata();
    metadata.platform = ReleaseArtifact.Platform.KUBERNETES;
    try (GzipCompressorInputStream gzIn = new GzipCompressorInputStream(helmStream);
        TarArchiveInputStream tarInput = new TarArchiveInputStream(gzIn)) {
      TarArchiveEntry entry;
      while ((entry = tarInput.getNextEntry()) != null) {
        if (entry.getName().endsWith("Chart.yaml") || entry.getName().endsWith("Chart.yml")) {
          log.trace("Found Chart.yml");
          // We can reasonably assume that the version metadata json is small enough to read in
          // oneshot
          byte[] fileContent = new byte[(int) entry.getSize()];
          tarInput.read(fileContent, 0, fileContent.length);
          ObjectMapper om = new ObjectMapper(new YAMLFactory());
          BasicChartYaml chartYaml = om.readValue(fileContent, BasicChartYaml.class);
          metadata.version = chartYaml.appVersion;
          metadata.release_type = releaseTypeFromVersion(metadata.version);
          return metadata;
        }
      }
      throw new RuntimeException("invalid helm chart -no Chart.yml found");
    } catch (java.io.IOException e) {
      throw new RuntimeException("failed to read metadata", e);
    }
  }

  public String releaseTypeFromVersion(String version) {
    Pattern versionPattern = Pattern.compile(YB_VERSION_TYPE_REGEX);
    Matcher versionMatcher = versionPattern.matcher(version);
    if (!versionMatcher.find()) {
      throw new RuntimeException("Could not parse version");
    }
    return releaseTypeMap.getOrDefault(versionMatcher.group(), "PREVIEW");
  }

  public ReleaseMetadata releaseToReleaseMetadata(Release release) {
    ReleaseMetadata metadata = ReleaseMetadata.create(release.getVersion());
    for (ReleaseArtifact artifact : release.getArtifacts()) {
      String path = null;
      if (artifact.getPackageFileID() != null) {
        metadata.filePath = ReleaseLocalFile.get(artifact.getPackageFileID()).getLocalFilePath();
        path = metadata.filePath;
      }
      if (artifact.getS3File() != null) {
        metadata.s3 = s3LocationFroms3File(artifact);
        path = artifact.getS3File().path;
      } else if (artifact.getGcsFile() != null) {
        metadata.gcs = gcsLocationFromGcsFile(artifact);
        path = artifact.getGcsFile().path;
      } else if (artifact.getPackageURL() != null) {
        metadata.http = httpLocationFromUrl(artifact);
        path = artifact.getPackageURL();
      }
      metadata = metadata.withPackage(path, artifact.getArchitecture());
    }
    return metadata;
  }

  public ReleaseMetadata.S3Location s3LocationFroms3File(ReleaseArtifact artifact) {
    ReleaseMetadata.S3Location s3Location = new ReleaseMetadata.S3Location();
    ReleaseArtifact.S3File s3File = artifact.getS3File();
    s3Location.accessKeyId = s3File.accessKeyId;
    s3Location.secretAccessKey = s3File.secretAccessKey;
    s3Location.paths = new ReleaseMetadata.PackagePaths();
    if (artifact.isKubernetes()) {
      s3Location.paths.helmChart = s3File.path;
      s3Location.paths.helmChartChecksum = artifact.getFormattedSha256();
    } else {
      s3Location.paths.x86_64 = s3File.path;
      s3Location.paths.x86_64_checksum = artifact.getFormattedSha256();
    }
    return s3Location;
  }

  public ReleaseMetadata.GCSLocation gcsLocationFromGcsFile(ReleaseArtifact artifact) {
    ReleaseMetadata.GCSLocation gcsLocation = new ReleaseMetadata.GCSLocation();
    ReleaseArtifact.GCSFile gcsFile = artifact.getGcsFile();
    gcsLocation.credentialsJson = gcsFile.credentialsJson;
    gcsLocation.paths = new ReleaseMetadata.PackagePaths();
    if (artifact.isKubernetes()) {
      gcsLocation.paths.helmChart = gcsFile.path;
      gcsLocation.paths.helmChartChecksum = artifact.getFormattedSha256();
    } else {
      gcsLocation.paths.x86_64 = gcsFile.path;
      gcsLocation.paths.x86_64_checksum = artifact.getFormattedSha256();
    }
    return gcsLocation;
  }

  public ReleaseMetadata.HttpLocation httpLocationFromUrl(ReleaseArtifact artifact) {
    ReleaseMetadata.HttpLocation httpLocation = new ReleaseMetadata.HttpLocation();
    httpLocation.paths = new ReleaseMetadata.PackagePaths();
    if (artifact.isKubernetes()) {
      httpLocation.paths.helmChart = artifact.getPackageURL();
      httpLocation.paths.helmChartChecksum = artifact.getFormattedSha256();
    } else {
      httpLocation.paths.x86_64 = artifact.getPackageURL();
      httpLocation.paths.x86_64_checksum = artifact.getFormattedSha256();
    }
    return httpLocation;
  }

  private String getAndValidateYbaMinimumVersion(JsonNode node) {
    if (node.has("minimum_yba_version")) {
      String minVersion = node.get("minimum_yba_version").asText();
      String currVersion = ybaCurrentVersion();
      // If the current version is less then the minimum version, validation failed
      if (Util.compareYbVersions(currVersion, minVersion) < 0) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "current version %s is less then the specified yba minimum version %s",
                currVersion.toString(), minVersion.toString()));
      }
      return minVersion;
    }
    log.warn("no key 'minimum_yba_version' found");
    return null;
  }

  private String ybaCurrentVersion() {
    Map<String, Object> versionCfg =
        configHelper.getConfig(ConfigHelper.ConfigType.SoftwareVersion);
    if (!versionCfg.containsKey("version")) {
      throw new RuntimeException(
          "no 'version' key found config " + ConfigHelper.ConfigType.SoftwareVersion);
    }
    return (String) versionCfg.get("version");
  }

  private String tagFromName(String name) {
    Pattern tagPattern = Pattern.compile(YB_TAG_REGEX);
    Matcher tagMatch = tagPattern.matcher(name);
    if (tagMatch.find()) {
      return tagMatch.group(1);
    }
    return null;
  }
}
