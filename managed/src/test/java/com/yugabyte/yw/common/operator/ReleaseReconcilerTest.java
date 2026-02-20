// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.operator;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.models.OperatorResource;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.NonNamespaceOperation;
import io.fabric8.kubernetes.client.dsl.Resource;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.fabric8.kubernetes.client.informers.cache.Indexer;
import io.fabric8.kubernetes.client.utils.Serialization;
import io.yugabyte.operator.v1alpha1.Release;
import io.yugabyte.operator.v1alpha1.ReleaseSpec;
import io.yugabyte.operator.v1alpha1.releasespec.Config;
import io.yugabyte.operator.v1alpha1.releasespec.config.DownloadConfig;
import io.yugabyte.operator.v1alpha1.releasespec.config.downloadconfig.Gcs;
import io.yugabyte.operator.v1alpha1.releasespec.config.downloadconfig.S3;
import io.yugabyte.operator.v1alpha1.releasespec.config.downloadconfig.gcs.CredentialsJsonSecret;
import io.yugabyte.operator.v1alpha1.releasespec.config.downloadconfig.s3.Paths;
import io.yugabyte.operator.v1alpha1.releasespec.config.downloadconfig.s3.SecretAccessKeySecret;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class ReleaseReconcilerTest extends FakeDBApplication {

  @Mock SharedIndexInformer<Release> releaseInformer;
  @Mock MixedOperation<Release, KubernetesResourceList<Release>, Resource<Release>> resourceClient;
  @Mock ReleaseManager releaseManager;
  @Mock GFlagsValidation gFlagsValidation;
  @Mock RuntimeConfGetter confGetter;
  @Mock OperatorUtils operatorUtils;
  @Mock Indexer<Release> indexer;

  @Mock
  NonNamespaceOperation<Release, KubernetesResourceList<Release>, Resource<Release>>
      inNamespaceResource;

  @Mock Resource<Release> releaseResource;

  private ReleaseReconciler releaseReconciler;
  private static final String NAMESPACE = "test-namespace";
  private static final String RELEASE_VERSION = "2.21.0.0-b1";

  @Before
  public void setup() {
    when(releaseInformer.getIndexer()).thenReturn(indexer);
    when(resourceClient.inNamespace(anyString())).thenReturn(inNamespaceResource);
    when(inNamespaceResource.resource(any(Release.class))).thenReturn(releaseResource);
    // when(inNamespaceResource.withName(anyString())).thenReturn(releaseResource);
    releaseReconciler =
        new ReleaseReconciler(
            releaseInformer,
            resourceClient,
            releaseManager,
            gFlagsValidation,
            NAMESPACE,
            confGetter,
            operatorUtils);
  }

  private Release createReleaseCr(String name) {
    Release release = new Release();
    ObjectMeta metadata = new ObjectMeta();
    metadata.setName(name);
    metadata.setNamespace(NAMESPACE);
    metadata.setUid(UUID.randomUUID().toString());
    release.setMetadata(metadata);
    ReleaseSpec spec = new ReleaseSpec();
    Config config = new Config();
    config.setVersion(RELEASE_VERSION);
    spec.setConfig(config);
    release.setSpec(spec);
    return release;
  }

  /**
   * Creates a Release CR with S3 download config and a secret reference (SecretAccessKeySecret).
   */
  private Release createReleaseCrWithS3Secret(String name, String secretName, String secretNs) {
    Release release = createReleaseCr(name);
    DownloadConfig downloadConfig = new DownloadConfig();
    S3 s3 = new S3();
    Paths paths = new Paths();
    paths.setX86_64("s3://bucket/path/x86_64.tar.gz");
    paths.setX86_64_checksum("checksum");
    paths.setHelmChart("s3://bucket/path/helm.tgz");
    paths.setHelmChartChecksum("chartchecksum");
    s3.setPaths(paths);
    SecretAccessKeySecret secretRef = new SecretAccessKeySecret();
    secretRef.setName(secretName);
    secretRef.setNamespace(secretNs);
    s3.setSecretAccessKeySecret(secretRef);
    downloadConfig.setS3(s3);
    release.getSpec().getConfig().setDownloadConfig(downloadConfig);
    return release;
  }

  /**
   * Creates a Release CR with GCS (GCP) download config and a secret reference
   * (CredentialsJsonSecret).
   */
  private Release createReleaseCrWithGcsSecret(String name, String secretName, String secretNs) {
    Release release = createReleaseCr(name);
    DownloadConfig downloadConfig = new DownloadConfig();
    Gcs gcs = new Gcs();
    io.yugabyte.operator.v1alpha1.releasespec.config.downloadconfig.gcs.Paths gcsPaths =
        new io.yugabyte.operator.v1alpha1.releasespec.config.downloadconfig.gcs.Paths();
    gcsPaths.setX86_64("gs://bucket/path/x86_64.tar.gz");
    gcsPaths.setX86_64_checksum("checksum");
    gcsPaths.setHelmChart("gs://bucket/path/helm.tgz");
    gcsPaths.setHelmChartChecksum("chartchecksum");
    gcs.setPaths(gcsPaths);
    CredentialsJsonSecret secretRef = new CredentialsJsonSecret();
    secretRef.setName(secretName);
    secretRef.setNamespace(secretNs);
    gcs.setCredentialsJsonSecret(secretRef);
    downloadConfig.setGcs(gcs);
    release.getSpec().getConfig().setDownloadConfig(downloadConfig);
    return release;
  }

  @Test
  public void testOnAddAddsResourceToTrackedResources() {
    // Create release so reconciler sees it as existing and only updates status (no download path).
    com.yugabyte.yw.models.Release.create(RELEASE_VERSION, "LTS");
    when(confGetter.getGlobalConf(GlobalConfKeys.enableReleasesRedesign)).thenReturn(true);
    Release releaseCr = createReleaseCr(RELEASE_VERSION);

    releaseReconciler.onAdd(releaseCr);

    assertEquals(1, releaseReconciler.getTrackedResources().size());
    KubernetesResourceDetails details = releaseReconciler.getTrackedResources().iterator().next();
    assertEquals(RELEASE_VERSION, details.name);
    assertEquals(NAMESPACE, details.namespace);

    // Verify OperatorResource entries were persisted in the database
    List<OperatorResource> allResources = OperatorResource.getAll();
    assertEquals(1, allResources.size());
    assertTrue(
        "OperatorResource name should contain the release version",
        allResources.get(0).getName().contains(RELEASE_VERSION));
    Release rRelease = Serialization.unmarshal(allResources.get(0).getData(), Release.class);
    assertEquals(RELEASE_VERSION, rRelease.getMetadata().getName());
    assertEquals(NAMESPACE, rRelease.getMetadata().getNamespace());
    assertEquals(RELEASE_VERSION, rRelease.getSpec().getConfig().getVersion());
  }

  @Test
  public void testOnAddWithS3SecretTracksReleaseAndSecret() throws Exception {
    when(confGetter.getGlobalConf(GlobalConfKeys.enableReleasesRedesign)).thenReturn(true);
    when(inNamespaceResource.withName(anyString())).thenReturn(releaseResource);
    when(releaseResource.patch(any(Release.class))).thenAnswer(inv -> inv.getArgument(0));
    when(releaseResource.replaceStatus()).thenReturn(null); // deprecated but used by reconciler
    doNothing()
        .when(releaseManager)
        .downloadYbHelmChart(anyString(), any(com.yugabyte.yw.models.Release.class));

    String secretName = "aws-release-secret";
    String secretNamespace = NAMESPACE;
    Release releaseCr = createReleaseCrWithS3Secret(RELEASE_VERSION, secretName, secretNamespace);

    // When resolving the secret, add it as a dependency via the ResourceTracker.
    doAnswer(
            inv -> {
              ResourceTracker tracker = inv.getArgument(3);
              KubernetesResourceDetails owner = inv.getArgument(4);
              String name = inv.getArgument(0);
              String ns = inv.getArgument(1);
              tracker.trackDependency(
                  owner, new KubernetesResourceDetails(name, ns != null ? ns : "default"));
              return "mock-secret-key";
            })
        .when(operatorUtils)
        .getAndParseSecretForKey(
            eq(secretName),
            eq(secretNamespace),
            eq("AWS_SECRET_ACCESS_KEY"),
            any(ResourceTracker.class),
            any(KubernetesResourceDetails.class));

    releaseReconciler.onAdd(releaseCr);

    assertEquals(2, releaseReconciler.getTrackedResources().size());
    assertTrue(
        releaseReconciler.getTrackedResources().stream()
            .anyMatch(d -> RELEASE_VERSION.equals(d.name) && NAMESPACE.equals(d.namespace)));
    assertTrue(
        releaseReconciler.getTrackedResources().stream()
            .anyMatch(d -> secretName.equals(d.name) && secretNamespace.equals(d.namespace)));

    // Verify the secret is tracked as a dependency of the release
    ResourceTracker tracker = releaseReconciler.getResourceTracker();
    KubernetesResourceDetails releaseDetails =
        tracker.getResourceDependencies().keySet().stream()
            .filter(d -> RELEASE_VERSION.equals(d.name))
            .findFirst()
            .orElse(null);
    assertTrue(releaseDetails != null);
    assertEquals(1, tracker.getDependencies(releaseDetails).size());

    // Verify OperatorResource entries were persisted in the database (release + secret)
    List<OperatorResource> allResources = OperatorResource.getAll();
    assertEquals(2, allResources.size());
    assertTrue(
        "OperatorResource entries should include the release",
        allResources.stream().anyMatch(r -> r.getName().contains(RELEASE_VERSION)));
    assertTrue(
        "OperatorResource entries should include the secret",
        allResources.stream().anyMatch(r -> r.getName().contains(secretName)));
    OperatorResource releaseResource =
        allResources.stream()
            .filter(r -> r.getName().contains(RELEASE_VERSION))
            .findFirst()
            .orElse(null);
    Release rRelease = Serialization.unmarshal(releaseResource.getData(), Release.class);
    assertEquals(RELEASE_VERSION, rRelease.getMetadata().getName());
    assertEquals(NAMESPACE, rRelease.getMetadata().getNamespace());
    assertEquals(RELEASE_VERSION, rRelease.getSpec().getConfig().getVersion());
    assertEquals(
        "s3://bucket/path/x86_64.tar.gz",
        rRelease.getSpec().getConfig().getDownloadConfig().getS3().getPaths().getX86_64());
  }

  @Test
  public void testOnAddWithGcsSecretTracksReleaseAndSecret() throws Exception {
    when(confGetter.getGlobalConf(GlobalConfKeys.enableReleasesRedesign)).thenReturn(true);
    when(inNamespaceResource.withName(anyString())).thenReturn(releaseResource);
    when(releaseResource.patch(any(Release.class))).thenAnswer(inv -> inv.getArgument(0));
    when(releaseResource.replaceStatus()).thenReturn(null); // deprecated but used by reconciler
    doNothing()
        .when(releaseManager)
        .downloadYbHelmChart(anyString(), any(com.yugabyte.yw.models.Release.class));

    String secretName = "gcp-credentials-secret";
    String secretNamespace = NAMESPACE;
    Release releaseCr = createReleaseCrWithGcsSecret(RELEASE_VERSION, secretName, secretNamespace);

    // When resolving the secret, add it as a dependency via the ResourceTracker.
    doAnswer(
            inv -> {
              ResourceTracker tracker = inv.getArgument(3);
              KubernetesResourceDetails owner = inv.getArgument(4);
              String name = inv.getArgument(0);
              String ns = inv.getArgument(1);
              tracker.trackDependency(
                  owner, new KubernetesResourceDetails(name, ns != null ? ns : "default"));
              return "{\"type\":\"service_account\"}";
            })
        .when(operatorUtils)
        .getAndParseSecretForKey(
            eq(secretName),
            eq(secretNamespace),
            eq("CREDENTIALS_JSON"),
            any(ResourceTracker.class),
            any(KubernetesResourceDetails.class));

    releaseReconciler.onAdd(releaseCr);

    assertEquals(2, releaseReconciler.getTrackedResources().size());
    assertTrue(
        releaseReconciler.getTrackedResources().stream()
            .anyMatch(d -> RELEASE_VERSION.equals(d.name) && NAMESPACE.equals(d.namespace)));
    assertTrue(
        releaseReconciler.getTrackedResources().stream()
            .anyMatch(d -> secretName.equals(d.name) && secretNamespace.equals(d.namespace)));

    // Verify the secret is tracked as a dependency of the release
    ResourceTracker tracker = releaseReconciler.getResourceTracker();
    KubernetesResourceDetails releaseDetails =
        tracker.getResourceDependencies().keySet().stream()
            .filter(d -> RELEASE_VERSION.equals(d.name))
            .findFirst()
            .orElse(null);
    assertTrue(releaseDetails != null);
    assertEquals(1, tracker.getDependencies(releaseDetails).size());

    // Verify OperatorResource entries were persisted in the database (release + secret)
    List<OperatorResource> allResources = OperatorResource.getAll();
    assertEquals(2, allResources.size());
    assertTrue(
        "OperatorResource entries should include the release",
        allResources.stream().anyMatch(r -> r.getName().contains(RELEASE_VERSION)));
    assertTrue(
        "OperatorResource entries should include the GCS secret",
        allResources.stream().anyMatch(r -> r.getName().contains(secretName)));
    OperatorResource releaseResource =
        allResources.stream()
            .filter(r -> r.getName().contains(RELEASE_VERSION))
            .findFirst()
            .orElse(null);
    Release rRelease = Serialization.unmarshal(releaseResource.getData(), Release.class);
    assertEquals(RELEASE_VERSION, rRelease.getMetadata().getName());
    assertEquals(NAMESPACE, rRelease.getMetadata().getNamespace());
    assertEquals(RELEASE_VERSION, rRelease.getSpec().getConfig().getVersion());
    assertEquals(
        "gs://bucket/path/x86_64.tar.gz",
        rRelease.getSpec().getConfig().getDownloadConfig().getGcs().getPaths().getX86_64());
  }

  @Test
  public void testOnDeleteRemovesOperatorResource() {
    com.yugabyte.yw.models.Release.create(RELEASE_VERSION, "LTS");
    when(confGetter.getGlobalConf(GlobalConfKeys.enableReleasesRedesign)).thenReturn(true);
    Release releaseCr = createReleaseCr(RELEASE_VERSION);

    releaseReconciler.onAdd(releaseCr);
    assertEquals(1, OperatorResource.getAll().size());

    // Delete
    releaseReconciler.onDelete(releaseCr, false);

    assertTrue(
        "Tracked resources should be empty after delete",
        releaseReconciler.getTrackedResources().isEmpty());
    assertTrue(
        "OperatorResource entries should be removed after delete",
        OperatorResource.getAll().isEmpty());
  }

  @Test
  public void testOnDeleteWithS3SecretRemovesAllOperatorResources() throws Exception {
    when(confGetter.getGlobalConf(GlobalConfKeys.enableReleasesRedesign)).thenReturn(true);
    when(inNamespaceResource.withName(anyString())).thenReturn(releaseResource);
    when(releaseResource.patch(any(Release.class))).thenAnswer(inv -> inv.getArgument(0));
    when(releaseResource.replaceStatus()).thenReturn(null);
    doNothing()
        .when(releaseManager)
        .downloadYbHelmChart(anyString(), any(com.yugabyte.yw.models.Release.class));

    String secretName = "aws-delete-test-secret";
    Release releaseCr = createReleaseCrWithS3Secret(RELEASE_VERSION, secretName, NAMESPACE);

    doAnswer(
            inv -> {
              ResourceTracker tracker = inv.getArgument(3);
              KubernetesResourceDetails owner = inv.getArgument(4);
              String name = inv.getArgument(0);
              String ns = inv.getArgument(1);
              tracker.trackDependency(
                  owner, new KubernetesResourceDetails(name, ns != null ? ns : "default"));
              return "mock-secret-key";
            })
        .when(operatorUtils)
        .getAndParseSecretForKey(
            eq(secretName),
            eq(NAMESPACE),
            eq("AWS_SECRET_ACCESS_KEY"),
            any(ResourceTracker.class),
            any(KubernetesResourceDetails.class));

    releaseReconciler.onAdd(releaseCr);
    assertEquals(2, OperatorResource.getAll().size());

    // Delete - should remove both release and orphaned secret dependency
    releaseReconciler.onDelete(releaseCr, false);

    assertTrue(
        "Tracked resources should be empty after delete",
        releaseReconciler.getTrackedResources().isEmpty());
    assertTrue(
        "All OperatorResource entries (release + orphaned secret) should be removed after delete",
        OperatorResource.getAll().isEmpty());
  }
}
