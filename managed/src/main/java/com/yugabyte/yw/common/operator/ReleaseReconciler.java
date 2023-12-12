package com.yugabyte.yw.common.operator;

import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.ReleaseManager.ReleaseMetadata;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.Resource;
import io.fabric8.kubernetes.client.informers.ResourceEventHandler;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.fabric8.kubernetes.client.informers.cache.Lister;
import io.yugabyte.operator.v1alpha1.Release;
import io.yugabyte.operator.v1alpha1.ReleaseStatus;
import io.yugabyte.operator.v1alpha1.releasespec.config.DownloadConfig;
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
      metadata.gcs.paths.x86_64_checksum = downloadConfig.getGcs().getPaths().getX86_64_checksum();
      metadata.gcs.paths.helmChart = downloadConfig.getGcs().getPaths().getHelmChart();
      metadata.gcs.paths.helmChartChecksum =
          downloadConfig.getGcs().getPaths().getHelmChartChecksum();
    }

    if (downloadConfig.getHttp() != null) {
      metadata.http = new ReleaseMetadata.HttpLocation();
      metadata.http.paths = new ReleaseMetadata.PackagePaths();
      metadata.http.paths.x86_64 = downloadConfig.getHttp().getPaths().getX86_64();
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
      String namespace) {
    this.resourceClient = resourceClient;
    this.informer = releaseInformer;
    this.lister = new Lister<>(informer.getIndexer());
    this.releaseManager = releaseManager;
    this.namespace = namespace;
    this.gFlagsValidation = gFlagsValidation;
  }

  @Override
  public void onAdd(Release release) {
    log.info("Adding release {} ", release);
    Pair<String, ReleaseMetadata> releasePair = crToReleaseMetadata(release);
    try {
      releaseManager.addReleaseWithMetadata(releasePair.getFirst(), releasePair.getSecond());
      gFlagsValidation.addDBMetadataFiles(releasePair.getFirst(), releasePair.getSecond());
      releaseManager.updateCurrentReleases();
      updateStatus(release, "Available", true);
    } catch (RuntimeException re) {
      log.error("Error in adding release", re);
      updateStatus(release, "Failed to Download", false);
    }
    log.info("Added release {} ", release);
  }

  @Override
  public void onUpdate(Release oldRelease, Release newRelease) {
    Pair<String, ReleaseMetadata> releasePair = crToReleaseMetadata(newRelease);
    try {
      String version = releasePair.getFirst();
      ReleaseMetadata metadata = releasePair.getSecond();
      // copy chartPath because it already exists.
      ReleaseMetadata existing_rm = releaseManager.getReleaseByVersion(version);
      if (existing_rm != null) {
        if (existing_rm.chartPath != null) {
          log.info("Updating the chartPath because existing metadata has chart path");
          metadata.chartPath = existing_rm.chartPath;
        } else {
          log.info("No existing chart path found, downloading chart");
          releaseManager.downloadYbHelmChart(version, metadata);
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
    log.info("finished update CR release old: {}, new: {}", oldRelease, newRelease);
  }

  @Override
  public void onDelete(Release release, boolean deletedFinalStateUnknown) {
    log.info("removing Release {}", release);
    Pair<String, ReleaseMetadata> releasePair = crToReleaseMetadata(release);
    try {
      releaseManager.removeRelease(releasePair.getFirst());
      releaseManager.updateCurrentReleases();
    } catch (RuntimeException re) {
      log.error("Error in deleting release", re);
    }
    log.info("Removed release {}", release);
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
}
