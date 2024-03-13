package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.common.ReleaseManager.ReleaseMetadata;
import com.yugabyte.yw.models.Release;
import com.yugabyte.yw.models.ReleaseArtifact;
import com.yugabyte.yw.models.ReleaseLocalFile;
import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.URL;
import java.nio.file.Path;
import java.text.DateFormat;
import java.text.ParseException;
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

  public final String RELEASE_PATH_CONFKEY = "yb.releases.artifacts.upload_path";

  public final String YB_PACKAGE_REGEX =
      "yugabyte-(?:ee-)?(.*)-(alma|centos|linux|el8|darwin)(.*).tar.gz";
  public final String YB_HELM_PACKAGE_REGEX = "(.*)yugabyte-(?:ee-)?(.*)-(helm)(.*).tar.gz";
  // Match release form 2.16.1.2 and return 2.16 or 2024.1.0.0 and return 2024
  public final String YB_VERSION_TYPE_REGEX = "(2\\.\\d+|\\d\\d\\d\\d)";

  // Should fallback to preview if a version is not in the map
  public final Map<String, String> releaseTypeMap =
      new HashMap<String, String>() {
        {
          put("2.14", "LTS");
          put("2.16", "STS");
          put("2.18", "STS");
          put("2.20", "LTS");
          put("2024", "STS");
        }
      };

  public static class ExtractedMetadata {
    public String version;
    public Release.YbType yb_type;
    public String sha256;
    public ReleaseArtifact.Platform platform;
    public Architecture architecture;
    public String release_type;
    public Date release_date;
    public String release_notes;
  }

  public ExtractedMetadata metadataFromPath(Path releaseFilePath) {
    String sha256 = null;
    try {
      sha256 = Util.computeFileChecksum(releaseFilePath, "SHA256");
    } catch (Exception e) {
      log.error("could not compute sha256", e);
    }
    try {
      ExtractedMetadata em =
          versionMetadataFromInputStream(
              new BufferedInputStream(new FileInputStream(releaseFilePath.toFile())));
      em.sha256 = sha256;
      return em;
    } catch (RuntimeException e) {
      // Fallback to file name validation
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
      return versionMetadataFromInputStream(new BufferedInputStream(url.openStream()));
    } catch (RuntimeException e) {
      // Fallback to file name validation
      ExtractedMetadata em = metadataFromName(url.getFile());
      return em;
    } catch (IOException e) {
      log.error("failed to open url " + url.toString());
      throw new RuntimeException("failed to open url", e);
    }
  }

  // Everything but the sha256
  private ExtractedMetadata versionMetadataFromInputStream(InputStream inputStream) {
    ExtractedMetadata metadata = new ExtractedMetadata();
    try (GzipCompressorInputStream gzIn = new GzipCompressorInputStream(inputStream);
        TarArchiveInputStream tarInput = new TarArchiveInputStream(gzIn)) {
      TarArchiveEntry entry;
      while ((entry = tarInput.getNextEntry()) != null) {
        if (entry.getName().endsWith("version_metadata.json")) {
          log.debug("found version_metadata.json");
          // We can reasonably assume that the version metadata json is small enough to read in
          // oneshot
          byte[] fileContent = new byte[(int) entry.getSize()];
          tarInput.read(fileContent, 0, fileContent.length);
          log.debug("read version_metadata.json string: {}", new String(fileContent));
          JsonNode node = Json.parse(fileContent);
          metadata.yb_type = Release.YbType.YBDB;

          // Populate required fields from version metadata. Bad Request if required fields do not
          // exist.
          if (node.has("version_number") && node.has("build_number")) {
            metadata.version =
                String.format(
                    "%s-b%s",
                    node.get("version_number").asText(), node.get("build_number").asText());
          } else {
            throw new RuntimeException("no version_number or build_number found");
          }
          if (node.has("platform")) {
            metadata.platform =
                ReleaseArtifact.Platform.valueOf(node.get("platform").asText().toUpperCase());
          } else {
            throw new RuntimeException("no platform found");
          }
          // TODO: release type should be mandatory
          if (node.has("release_type")) {
            metadata.release_type = node.get("release_type").asText();
          } else {
            log.warn("no release type, default to PREVIEW");
            metadata.release_type = "PREVIEW (DEFAULT)";
          }
          // Only Linux platform has architecture. K8S expects null value for architecture.
          if (metadata.platform.equals(ReleaseArtifact.Platform.LINUX)) {
            if (node.has("architecture")) {
              metadata.architecture = Architecture.valueOf(node.get("architecture").asText());
            } else {
              throw new RuntimeException("no 'architecture' for linux platform");
            }
          }

          // Populate optional sections if available.
          if (node.has("release_date")) {
            DateFormat df = DateFormat.getDateInstance();
            try {
              metadata.release_date = df.parse(node.get("release_date").asText());
              // best effort parse.
            } catch (ParseException e) {
              log.warn("invalid date format", e);
            }
          }
          if (node.has("release_notes")) {
            metadata.release_notes = node.get("release_notes").asText();
          }
          return metadata;
        }
      } // end of while loop
      log.error("No verison_metadata found in given input stream");
      throw new RuntimeException("no version_metadata found");
    } catch (java.io.IOException e) {
      log.error("failed reading the local file", e);
      throw new RuntimeException("failed to read metadata");
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
          if (!uploadedDir.delete()) {
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
      em.version = helmPackage.group(2);
      em.architecture = null;
    } else {
      throw new RuntimeException("failed to parse package " + fileName);
    }
    Pattern versionPattern = Pattern.compile(YB_VERSION_TYPE_REGEX);
    Matcher versionMatcher = versionPattern.matcher(em.version);
    if (!versionMatcher.find()) {
      throw new RuntimeException("Could not parse version");
    }
    em.release_type = releaseTypeMap.getOrDefault(versionMatcher.group(), "PREVIEW");
    return em;
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
      s3Location.paths.helmChartChecksum = artifact.getSha256();
    } else {
      s3Location.paths.x86_64 = s3File.path;
      s3Location.paths.x86_64_checksum = artifact.getSha256();
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
      gcsLocation.paths.helmChartChecksum = artifact.getSha256();
    } else {
      gcsLocation.paths.x86_64 = gcsFile.path;
      gcsLocation.paths.x86_64_checksum = artifact.getSha256();
    }
    return gcsLocation;
  }

  public ReleaseMetadata.HttpLocation httpLocationFromUrl(ReleaseArtifact artifact) {
    ReleaseMetadata.HttpLocation httpLocation = new ReleaseMetadata.HttpLocation();
    httpLocation.paths = new ReleaseMetadata.PackagePaths();
    if (artifact.isKubernetes()) {
      httpLocation.paths.helmChart = artifact.getPackageURL();
      httpLocation.paths.helmChartChecksum = artifact.getSha256();
    } else {
      httpLocation.paths.x86_64 = artifact.getPackageURL();
      httpLocation.paths.x86_64_checksum = artifact.getSha256();
    }
    return httpLocation;
  }
}
