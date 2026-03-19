// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.operator;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.common.operator.utils.OperatorWorkQueue;
import com.yugabyte.yw.controllers.handlers.CloudProviderHandler;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.OperatorResource;
import com.yugabyte.yw.models.Provider;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.NonNamespaceOperation;
import io.fabric8.kubernetes.client.dsl.Resource;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.fabric8.kubernetes.client.informers.cache.Indexer;
import io.fabric8.kubernetes.client.utils.Serialization;
import io.yugabyte.operator.v1alpha1.YBProvider;
import io.yugabyte.operator.v1alpha1.YBProviderSpec;
import io.yugabyte.operator.v1alpha1.ybproviderspec.CloudInfo;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class YBProviderReconcilerTest extends FakeDBApplication {

  @Mock KubernetesClient client;
  @Mock YBInformerFactory informerFactory;
  @Mock OperatorUtils operatorUtils;
  @Mock CloudProviderHandler cloudProviderHandler;
  @Mock SharedIndexInformer<YBProvider> providerInformer;
  @Mock Indexer<YBProvider> indexer;

  @Mock
  MixedOperation<YBProvider, KubernetesResourceList<YBProvider>, Resource<YBProvider>>
      resourceClient;

  @Mock
  NonNamespaceOperation<YBProvider, KubernetesResourceList<YBProvider>, Resource<YBProvider>>
      inNamespaceResource;

  @Mock Resource<YBProvider> providerResource;

  private YBProviderReconciler ybProviderReconciler;
  private Customer testCustomer;
  private Provider testProvider;
  private static final String NAMESPACE = "test-namespace";
  private static final String PROVIDER_NAME = "test-k8s-provider";

  @Before
  public void setup() throws Exception {
    when(informerFactory.getSharedIndexInformer(eq(YBProvider.class), any(KubernetesClient.class)))
        .thenReturn(providerInformer);
    when(providerInformer.getIndexer()).thenReturn(indexer);
    when(cloudProviderHandler.getDefaultKubernetesPullSecretYaml()).thenReturn("");
    when(cloudProviderHandler.getKubernetesPullSecretName()).thenReturn("pull-secret");
    testCustomer = ModelFactory.testCustomer();
    testProvider = ModelFactory.kubernetesProvider(testCustomer, PROVIDER_NAME);
    when(operatorUtils.getOperatorCustomer()).thenReturn(testCustomer);
    lenient().when(client.resources(YBProvider.class)).thenReturn(resourceClient);
    lenient().when(resourceClient.inNamespace(NAMESPACE)).thenReturn(inNamespaceResource);
    lenient()
        .when(inNamespaceResource.resource(any(YBProvider.class)))
        .thenReturn(providerResource);

    ybProviderReconciler =
        new YBProviderReconciler(
            client, informerFactory, NAMESPACE, operatorUtils, cloudProviderHandler);
  }

  private YBProvider createYBProviderCr(String name) {
    YBProvider provider = new YBProvider();
    ObjectMeta metadata = new ObjectMeta();
    metadata.setName(name);
    metadata.setNamespace(NAMESPACE);
    metadata.setUid(UUID.randomUUID().toString());
    provider.setMetadata(metadata);
    provider.setStatus(null);
    YBProviderSpec spec = new YBProviderSpec();
    CloudInfo cloudInfo = new CloudInfo();
    cloudInfo.setKubernetesProvider(CloudInfo.KubernetesProvider.GKE);
    cloudInfo.setKubernetesImageRegistry("quay.io/yugabyte/yugabyte");
    spec.setCloudInfo(cloudInfo);
    provider.setSpec(spec);
    return provider;
  }

  @Test
  public void testReconcileCreateAddsResourceToTrackedResources() {
    YBProvider providerCr = createYBProviderCr(PROVIDER_NAME);
    assertTrue(
        "Tracked resources should be empty before reconcile",
        ybProviderReconciler.getTrackedResources().isEmpty());

    ybProviderReconciler.reconcile(providerCr, OperatorWorkQueue.ResourceAction.CREATE);

    assertEquals(
        "Tracked resources should contain the provider after CREATE",
        1,
        ybProviderReconciler.getTrackedResources().size());
    KubernetesResourceDetails details =
        ybProviderReconciler.getTrackedResources().iterator().next();
    assertEquals(PROVIDER_NAME, details.name);
    assertEquals(NAMESPACE, details.namespace);

    // Verify OperatorResource entries were persisted in the database
    List<OperatorResource> allResources = OperatorResource.getAll();
    assertEquals(1, allResources.size());
    assertTrue(
        "OperatorResource name should contain the provider name",
        allResources.get(0).getName().contains(PROVIDER_NAME));
    YBProvider rProvider = Serialization.unmarshal(allResources.get(0).getData(), YBProvider.class);
    assertEquals(PROVIDER_NAME, rProvider.getMetadata().getName());
    assertEquals(NAMESPACE, rProvider.getMetadata().getNamespace());
    assertEquals(
        CloudInfo.KubernetesProvider.GKE,
        rProvider.getSpec().getCloudInfo().getKubernetesProvider());
    assertEquals(
        "quay.io/yugabyte/yugabyte",
        rProvider.getSpec().getCloudInfo().getKubernetesImageRegistry());
  }

  @Test
  public void testReconcileDeleteRemovesOperatorResource() {
    YBProvider providerCr = createYBProviderCr(PROVIDER_NAME);

    // First CREATE to track the resource
    ybProviderReconciler.reconcile(providerCr, OperatorWorkQueue.ResourceAction.CREATE);
    assertEquals(1, OperatorResource.getAll().size());

    // DELETE - provider exists in DB with 0 universes, so handleResourceDeletion
    // calls deleteProvider (mocked cloudProviderHandler.delete returns null) then untrackResource
    ybProviderReconciler.reconcile(providerCr, OperatorWorkQueue.ResourceAction.DELETE);

    assertTrue(
        "Tracked resources should be empty after delete",
        ybProviderReconciler.getTrackedResources().isEmpty());
    assertTrue(
        "OperatorResource entries should be removed after delete",
        OperatorResource.getAll().isEmpty());
  }

  @Test
  public void testReconcileNoOpUpdatesOperatorResourceData() {
    YBProvider providerCr = createYBProviderCr(PROVIDER_NAME);

    // First CREATE to track the resource
    ybProviderReconciler.reconcile(providerCr, OperatorWorkQueue.ResourceAction.CREATE);
    assertEquals(1, OperatorResource.getAll().size());

    // Verify initial stored data has GKE provider type
    YBProvider stored =
        Serialization.unmarshal(OperatorResource.getAll().get(0).getData(), YBProvider.class);
    assertEquals(
        CloudInfo.KubernetesProvider.GKE, stored.getSpec().getCloudInfo().getKubernetesProvider());

    // Update the CR spec - change provider type to EKS
    providerCr.getSpec().getCloudInfo().setKubernetesProvider(CloudInfo.KubernetesProvider.EKS);

    // NO_OP reconcile (simulates an update via the informer) should persist the updated data
    ybProviderReconciler.reconcile(providerCr, OperatorWorkQueue.ResourceAction.NO_OP);

    // Verify the stored data was updated to EKS
    List<OperatorResource> allResources = OperatorResource.getAll();
    assertEquals(1, allResources.size());
    YBProvider updatedStored =
        Serialization.unmarshal(allResources.get(0).getData(), YBProvider.class);
    assertEquals(
        CloudInfo.KubernetesProvider.EKS,
        updatedStored.getSpec().getCloudInfo().getKubernetesProvider());
  }
}
