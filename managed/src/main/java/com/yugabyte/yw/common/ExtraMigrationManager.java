// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.commissioner.Common.CloudType.onprem;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.ReleaseManager.ReleaseMetadata;
import com.yugabyte.yw.common.ReleasesUtils.ExtractedMetadata;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Release;
import com.yugabyte.yw.models.ReleaseArtifact;
import com.yugabyte.yw.models.ReleaseArtifact.GCSFile;
import com.yugabyte.yw.models.ReleaseArtifact.S3File;
import com.yugabyte.yw.models.ReleaseLocalFile;
import io.ebean.DB;
import io.ebean.Transaction;
import java.nio.file.Paths;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/*
 * This is a manager to hold injected resources needed for extra migrations.
 */
@Singleton
public class ExtraMigrationManager extends DevopsBase {

  public static final Logger LOG = LoggerFactory.getLogger(ExtraMigrationManager.class);

  @Inject TemplateManager templateManager;

  @Inject ReleaseManager releaseManager;

  @Inject ReleasesUtils releasesUtils;

  @Override
  protected String getCommandType() {
    return "";
  }

  private void recreateProvisionScripts() {
    for (AccessKey accessKey : AccessKey.getAll()) {
      Provider p = Provider.get(accessKey.getProviderUUID());
      if (p != null && p.getCode().equals(onprem.name())) {
        templateManager.createProvisionTemplate(
            accessKey,
            p.getDetails().airGapInstall,
            p.getDetails().passwordlessSudoAccess,
            p.getDetails().installNodeExporter,
            p.getDetails().nodeExporterPort,
            p.getDetails().nodeExporterUser,
            p.getDetails().setUpChrony,
            p.getDetails().ntpServers);
      }
    }
  }

  public void V52__Update_Access_Key_Create_Extra_Migration() {
    recreateProvisionScripts();
  }

  public void R__Recreate_Provision_Script_Extra_Migrations() {
    recreateProvisionScripts();
  }

  public void R__Release_Metadata_Migration() {
    Map<String, Object> metadatas = releaseManager.getReleaseMetadata();
    metadatas.forEach(
        (releaseVersion, metadataObject) -> {
          LOG.info("Checking release for migration: version " + releaseVersion);
          try (Transaction transaction = DB.beginTransaction()) {
            ReleaseManager.ReleaseMetadata metadata =
                releaseManager.metadataFromObject(metadataObject);
            Release release = Release.getByVersion(releaseVersion);
            if (release == null) {
              LOG.debug("Creating new release");
              release =
                  Release.create(
                      releaseVersion, releasesUtils.releaseTypeFromVersion(releaseVersion));
            } else {
              LOG.trace("Found existing release");
            }
            createArtifacts(metadata, release);
            transaction.commit();
          } catch (Exception e) {
            LOG.error("failed to migrate, skipping version " + releaseVersion, e);
          }
        });
  }

