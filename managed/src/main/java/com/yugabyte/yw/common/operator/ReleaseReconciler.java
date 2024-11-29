package com.yugabyte.yw.common.operator;

import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.common.ReleaseContainer;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.ReleaseManager.ReleaseMetadata;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.models.ReleaseArtifact;
import com.yugabyte.yw.models.ReleaseArtifact.Platform;
import io.ebean.DB;
import io.ebean.Transaction;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.Resource;
import io.fabric8.kubernetes.client.informers.ResourceEventHandler;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.fabric8.kubernetes.client.informers.cache.Lister;
import io.yugabyte.operator.v1alpha1.Release;
import io.yugabyte.operator.v1alpha1.ReleaseStatus;
import io.yugabyte.operator.v1alpha1.releasespec.config.DownloadConfig;
import java.util.Arrays;
import java.util.List;
import lombok.extern.slf4j.Slf4j;
import org.yb.util.Pair;

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

  public static Pair<String, ReleaseMetadata> crToReleaseMetadata(Release release) {
    DownloadConfig downloadConfig = release.getSpec().getConfig().getDownloadConfig();
    String version = release.getSpec().getConfig().getVersion();
    ReleaseMetadata metadata = ReleaseMetadata.create(version);
    if (downloadConfig.getS3() != null) {
      metadata.s3 = new ReleaseMetadata.S3Location();
      metadata.s3.paths = new ReleaseMetadata.PackagePaths();
      metadata.s3.accessKeyId = downloadConfig.getS3().getAccessKeyId();
      metadata.s3.secretAccessKey = downloadConfig.getS3().getSecretAccessKey();
      metadata.s3.paths.x86_64 = downloadConfig.getS3().getPaths().getX86_64();
      metadata.filePath = downloadConfig.getS3().getPaths().getX86_64();
      metadata.s3.paths.x86_64_checksum = downloadConfig.getS3().getPaths().getX86_64_checksum();
      metadata.s3.paths.helmChart = downloadConfig.getS3().getPaths().getHelmChart();
      metadata.s3.paths.helmChartChecksum =
          downloadConfig.getS3().getPaths().getHelmChartChecksum();
    }

    if (downloadConfig.getGcs() != null) {
      metadata.gcs = new ReleaseMetadata.GCSLocation();
      metadata.gcs.paths = new ReleaseMetadata.PackagePaths();
      metadata.gcs.credentialsJson = downloadConfig.getGcs().getCredentialsJson();
      metadata.gcs.paths.x86_64 = downloadConfig.getGcs().getPaths().getX86_64();
      metadata.filePath = downloadConfig.getGcs().getPaths().getX86_64();
      metadata.gcs.paths.x86_64_checksum = downloadConfig.getGcs().getPaths().getX86_64_checksum();
      metadata.gcs.paths.helmChart = downloadConfig.getGcs().getPaths().getHelmChart();
      metadata.gcs.paths.helmChartChecksum =
          downloadConfig.getGcs().getPaths().getHelmChartChecksum();
    }

    if (downloadConfig.getHttp() != null) {
      metadata.http = new ReleaseMetadata.HttpLocation();
      metadata.http.paths = new ReleaseMetadata.PackagePaths();
      metadata.http.paths.x86_64 = downloadConfig.getHttp().getPaths().getX86_64();
      metadata.filePath = downloadConfig.getHttp().getPaths().getX86_64();
      metadata.http.paths.x86_64_checksum =
          downloadConfig.getHttp().getPaths().getX86_64_checksum();
      metadata.http.paths.helmChart = downloadConfig.getHttp().getPaths().getHelmChart();
      metadata.http.paths.helmChartChecksum =
          downloadConfig.getHttp().getPaths().getHelmChartChecksum();
    }
    Pair<String, ReleaseMetadata> output = new Pair<>(version, metadata);
    return output;
  }

  public ReleaseReconciler(
      SharedIndexInformer<Release> releaseInformer,
      MixedOperation<Release, KubernetesResourceList<Release>, Resource<Release>> resourceClient,
      ReleaseManager releaseManager,
      GFlagsValidation gFlagsValidation,
      String namespace,
      RuntimeConfGetter confGetter) {
    this.resourceClient = resourceClient;
    this.informer = releaseInformer;
    this.lister = new Lister<>(informer.getIndexer());
    this.releaseManager = releaseManager;
    this.namespace = namespace;
    this.gFlagsValidation = gFlagsValidation;
    this.confGetter = confGetter;
  }

  @Override
  public void onAdd(Release release) {
    log.info("Adding release {} ", release.getMetadata().getName());
    if (confGetter.getGlobalConf(GlobalConfKeys.enableReleasesRedesign)) {
      String version = release.getSpec().getConfig().getVersion();

      com.yugabyte.yw.models.Release ybRelease =
          com.yugabyte.yw.models.Release.getByVersion(version);
      if (ybRelease != null) {
        log.error("release version already exists, cannot create: " + version);
        // If it already exists, we should just use it.
        updateStatus(release, "Release version " + version + "already exists", true);
        return;
      }
      try (Transaction transaction = DB.beginTransaction()) {
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
    if (confGetter.getGlobalConf(GlobalConfKeys.enableReleasesRedesign)) {
      String version = newRelease.getSpec().getConfig().getVersion();
      com.yugabyte.yw.models.Release ybRelease =
          com.yugabyte.yw.models.Release.getByVersion(version);
      if (ybRelease == null) {
        log.warn("no release found for version " + version + ". creating it");
        ybRelease = com.yugabyte.yw.models.Release.create(version, "LTS");
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
    log.info("Removing Release {}", release.getMetadata().getName());
    Pair<String, ReleaseMetadata> releasePair = crToReleaseMetadata(release);
    try {
      releaseManager.removeRelease(releasePair.getFirst());
      releaseManager.updateCurrentReleases();
    } catch (RuntimeException re) {
      log.error("Error in deleting release", re);
    }
    log.info("Removed release {}", release.getMetadata().getName());
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
    if (release.getSpec().getConfig().getDownloadConfig().getS3() != null) {
      ReleaseArtifact.S3File s3File = new ReleaseArtifact.S3File();
      s3File.path =
          release.getSpec().getConfig().getDownloadConfig().getS3().getPaths().getX86_64();
      s3File.accessKeyId =
          release.getSpec().getConfig().getDownloadConfig().getS3().getAccessKeyId();
      s3File.secretAccessKey =
          release.getSpec().getConfig().getDownloadConfig().getS3().getSecretAccessKey();
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
}
