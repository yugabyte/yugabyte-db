package com.yugabyte.yw.common.operator;

import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.common.ReleaseContainer;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.ReleaseManager.ReleaseMetadata;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.common.operator.utils.ResourceAnnotationKeys;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.models.ReleaseArtifact;
import com.yugabyte.yw.models.ReleaseArtifact.Platform;
import io.ebean.DB;
import io.ebean.Transaction;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.Resource;
import io.fabric8.kubernetes.client.informers.ResourceEventHandler;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.fabric8.kubernetes.client.informers.cache.Lister;
import io.yugabyte.operator.v1alpha1.Release;
import io.yugabyte.operator.v1alpha1.ReleaseStatus;
import io.yugabyte.operator.v1alpha1.releasespec.config.downloadconfig.gcs.CredentialsJsonSecret;
import io.yugabyte.operator.v1alpha1.releasespec.config.downloadconfig.s3.SecretAccessKeySecret;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class ReleaseReconciler implements ResourceEventHandler<Release>, Runnable {
  private final SharedIndexInformer<Release> informer;
  private final Lister<Release> lister;
  private final MixedOperation<Release, KubernetesResourceList<Release>, Resource<Release>>
      resourceClient;
  private final ReleaseManager releaseManager;
  private final GFlagsValidation gFlagsValidation;
  private final String namespace;
  private final RuntimeConfGetter confGetter;
  private final OperatorUtils operatorUtils;

  public static Pair<String, ReleaseMetadata> crToReleaseMetadata(Release release) {
    return OperatorUtils.crToReleaseMetadata(release);
  }

  public ReleaseReconciler(
      SharedIndexInformer<Release> releaseInformer,
      MixedOperation<Release, KubernetesResourceList<Release>, Resource<Release>> resourceClient,
      ReleaseManager releaseManager,
      GFlagsValidation gFlagsValidation,
      String namespace,
      RuntimeConfGetter confGetter,
      OperatorUtils operatorUtils) {
    this.resourceClient = resourceClient;
    this.informer = releaseInformer;
    this.lister = new Lister<>(informer.getIndexer());
    this.releaseManager = releaseManager;
    this.namespace = namespace;
    this.gFlagsValidation = gFlagsValidation;
    this.confGetter = confGetter;
    this.operatorUtils = operatorUtils;
  }

  @Override
  public void onAdd(Release release) {
    ObjectMeta releaseMetadata = release.getMetadata();
    log.info("Adding release {} ", releaseMetadata.getName());
    if (confGetter.getGlobalConf(GlobalConfKeys.enableReleasesRedesign)) {
      String version = release.getSpec().getConfig().getVersion();
      com.yugabyte.yw.models.Release ybRelease;
      if (releaseMetadata.getAnnotations() != null
          && releaseMetadata.getAnnotations().containsKey(ResourceAnnotationKeys.YBA_RESOURCE_ID)) {
        ybRelease =
            com.yugabyte.yw.models.Release.get(
                UUID.fromString(
                    releaseMetadata.getAnnotations().get(ResourceAnnotationKeys.YBA_RESOURCE_ID)));
      } else {
        ybRelease = com.yugabyte.yw.models.Release.getByVersion(version);
      }
      if (ybRelease != null) {
        log.info("release version already exists, cannot create: " + version);
        // If it already exists, we should just use it.
        updateStatus(release, "Available", true);
        return;
      }
      try (Transaction transaction = DB.beginTransaction()) {
        releaseMetadata.setFinalizers(Collections.singletonList(OperatorUtils.YB_FINALIZER));
        resourceClient.inNamespace(namespace).withName(releaseMetadata.getName()).patch(release);
        ybRelease = com.yugabyte.yw.models.Release.create(version, "LTS");
        List<ReleaseArtifact> artifacts = createReleaseArtifacts(release);
        for (ReleaseArtifact artifact : artifacts) {
          ybRelease.addArtifact(artifact);
        }
        releaseManager.downloadYbHelmChart(version, ybRelease);
        transaction.commit();
        // We downloaded it, so we should update the current releases and mark them available.
        updateStatus(release, "Available", true);
      } catch (Exception e) {
        log.error("failed to add release", e);
        updateStatus(release, "unable to add release: " + e.getMessage(), false);
      }
    } else {
      Pair<String, ReleaseMetadata> releasePair = crToReleaseMetadata(release);
      try {
        releaseMetadata.setFinalizers(Collections.singletonList(OperatorUtils.YB_FINALIZER));
        resourceClient.inNamespace(namespace).withName(releaseMetadata.getName()).patch(release);
        releaseManager.addReleaseWithMetadata(releasePair.getFirst(), releasePair.getSecond());
        gFlagsValidation.addDBMetadataFiles(releasePair.getFirst());
        releaseManager.updateCurrentReleases();
        updateStatus(release, "Available", true);
      } catch (RuntimeException re) {
        log.error("Error in adding release", re);
        updateStatus(release, "Failed to Download", false);
        return;
      }
    }
    log.info("Added release {} ", release.getMetadata().getName());
  }

  @Override
  public void onUpdate(Release oldRelease, Release newRelease) {
    // Handle the delete workflow first, as we get a this call before onDelete is called
    if (newRelease.getMetadata().getDeletionTimestamp() != null) {
      onDelete(newRelease, true);
      return;
    }
    if (confGetter.getGlobalConf(GlobalConfKeys.enableReleasesRedesign)) {
      String version = newRelease.getSpec().getConfig().getVersion();
      com.yugabyte.yw.models.Release ybRelease;
      if (newRelease.getMetadata().getAnnotations() != null
          && newRelease
              .getMetadata()
              .getAnnotations()
              .containsKey(ResourceAnnotationKeys.YBA_RESOURCE_ID)) {
        ybRelease =
            com.yugabyte.yw.models.Release.get(
                UUID.fromString(
                    newRelease
                        .getMetadata()
                        .getAnnotations()
                        .get(ResourceAnnotationKeys.YBA_RESOURCE_ID)));
      } else {
        ybRelease = com.yugabyte.yw.models.Release.getByVersion(version);
      }
      if (ybRelease == null) {
        log.warn("no release found for version " + version + ". creating it");
        ybRelease = com.yugabyte.yw.models.Release.create(version, "LTS");
      }
      boolean changes = false;
      for (ReleaseArtifact artifact : ybRelease.getArtifacts()) {
        if (artifact.getPackageFileID() != null) {
          log.debug("skip reconcile for local file release artifact {}", artifact);
          continue;
        }
        if (artifactHasChanges(artifact, newRelease)) {
          changes = true;
          break;
        }
      }
      if (!changes) {
        log.info("no changes found for release {}", version);
        return;
      }

      try (Transaction transaction = DB.beginTransaction()) {
        // Delete existing artifacts
        for (ReleaseArtifact artifact : ybRelease.getArtifacts()) {
          artifact.delete();
        }
        // create new artifacts
        List<ReleaseArtifact> artifacts = createReleaseArtifacts(newRelease);
        for (ReleaseArtifact artifact : artifacts) {
          ybRelease.addArtifact(artifact);
        }
        releaseManager.downloadYbHelmChart(version, ybRelease);
        transaction.commit();
      } catch (Exception e) {
        log.error("failed to add release", e);
        updateStatus(newRelease, "unable to add release: " + e.getMessage(), false);
      }
    } else {
      Pair<String, ReleaseMetadata> releasePair = crToReleaseMetadata(newRelease);
      try {
        String version = releasePair.getFirst();
        ReleaseMetadata metadata = releasePair.getSecond();
        // copy chartPath because it already exists.
        ReleaseContainer existing_rm = releaseManager.getReleaseByVersion(version);
        if (existing_rm != null) {
          if (existing_rm.getHelmChart() != null) {
            log.info("Updating the chartPath because existing metadata has chart path");
            metadata.chartPath = existing_rm.getHelmChart();
          } else {
            log.info("No existing chart path found, downloading chart");
            releaseManager.downloadYbHelmChart(version, existing_rm);
          }
        } else {
          log.info("No existing metadata found, adding new release metadata");
          // We never downloaded the helm chart for the previous release, so lets add the releasee
          releaseManager.addReleaseWithMetadata(version, metadata);
        }
        releaseManager.updateReleaseMetadata(version, metadata);
        releaseManager.updateCurrentReleases();
        updateStatus(newRelease, "Available", true);
      } catch (RuntimeException re) {
        updateStatus(newRelease, "Failed to Download", false);
        log.error("Error in updating release", re);
      }
    }
    log.info(
        "finished update CR release old: {}, new: {}",
        oldRelease.getMetadata().getName(),
        newRelease.getMetadata().getName());
  }

  @Override
  public void onDelete(Release release, boolean deletedFinalStateUnknown) {
    operatorUtils.deleteReleaseCr(release);
  }

  @Override
  public void run() {
    informer.addEventHandler(this);
    informer.run();
  }

  private void updateStatus(Release release, String status, Boolean success) {
    ReleaseStatus releaseStatus = new ReleaseStatus();
    releaseStatus.setMessage(status);
    releaseStatus.setSuccess(success);
    release.setStatus(releaseStatus);
    resourceClient.inNamespace(namespace).resource(release).replaceStatus();
  }

  private List<ReleaseArtifact> createReleaseArtifacts(Release release) throws Exception {
    ReleaseArtifact dbArtifact = null;
    ReleaseArtifact helmArtifact = null;
    if (release.getSpec().getConfig() == null
        || release.getSpec().getConfig().getDownloadConfig() == null) {
      throw new Exception(
          String.format("No download config found release %s", release.getMetadata().getName()));
    }
    if (release.getSpec().getConfig().getDownloadConfig().getS3() != null) {
      ReleaseArtifact.S3File s3File = new ReleaseArtifact.S3File();
      s3File.path =
          release.getSpec().getConfig().getDownloadConfig().getS3().getPaths().getX86_64();
      s3File.accessKeyId =
          release.getSpec().getConfig().getDownloadConfig().getS3().getAccessKeyId();
      s3File.secretAccessKey =
          release.getSpec().getConfig().getDownloadConfig().getS3().getSecretAccessKey();
      if (release.getSpec().getConfig().getDownloadConfig().getS3().getSecretAccessKeySecret()
          != null) {
        SecretAccessKeySecret awsSecret =
            release.getSpec().getConfig().getDownloadConfig().getS3().getSecretAccessKeySecret();
        String secret =
            operatorUtils.getAndParseSecretForKey(
                awsSecret.getName(), awsSecret.getNamespace(), "AWS_SECRET_ACCESS_KEY");
        if (secret != null) {
          s3File.secretAccessKey = secret;
        } else {
          log.warn("Aws secret access key secret {} not found", awsSecret.getName());
        }
      }
      dbArtifact =
          ReleaseArtifact.create(
              release
                  .getSpec()
                  .getConfig()
                  .getDownloadConfig()
                  .getS3()
                  .getPaths()
                  .getX86_64_checksum(),
              Platform.LINUX,
              Architecture.x86_64,
              s3File);
      s3File.path =
          release.getSpec().getConfig().getDownloadConfig().getS3().getPaths().getHelmChart();
      helmArtifact =
          ReleaseArtifact.create(
              release
                  .getSpec()
                  .getConfig()
                  .getDownloadConfig()
                  .getS3()
                  .getPaths()
                  .getHelmChartChecksum(),
              Platform.KUBERNETES,
              null,
              s3File);
    } else if (release.getSpec().getConfig().getDownloadConfig().getGcs() != null) {
      ReleaseArtifact.GCSFile gcsFile = new ReleaseArtifact.GCSFile();
      gcsFile.path =
          release.getSpec().getConfig().getDownloadConfig().getGcs().getPaths().getX86_64();
      gcsFile.credentialsJson =
          release.getSpec().getConfig().getDownloadConfig().getGcs().getCredentialsJson();
      if (release.getSpec().getConfig().getDownloadConfig().getGcs().getCredentialsJsonSecret()
          != null) {
        CredentialsJsonSecret gcsSecret =
            release.getSpec().getConfig().getDownloadConfig().getGcs().getCredentialsJsonSecret();
        String secret =
            operatorUtils.getAndParseSecretForKey(
                gcsSecret.getName(), gcsSecret.getNamespace(), "CREDENTIALS_JSON");
        if (secret != null) {
          gcsFile.credentialsJson = secret;
        } else {
          log.warn("Gcs credentials json secret {} not found", gcsSecret.getName());
        }
      }
      dbArtifact =
          ReleaseArtifact.create(
              release
                  .getSpec()
                  .getConfig()
                  .getDownloadConfig()
                  .getGcs()
                  .getPaths()
                  .getX86_64_checksum(),
              Platform.LINUX,
              Architecture.x86_64,
              gcsFile);
      gcsFile.path =
          release.getSpec().getConfig().getDownloadConfig().getGcs().getPaths().getHelmChart();
      helmArtifact =
          ReleaseArtifact.create(
              release
                  .getSpec()
                  .getConfig()
                  .getDownloadConfig()
                  .getGcs()
                  .getPaths()
                  .getHelmChartChecksum(),
              Platform.KUBERNETES,
              null,
              gcsFile);
    } else if (release.getSpec().getConfig().getDownloadConfig().getHttp() != null) {
      dbArtifact =
          ReleaseArtifact.create(
              release
                  .getSpec()
                  .getConfig()
                  .getDownloadConfig()
                  .getHttp()
                  .getPaths()
                  .getX86_64_checksum(),
              Platform.LINUX,
              Architecture.x86_64,
              release.getSpec().getConfig().getDownloadConfig().getHttp().getPaths().getX86_64());
      helmArtifact =
          ReleaseArtifact.create(
              release
                  .getSpec()
                  .getConfig()
                  .getDownloadConfig()
                  .getHttp()
                  .getPaths()
                  .getHelmChartChecksum(),
              Platform.KUBERNETES,
              null,
              release
                  .getSpec()
                  .getConfig()
                  .getDownloadConfig()
                  .getHttp()
                  .getPaths()
                  .getHelmChart());
    } else {
      throw new Exception("Error in adding release, no remote found");
    }
    return Arrays.asList(dbArtifact, helmArtifact);
  }

  // Validate if the artifact remotes have changed.
  private boolean artifactHasChanges(ReleaseArtifact artifact, Release release) {
    if (artifact.getPlatform() == Platform.LINUX) {
      if (artifact.getPackageURL() != null
          && !artifact
              .getPackageURL()
              .equals(
                  release
                      .getSpec()
                      .getConfig()
                      .getDownloadConfig()
                      .getHttp()
                      .getPaths()
                      .getX86_64())) {
        return true;
      }
      if (artifact.getS3File() != null
          && !artifact
              .getS3File()
              .path
              .equals(
                  release
                      .getSpec()
                      .getConfig()
                      .getDownloadConfig()
                      .getS3()
                      .getPaths()
                      .getX86_64())) {
        return true;
      }
      if (artifact.getGcsFile() != null
          && !artifact
              .getGcsFile()
              .path
              .equals(
                  release
                      .getSpec()
                      .getConfig()
                      .getDownloadConfig()
                      .getGcs()
                      .getPaths()
                      .getX86_64())) {
        return true;
      }
    } else {
      if (artifact.getPackageURL() != null
          && !artifact
              .getPackageURL()
              .equals(
                  release
                      .getSpec()
                      .getConfig()
                      .getDownloadConfig()
                      .getHttp()
                      .getPaths()
                      .getHelmChart())) {
        return true;
      }
      if (artifact.getS3File() != null
          && !artifact
              .getS3File()
              .path
              .equals(
                  release
                      .getSpec()
                      .getConfig()
                      .getDownloadConfig()
                      .getS3()
                      .getPaths()
                      .getHelmChart())) {
        return true;
      }
      if (artifact.getGcsFile() != null
          && !artifact
              .getGcsFile()
              .path
              .equals(
                  release
                      .getSpec()
                      .getConfig()
                      .getDownloadConfig()
                      .getGcs()
                      .getPaths()
                      .getHelmChart())) {
        return true;
      }
    }
    return false;
  }
}