  private void createArtifacts(ReleaseMetadata metadata, Release release) {
    // create helm artifact if necessary
    if (metadata.chartPath != null && release.getKubernetesArtifact() == null) {
      LOG.debug("updating k8s");
      ReleaseLocalFile rlf = ReleaseLocalFile.create(metadata.chartPath);
      release.addArtifact(
          ReleaseArtifact.create(
              null, ReleaseArtifact.Platform.KUBERNETES, null, rlf.getFileUUID()));
    }

    List<ReleaseArtifact> existingArtifacts = release.getArtifacts();
    // Now create the db.tgz artifact if necessary
    if (metadata.s3 != null) {
      LOG.debug("its an s3 release");
      Optional<ReleaseArtifact> foundArtifact =
          existingArtifacts.stream().filter(a -> a.getS3File() != null).findAny();
      if (foundArtifact.isPresent()) {
        LOG.trace("updating s3 release");
        // s3 artifact exists, but needs update.
        if (!foundArtifact.get().getS3File().path.equals(metadata.s3.paths.x86_64)) {
          S3File s3File = foundArtifact.get().getS3File();
          s3File.path = metadata.s3.paths.x86_64;
          s3File.accessKeyId = metadata.s3.accessKeyId;
          s3File.secretAccessKey = metadata.s3.secretAccessKey;
          foundArtifact.get().setS3File(s3File);
          foundArtifact.get().setSha256(metadata.s3.paths.x86_64_checksum);
        }
      } else {
        // S3 artifact does not exist, create it
        LOG.trace("Creating new s3 artifact");
        ExtractedMetadata em = releasesUtils.metadataFromName(metadata.s3.paths.x86_64);
        S3File s3File = new S3File();
        s3File.accessKeyId = metadata.s3.accessKeyId;
        s3File.secretAccessKey = metadata.s3.secretAccessKey;
        s3File.path = metadata.s3.paths.x86_64;
        try {
          release.addArtifact(
              ReleaseArtifact.create(
                  metadata.s3.paths.x86_64_checksum, em.platform, em.architecture, s3File));
        } catch (PlatformServiceException e) {
          LOG.warn("artifact matching platform/architecture already exists");
        }
      }
    } else if (metadata.gcs != null) {
      LOG.debug("its a gcs release");
      Optional<ReleaseArtifact> foundArtifact =
          existingArtifacts.stream().filter(a -> a.getGcsFile() != null).findAny();
      if (foundArtifact.isPresent()) {
        // GCS artifact exists, but needs update
        if (!foundArtifact.get().getGcsFile().path.equals(metadata.gcs.paths.x86_64)) {
          LOG.trace("Updating gcs artifact");
          GCSFile gcsFile = foundArtifact.get().getGcsFile();
          gcsFile.path = metadata.gcs.paths.x86_64;
          gcsFile.credentialsJson = metadata.gcs.credentialsJson;
          foundArtifact.get().setGCSFile(gcsFile);
          foundArtifact.get().setSha256(metadata.gcs.paths.x86_64_checksum);
        }
      } else {
        LOG.trace("Creating new gcs artifact");
        // GCS file does not exist, create it
        ExtractedMetadata em = releasesUtils.metadataFromName(metadata.gcs.paths.x86_64);
        GCSFile gcsFile = new GCSFile();
        gcsFile.path = metadata.gcs.paths.x86_64;
        gcsFile.credentialsJson = metadata.gcs.credentialsJson;
        try {
          release.addArtifact(
              ReleaseArtifact.create(
                  metadata.gcs.paths.x86_64_checksum, em.platform, em.architecture, gcsFile));
        } catch (PlatformServiceException e) {
          LOG.warn("artifact matching platform/architecture already exists");
        }
      }
    } else if (metadata.http != null) {
      LOG.debug("its an http release");
      Optional<ReleaseArtifact> foundArtifact =
          existingArtifacts.stream().filter(a -> a.getPackageURL() != null).findAny();
      if (foundArtifact.isPresent()) {
        // http artifact exists, but needs update
        if (!foundArtifact.get().getPackageURL().equals(metadata.http.paths.x86_64)) {
          LOG.trace("updating http artifact");
          foundArtifact.get().setPackageURL(metadata.http.paths.x86_64);
          foundArtifact.get().setSha256(metadata.gcs.paths.x86_64_checksum);
        }
      } else {
        // http file does not exist, create it
        LOG.trace("Creating new http artifact");
        ExtractedMetadata em = releasesUtils.metadataFromName(metadata.http.paths.x86_64);
        try {
          release.addArtifact(
              ReleaseArtifact.create(
                  metadata.http.paths.x86_64_checksum,
                  em.platform,
                  em.architecture,
                  metadata.http.paths.x86_64));
        } catch (PlatformServiceException e) {
          LOG.warn("artifact matching platform/architecture already exists");
        }
      }
    } else {
      LOG.debug("its a local artifact");
      // Handle local file
      Optional<ReleaseArtifact> foundArtifact =
          existingArtifacts.stream().filter(a -> a.getPackageFileID() != null).findAny();
      if (foundArtifact.isPresent()) {
        ReleaseLocalFile rlf = ReleaseLocalFile.get(foundArtifact.get().getPackageFileID());
        if (rlf.getLocalFilePath().equals(metadata.filePath)) {
          LOG.trace("updating local file path");
          rlf.setLocalFilePath(metadata.filePath);
        }
      } else {
        LOG.trace("Creating local artifact");
        ExtractedMetadata em = releasesUtils.metadataFromPath(Paths.get(metadata.filePath));
        try {
          release.addArtifact(
              ReleaseArtifact.create(
                  em.sha256,
                  em.platform,
                  em.architecture,
                  ReleaseLocalFile.create(metadata.filePath).getFileUUID()));
        } catch (PlatformServiceException e) {
          LOG.warn("artifact matching platform/architecture already exists");
        }
      }
    }
  }
}
