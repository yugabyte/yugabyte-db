// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.commissioner.Common.CloudType.onprem;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.ReleaseManager.ReleaseMetadata;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Release;
import com.yugabyte.yw.models.ReleaseArtifact;
import com.yugabyte.yw.models.ReleaseArtifact.GCSFile;
import com.yugabyte.yw.models.ReleaseArtifact.S3File;
import com.yugabyte.yw.models.ReleaseLocalFile;
import io.ebean.DB;
import io.ebean.Transaction;
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
              release =
                  Release.create(
                      releaseVersion, releasesUtils.releaseTypeFromVersion(releaseVersion));
            }
            createArtifacts(metadata, release);
            transaction.commit();
          } catch (Exception e) {
            LOG.error(
                "failed to migrate, skipping version "
                    + releaseVersion
                    + ": "
                    + e.getLocalizedMessage(),
                e);
          }
        });
  }

  private void createArtifacts(ReleaseMetadata metadata, Release release) {
    List<ReleaseArtifact> existingArtifacts = release.getArtifacts();
    metadata.packages.forEach(
        pkg -> {
          if (pkg.path.startsWith("/")) {
            // First, check to see if a local file that matches already exists
            Optional<ReleaseLocalFile> foundLocalFile =
                existingArtifacts.stream()
                    .filter(a -> a.getPackageFileID() != null)
                    .map(a -> ReleaseLocalFile.get(a.getPackageFileID()))
                    .filter(rlf -> rlf.getLocalFilePath().equals(pkg.path))
                    .findAny();
            if (!foundLocalFile.isPresent()) {
              // Local file and artifact is not present, create it
              release.addArtifact(
                  ReleaseArtifact.create(
                      null,
                      ReleaseArtifact.Platform.LINUX,
                      pkg.arch,
                      ReleaseLocalFile.create(pkg.path).getFileUUID()));
            }
          } else if (pkg.path.startsWith("s3")) {
            Optional<ReleaseArtifact> foundArtifact =
                existingArtifacts.stream().filter(a -> a.getS3File() != null).findAny();
            if (!foundArtifact.isPresent()) {
              // Create the s3 file if needed
              S3File s3File = new S3File();
              s3File.path = metadata.s3.paths.x86_64;
              s3File.accessKeyId = metadata.s3.accessKeyId;
              s3File.secretAccessKey = metadata.s3.secretAccessKey;
              release.addArtifact(
                  ReleaseArtifact.create(
                      metadata.s3.paths.x86_64_checksum,
                      ReleaseArtifact.Platform.LINUX,
                      pkg.arch,
                      s3File));
            }
          } else if (pkg.path.startsWith("gcs")) {
            Optional<ReleaseArtifact> foundArtifact =
                existingArtifacts.stream().filter(a -> a.getGcsFile() != null).findAny();
            if (!foundArtifact.isPresent()) {
              GCSFile gcsFile = new GCSFile();
              gcsFile.path = metadata.gcs.paths.x86_64;
              gcsFile.credentialsJson = metadata.gcs.credentialsJson;
              release.addArtifact(
                  ReleaseArtifact.create(
                      metadata.gcs.paths.x86_64_checksum,
                      ReleaseArtifact.Platform.LINUX,
                      pkg.arch,
                      gcsFile));
            }
          } else if (pkg.path.startsWith("http")) {
            Optional<ReleaseArtifact> foundArtifact =
                existingArtifacts.stream().filter(a -> a.getPackageURL() != null).findAny();
            if (!foundArtifact.isPresent()) {
              release.addArtifact(
                  ReleaseArtifact.create(
                      metadata.http.paths.x86_64_checksum,
                      ReleaseArtifact.Platform.LINUX,
                      pkg.arch,
                      metadata.http.paths.x86_64));
            }
          }
        });
    // create helm artifact if necessary
    if (metadata.chartPath != null
        && !metadata.chartPath.isEmpty()
        && release.getKubernetesArtifact() == null) {
      ReleaseLocalFile rlf = ReleaseLocalFile.create(metadata.chartPath);
      release.addArtifact(
          ReleaseArtifact.create(
              null, ReleaseArtifact.Platform.KUBERNETES, null, rlf.getFileUUID()));
    }
  }
}
