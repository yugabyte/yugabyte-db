// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.operator;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.operator.utils.KubernetesClientFactory;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.models.OperatorResource;
import io.fabric8.kubernetes.api.model.HasMetadata;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.fabric8.kubernetes.client.KubernetesClientBuilder;
import io.fabric8.kubernetes.client.dsl.NamespaceableResource;
import io.fabric8.kubernetes.client.utils.Serialization;
import io.yugabyte.operator.v1alpha1.Release;
import io.yugabyte.operator.v1alpha1.ReleaseSpec;
import java.util.UUID;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.MockedConstruction;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class OperatorResourceRestorerTest extends FakeDBApplication {

  @Mock RuntimeConfGetter confGetter;
  @Mock OperatorUtils operatorUtils;
  @Mock KubernetesClient mockClient;
  @Mock KubernetesClientFactory kubernetesClientFactory;

  private OperatorResourceRestorer restorer;
  private MockedConstruction<KubernetesClientBuilder> clientBuilderMock;
  private static final String NAMESPACE = "test-namespace";

  @Before
  public void setup() {
    restorer = new OperatorResourceRestorer(confGetter, operatorUtils, kubernetesClientFactory);

    when(kubernetesClientFactory.getKubernetesClientWithConfig(any())).thenReturn(mockClient);
  }

  @After
  public void tearDown() {
    if (clientBuilderMock != null) {
      clientBuilderMock.close();
    }
  }

  // ---------------------------------------------------------------------------
  // isStrictlyGreater tests
  // ---------------------------------------------------------------------------

  @Test
  public void testIsStrictlyGreater_greaterNumericVersion() {
    assertTrue(OperatorResourceRestorer.isStrictlyGreater("200", "100"));
  }

  @Test
  public void testIsStrictlyGreater_equalVersions() {
    assertFalse(OperatorResourceRestorer.isStrictlyGreater("100", "100"));
  }

  @Test
  public void testIsStrictlyGreater_lesserVersion() {
    assertFalse(OperatorResourceRestorer.isStrictlyGreater("50", "100"));
  }

  @Test
  public void testIsStrictlyGreater_storedNull() {
    assertFalse(OperatorResourceRestorer.isStrictlyGreater(null, "100"));
  }

  @Test
  public void testIsStrictlyGreater_k8sNull() {
    assertTrue(OperatorResourceRestorer.isStrictlyGreater("100", null));
  }

  @Test
  public void testIsStrictlyGreater_bothNull() {
    assertFalse(OperatorResourceRestorer.isStrictlyGreater(null, null));
  }

  @Test
  public void testIsStrictlyGreater_largeNumbers() {
    assertTrue(OperatorResourceRestorer.isStrictlyGreater("99999999999", "99999999998"));
    assertFalse(OperatorResourceRestorer.isStrictlyGreater("99999999998", "99999999999"));
  }

  @Test
  public void testIsStrictlyGreater_nonNumericFallsBackToLexicographic() {
    assertTrue(OperatorResourceRestorer.isStrictlyGreater("b", "a"));
    assertFalse(OperatorResourceRestorer.isStrictlyGreater("a", "b"));
    assertFalse(OperatorResourceRestorer.isStrictlyGreater("abc", "abc"));
  }

  @Test
  public void testIsStrictlyGreater_mixedNumericNonNumericFallback() {
    // One parseable, one not -> NumberFormatException -> lexicographic
    assertTrue(OperatorResourceRestorer.isStrictlyGreater("z", "100"));
    assertFalse(OperatorResourceRestorer.isStrictlyGreater("100", "z"));
  }

  @Test
  public void testIsStrictlyGreater_zeroVersions() {
    assertFalse(OperatorResourceRestorer.isStrictlyGreater("0", "0"));
    assertTrue(OperatorResourceRestorer.isStrictlyGreater("1", "0"));
    assertFalse(OperatorResourceRestorer.isStrictlyGreater("0", "1"));
  }

  // ---------------------------------------------------------------------------
  // restoreOperatorResources - early exit paths
  // ---------------------------------------------------------------------------

  @Test
  public void testRestore_skipsWhenOperatorNotEnabled() {
    when(confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)).thenReturn(false);

    restorer.restoreOperatorResources();

    verify(confGetter, never()).getGlobalConf(GlobalConfKeys.KubernetesOperatorNamespace);
  }

  @Test
  public void testRestore_skipsWhenNoResourcesInDb() {
    when(confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)).thenReturn(true);
    // FakeDBApplication provides an empty DB, so OperatorResource.getAll() returns []

    restorer.restoreOperatorResources();

    // Should not attempt to build a K8s client when there are no resources
    verify(kubernetesClientFactory, never()).getKubernetesClientWithConfig(any());
  }

  // ---------------------------------------------------------------------------
  // restoreOperatorResources - resource creation/update/skip paths
  // ---------------------------------------------------------------------------

  @Test
  @SuppressWarnings("unchecked")
  public void testRestore_createsResourceWhenNotInK8s() {
    when(confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)).thenReturn(true);
    when(confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorNamespace))
        .thenReturn(NAMESPACE);

    Release release = createRelease("my-release", NAMESPACE, "100");
    String yaml = Serialization.asYaml(release);
    OperatorResource.createOrUpdate("release:" + NAMESPACE + ":my-release", yaml);

    NamespaceableResource<HasMetadata> namespaceable = mock(NamespaceableResource.class);
    NamespaceableResource<HasMetadata> inNs = mock(NamespaceableResource.class);
    when(mockClient.resource(any(HasMetadata.class))).thenReturn(namespaceable);
    when(namespaceable.inNamespace(anyString())).thenReturn(inNs);
    when(inNs.get()).thenReturn(null); // resource does not exist in K8s

    restorer.restoreOperatorResources();

    verify(inNs).create();
    verify(inNs, never()).patch();
  }

  @Test
  @SuppressWarnings("unchecked")
  public void testRestore_updatesResourceWhenStoredVersionIsNewer() {
    when(confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)).thenReturn(true);
    when(confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorNamespace))
        .thenReturn(NAMESPACE);

    Release storedRelease = createRelease("my-release", NAMESPACE, "200");
    String yaml = Serialization.asYaml(storedRelease);
    OperatorResource.createOrUpdate("release:" + NAMESPACE + ":my-release", yaml);

    Release existingRelease = createRelease("my-release", NAMESPACE, "100");

    NamespaceableResource<HasMetadata> namespaceable = mock(NamespaceableResource.class);
    NamespaceableResource<HasMetadata> inNs = mock(NamespaceableResource.class);
    when(mockClient.resource(any(HasMetadata.class))).thenReturn(namespaceable);
    when(namespaceable.inNamespace(anyString())).thenReturn(inNs);
    when(inNs.get()).thenReturn(existingRelease);

    restorer.restoreOperatorResources();

    verify(inNs).patch();
    verify(inNs, never()).create();
  }

  @Test
  @SuppressWarnings("unchecked")
  public void testRestore_skipsResourceWhenK8sVersionIsNewerOrEqual() {
    when(confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)).thenReturn(true);
    when(confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorNamespace))
        .thenReturn(NAMESPACE);

    Release storedRelease = createRelease("my-release", NAMESPACE, "100");
    String yaml = Serialization.asYaml(storedRelease);
    OperatorResource.createOrUpdate("release:" + NAMESPACE + ":my-release", yaml);

    Release existingRelease = createRelease("my-release", NAMESPACE, "200");

    NamespaceableResource<HasMetadata> namespaceable = mock(NamespaceableResource.class);
    NamespaceableResource<HasMetadata> inNs = mock(NamespaceableResource.class);
    when(mockClient.resource(any(HasMetadata.class))).thenReturn(namespaceable);
    when(namespaceable.inNamespace(anyString())).thenReturn(inNs);
    when(inNs.get()).thenReturn(existingRelease);

    restorer.restoreOperatorResources();

    verify(inNs, never()).create();
    verify(inNs, never()).patch();
  }

  @Test
  public void testRestore_skipsResourceWithBlankData() {
    when(confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)).thenReturn(true);
    when(confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorNamespace))
        .thenReturn(NAMESPACE);

    OperatorResource.createOrUpdate("release:" + NAMESPACE + ":empty-release", "");

    restorer.restoreOperatorResources();

    verify(mockClient, never()).resource(any(HasMetadata.class));
  }

  @Test
  public void testRestore_skipsUnknownResourceType() {
    when(confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)).thenReturn(true);
    when(confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorNamespace))
        .thenReturn(NAMESPACE);

    OperatorResource.createOrUpdate(
        "unknowntype:" + NAMESPACE + ":some-resource", "apiVersion: v1\nkind: Unknown");

    restorer.restoreOperatorResources();

    verify(mockClient, never()).resource(any(HasMetadata.class));
  }

  @Test
  @SuppressWarnings("unchecked")
  public void testRestore_handlesMultipleResourcesMixed() {
    when(confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)).thenReturn(true);
    when(confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorNamespace))
        .thenReturn(NAMESPACE);

    // Resource 1: will be created (not in K8s)
    Release release1 = createRelease("release-1", NAMESPACE, "100");
    OperatorResource.createOrUpdate(
        "release:" + NAMESPACE + ":release-1", Serialization.asYaml(release1));

    // Resource 2: will be updated (stored version newer)
    Release release2 = createRelease("release-2", NAMESPACE, "300");
    OperatorResource.createOrUpdate(
        "release:" + NAMESPACE + ":release-2", Serialization.asYaml(release2));

    // Resource 3: will be skipped (K8s version newer)
    Release release3 = createRelease("release-3", NAMESPACE, "50");
    OperatorResource.createOrUpdate(
        "release:" + NAMESPACE + ":release-3", Serialization.asYaml(release3));

    Release existing2 = createRelease("release-2", NAMESPACE, "100");
    Release existing3 = createRelease("release-3", NAMESPACE, "200");

    NamespaceableResource<HasMetadata> namespaceable = mock(NamespaceableResource.class);
    NamespaceableResource<HasMetadata> inNs1 = mock(NamespaceableResource.class);
    NamespaceableResource<HasMetadata> inNs2 = mock(NamespaceableResource.class);
    NamespaceableResource<HasMetadata> inNs3 = mock(NamespaceableResource.class);

    when(mockClient.resource(any(HasMetadata.class))).thenReturn(namespaceable);

    // Return different inNs mocks based on call order
    when(namespaceable.inNamespace(anyString()))
        .thenReturn(inNs1)
        .thenReturn(inNs1)
        .thenReturn(inNs2)
        .thenReturn(inNs2)
        .thenReturn(inNs3);

    when(inNs1.get()).thenReturn(null); // not in K8s -> create
    when(inNs2.get()).thenReturn(existing2); // older -> update
    // inNs3 only called for get, not for create/patch
    when(inNs3.get()).thenReturn(existing3); // newer -> skip

    restorer.restoreOperatorResources();

    verify(inNs1).create();
    verify(inNs2).patch();
    verify(inNs3, never()).create();
    verify(inNs3, never()).patch();
  }

  @Test
  @SuppressWarnings("unchecked")
  public void testRestore_continuesOnErrorForIndividualResource() {
    when(confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)).thenReturn(true);
    when(confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorNamespace))
        .thenReturn(NAMESPACE);

    // Resource 1: will throw an exception
    Release release1 = createRelease("release-error", NAMESPACE, "100");
    OperatorResource.createOrUpdate(
        "release:" + NAMESPACE + ":release-error", Serialization.asYaml(release1));

    // Resource 2: should still be processed
    Release release2 = createRelease("release-ok", NAMESPACE, "100");
    OperatorResource.createOrUpdate(
        "release:" + NAMESPACE + ":release-ok", Serialization.asYaml(release2));

    NamespaceableResource<HasMetadata> namespaceable = mock(NamespaceableResource.class);
    NamespaceableResource<HasMetadata> inNsBad = mock(NamespaceableResource.class);
    NamespaceableResource<HasMetadata> inNsGood = mock(NamespaceableResource.class);

    when(mockClient.resource(any(HasMetadata.class))).thenReturn(namespaceable);
    when(namespaceable.inNamespace(anyString()))
        .thenReturn(inNsBad)
        .thenReturn(inNsBad)
        .thenReturn(inNsGood)
        .thenReturn(inNsGood);

    // get() returns null (not in K8s), but create() throws for the first resource
    when(inNsBad.get()).thenReturn(null);
    when(inNsBad.create()).thenThrow(new RuntimeException("K8s API error on create"));
    when(inNsGood.get()).thenReturn(null);

    restorer.restoreOperatorResources();

    // Second resource should still be created despite first one failing
    verify(inNsGood).create();
  }

  @Test
  @SuppressWarnings("unchecked")
  public void testRestore_stripsServerManagedMetadata() {
    when(confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)).thenReturn(true);
    when(confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorNamespace))
        .thenReturn(NAMESPACE);

    Release release = createRelease("my-release", NAMESPACE, "100");
    release.getMetadata().setUid(UUID.randomUUID().toString());
    release.getMetadata().setCreationTimestamp("2025-01-01T00:00:00Z");
    release.getMetadata().setGeneration(5L);
    release.getMetadata().setSelfLink("/api/v1/releases/my-release");
    String yaml = Serialization.asYaml(release);
    OperatorResource.createOrUpdate("release:" + NAMESPACE + ":my-release", yaml);

    NamespaceableResource<HasMetadata> namespaceable = mock(NamespaceableResource.class);
    NamespaceableResource<HasMetadata> inNs = mock(NamespaceableResource.class);
    when(mockClient.resource(any(HasMetadata.class))).thenReturn(namespaceable);
    when(namespaceable.inNamespace(anyString())).thenReturn(inNs);
    when(inNs.get()).thenReturn(null);

    restorer.restoreOperatorResources();

    org.mockito.ArgumentCaptor<HasMetadata> captor =
        org.mockito.ArgumentCaptor.forClass(HasMetadata.class);
    verify(mockClient, times(2)).resource(captor.capture());
    // First call is for getExistingResource, second is for create
    HasMetadata createdResource = captor.getAllValues().get(0);
    org.junit.Assert.assertNull(createdResource.getMetadata().getUid());
    org.junit.Assert.assertNull(createdResource.getMetadata().getCreationTimestamp());
    org.junit.Assert.assertNull(createdResource.getMetadata().getGeneration());
    org.junit.Assert.assertNull(createdResource.getMetadata().getSelfLink());
  }

  @Test
  @SuppressWarnings("unchecked")
  public void testRestore_usesConfiguredNamespace() {
    when(confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)).thenReturn(true);
    when(confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorNamespace))
        .thenReturn("custom-ns");

    Release release = createRelease("my-release", "custom-ns", "100");
    String yaml = Serialization.asYaml(release);
    OperatorResource.createOrUpdate("release:custom-ns:my-release", yaml);

    NamespaceableResource<HasMetadata> namespaceable = mock(NamespaceableResource.class);
    NamespaceableResource<HasMetadata> inNs = mock(NamespaceableResource.class);
    when(mockClient.resource(any(HasMetadata.class))).thenReturn(namespaceable);
    when(namespaceable.inNamespace(anyString())).thenReturn(inNs);
    when(inNs.get()).thenReturn(null);

    restorer.restoreOperatorResources();

    // Called twice: once for getExistingResource, once for create
    verify(namespaceable, times(2)).inNamespace("custom-ns");
  }

  // ---------------------------------------------------------------------------
  // Helpers
  // ---------------------------------------------------------------------------

  private Release createRelease(String name, String namespace, String resourceVersion) {
    Release release = new Release();
    ObjectMeta metadata = new ObjectMeta();
    metadata.setName(name);
    metadata.setNamespace(namespace);
    metadata.setResourceVersion(resourceVersion);
    release.setMetadata(metadata);
    ReleaseSpec spec = new ReleaseSpec();
    release.setSpec(spec);
    return release;
  }
}
