package com.yugabyte.yw.common;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.models.Release;
import com.yugabyte.yw.models.ReleaseArtifact;
import com.yugabyte.yw.models.ReleaseLocalFile;
import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.nio.file.Path;
import java.text.DateFormat;
import java.text.ParseException;
import java.util.Date;
import java.util.UUID;
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
    ExtractedMetadata metadata = new ExtractedMetadata();
    try (InputStream fIn = new BufferedInputStream(new FileInputStream(releaseFilePath.toFile()));
        GzipCompressorInputStream gzIn = new GzipCompressorInputStream(fIn);
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
          metadata.sha256 = sha256;

          // Populate required fields from version metadata. Bad Request if required fields do not
          // exist.
          if (node.has("version_number") && node.has("build_number")) {
            metadata.version =
                String.format(
                    "%s-b%s",
                    node.get("version_number").asText(), node.get("build_number").asText());
          } else {
            throw new PlatformServiceException(
                BAD_REQUEST, "no version_number or build_number found");
          }
          if (node.has("platform")) {
            metadata.platform =
                ReleaseArtifact.Platform.valueOf(node.get("platform").asText().toUpperCase());
          } else {
            throw new PlatformServiceException(BAD_REQUEST, "no platform found");
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
              throw new PlatformServiceException(
                  BAD_REQUEST, "no 'architecture' for linux platform");
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
      log.error("No verison_metadata found in {}", releaseFilePath);
      throw new PlatformServiceException(BAD_REQUEST, "no version_metadata found");
    } catch (java.io.IOException e) {
      log.error("failed reading the local file", e);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "failed to read metadata from " + releaseFilePath);
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
}
