// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.operator;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.common.operator.utils.OperatorWorkQueue;
import com.yugabyte.yw.controllers.handlers.CloudProviderHandler;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.OperatorResource;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.provider.KubernetesInfo;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.api.model.Secret;
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
import io.yugabyte.operator.v1alpha1.ybproviderspec.Regions;
import io.yugabyte.operator.v1alpha1.ybproviderspec.cloudinfo.KubeConfigSecret;
import io.yugabyte.operator.v1alpha1.ybproviderspec.regions.Zones;
import java.util.Collections;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
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

  @Test
  public void testHandleResourceDeletionBlockedByInProgressTask() throws Exception {
    YBProvider providerCr = createYBProviderCr(PROVIDER_NAME);
    Zones zone = new Zones();
    zone.setCode("us-west1-a");
    Regions region = new Regions();
    region.setCode("us-west1");
    region.setZones(Collections.singletonList(zone));
    providerCr.getSpec().setRegions(Collections.singletonList(region));

    UUID taskUUID = UUID.randomUUID();
    TaskInfo taskInfo = new TaskInfo(TaskType.CloudProviderEdit, null);
    taskInfo.setUuid(taskUUID);
    taskInfo.setTaskState(TaskInfo.State.Running);
    taskInfo.setTaskParams(new ObjectMapper().createObjectNode());
    taskInfo.setOwner("test");
    taskInfo.save();

    Provider reqProvider = new Provider();
    reqProvider.setName(PROVIDER_NAME);
    when(operatorUtils.getProviderReqFromProviderDetails(any())).thenReturn(reqProvider);
    when(cloudProviderHandler.editProvider(
            any(Customer.class),
            any(Provider.class),
            any(Provider.class),
            anyBoolean(),
            anyBoolean(),
            any()))
        .thenReturn(taskUUID);

    ybProviderReconciler.editProviderFromCRD(testCustomer, providerCr, testProvider);

    String mapKey = OperatorWorkQueue.getWorkQueueKey(providerCr.getMetadata());
    assertNotNull(
        "providerTaskMap should contain the edit task UUID",
        ybProviderReconciler.getProviderTaskMapValue(mapKey));

    ybProviderReconciler.handleResourceDeletion(
        providerCr, testCustomer, OperatorWorkQueue.ResourceAction.DELETE);

    verify(cloudProviderHandler, never()).delete(any(Customer.class), any(UUID.class));

    taskInfo.setTaskState(TaskInfo.State.Success);
    taskInfo.save();

    ybProviderReconciler.handleResourceDeletion(
        providerCr, testCustomer, OperatorWorkQueue.ResourceAction.DELETE);

    verify(cloudProviderHandler).delete(eq(testCustomer), eq(testProvider.getUuid()));
  }

  // A CR the universe import created points at a provider that already exists and needs no edit,
  // so no provider task ever runs to report its status. PLAT-22036.
  @Test
  public void testSyncProviderStatusReportsExistingProviderState() {
    YBProvider providerCr = createYBProviderCr(PROVIDER_NAME);
    assertNull("CR from import starts with no status", providerCr.getStatus());
    testProvider.setUsabilityState(Provider.UsabilityState.READY);
    testProvider.save();

    ybProviderReconciler.syncProviderStatus(providerCr, testProvider);

    assertNotNull("status should be populated", providerCr.getStatus());
    assertEquals(Provider.UsabilityState.READY.toString(), providerCr.getStatus().getState());
    assertEquals(testProvider.getUuid().toString(), providerCr.getStatus().getResourceUUID());
    verify(providerResource, times(1)).replaceStatus();

    // The no-op action reconciles on every resync, so an unchanged state must not write again.
    ybProviderReconciler.syncProviderStatus(providerCr, testProvider);
    verify(providerResource, times(1)).replaceStatus();

    testProvider.setUsabilityState(Provider.UsabilityState.ERROR);
    testProvider.save();
    ybProviderReconciler.syncProviderStatus(providerCr, testProvider);
    assertEquals(Provider.UsabilityState.ERROR.toString(), providerCr.getStatus().getState());
    verify(providerResource, times(2)).replaceStatus();
  }

  private static final String OLD_KUBECONFIG_SECRET = "test-provider-kubeconfig";
  private static final String NEW_KUBECONFIG_SECRET = "test-provider-kubeconfig-v2";
  private static final String OLD_KUBECONFIG =
      "apiVersion: v1\nkind: Config\nusers:\n- name: old\n";
  private static final String NEW_KUBECONFIG =
      "apiVersion: v1\nkind: Config\nusers:\n- name: new\n";

  private YBProvider createYBProviderCrWithKubeConfigSecret(String secretName) {
    YBProvider provider = createYBProviderCr(PROVIDER_NAME);
    Zones zone = new Zones();
    zone.setCode("us-west1-a");
    Regions region = new Regions();
    region.setCode("us-west1");
    region.setZones(Collections.singletonList(zone));
    provider.getSpec().setRegions(Collections.singletonList(region));
    pointAtKubeConfigSecret(provider, secretName);
    return provider;
  }

  private void pointAtKubeConfigSecret(YBProvider provider, String secretName) {
    KubeConfigSecret secretRef = new KubeConfigSecret();
    secretRef.setName(secretName);
    secretRef.setNamespace(NAMESPACE);
    provider.getSpec().getCloudInfo().setKubeConfigSecret(secretRef);
  }

  private void stubKubeConfigSecret(String secretName, String content) {
    Secret secret = new Secret();
    secret.setMetadata(new ObjectMeta());
    secret.getMetadata().setName(secretName);
    secret.getMetadata().setNamespace(NAMESPACE);
    lenient().when(operatorUtils.getSecret(eq(secretName), eq(NAMESPACE))).thenReturn(secret);
    lenient()
        .when(operatorUtils.parseSecretForKey(eq(secret), eq("kubeconfig")))
        .thenReturn(content);
  }

  private String extractedKubeConfigContent(YBProvider providerCr) {
    ObjectNode cloudInfo = new ObjectMapper().valueToTree(providerCr.getSpec().getCloudInfo());
    ybProviderReconciler.maybeExtractKubeConfig(providerCr, cloudInfo);
    JsonNode content = cloudInfo.get("kubeConfigContent");
    return content == null ? null : content.asText();
  }

  @Test
  public void testKubeConfigUpdateReadsTheNewSecret() {
    YBProvider providerCr = createYBProviderCrWithKubeConfigSecret(OLD_KUBECONFIG_SECRET);
    stubKubeConfigSecret(OLD_KUBECONFIG_SECRET, OLD_KUBECONFIG);
    stubKubeConfigSecret(NEW_KUBECONFIG_SECRET, NEW_KUBECONFIG);

    assertEquals(OLD_KUBECONFIG, extractedKubeConfigContent(providerCr));

    pointAtKubeConfigSecret(providerCr, NEW_KUBECONFIG_SECRET);
    assertEquals(
        "the CR now references a different Secret, so its contents must be read",
        NEW_KUBECONFIG,
        extractedKubeConfigContent(providerCr));
  }

  // The kubeconfig has to reach the Provider YBA is asked to persist, not merely register as drift.
  @Test
  public void testEditProviderFromCRDSendsNewKubeConfigToYba() {
    YBProvider providerCr = createYBProviderCrWithKubeConfigSecret(NEW_KUBECONFIG_SECRET);
    stubKubeConfigSecret(NEW_KUBECONFIG_SECRET, NEW_KUBECONFIG);

    Provider reqProvider = new Provider();
    when(operatorUtils.getProviderReqFromProviderDetails(any())).thenReturn(reqProvider);
    when(cloudProviderHandler.editProvider(
            any(Customer.class),
            any(Provider.class),
            any(Provider.class),
            anyBoolean(),
            anyBoolean(),
            any()))
        .thenReturn(UUID.randomUUID());

    ybProviderReconciler.editProviderFromCRD(testCustomer, providerCr, testProvider);

    ArgumentCaptor<JsonNode> payload = ArgumentCaptor.forClass(JsonNode.class);
    verify(operatorUtils).getProviderReqFromProviderDetails(payload.capture());
    assertEquals(
        "the payload sent to YBA carries the new kubeconfig",
        NEW_KUBECONFIG,
        payload
            .getValue()
            .get("details")
            .get("cloudInfo")
            .get("kubernetes")
            .get("kubeConfigContent")
            .asText());
  }

  private JsonNode payloadWithKubernetesProvider(String value) {
    ObjectNode payload = new ObjectMapper().createObjectNode();
    payload
        .putObject("details")
        .putObject("cloudInfo")
        .putObject("kubernetes")
        .put("kubernetesProvider", value);
    return payload;
  }

  private Provider providerStoringKubernetesProvider(String value) {
    KubernetesInfo k8sInfo = new KubernetesInfo();
    k8sInfo.setKubernetesProvider(value);
    ProviderDetails.CloudInfo cloudInfo = new ProviderDetails.CloudInfo();
    cloudInfo.setKubernetes(k8sInfo);
    testProvider.getDetails().setCloudInfo(cloudInfo);
    return testProvider;
  }

  private String kubernetesProviderIn(JsonNode payload) {
    return payload
        .get("details")
        .get("cloudInfo")
        .get("kubernetes")
        .get("kubernetesProvider")
        .asText();
  }

  // The CR enum always serializes lower case, but a provider is stored with whatever case it was
  // created with - "GKE" via the suggested-config API, "gke" via the UI. kubernetesProvider is not
  // editable on an in-use provider, so a case-only difference makes YBA reject the whole edit.
  @Test
  public void testEditAlignsKubernetesProviderCaseToWhatIsStored() {
    JsonNode upperCaseStored = payloadWithKubernetesProvider("gke");
    ybProviderReconciler.alignKubernetesProviderCase(
        upperCaseStored, providerStoringKubernetesProvider("GKE"));
    assertEquals("GKE", kubernetesProviderIn(upperCaseStored));

    JsonNode lowerCaseStored = payloadWithKubernetesProvider("gke");
    ybProviderReconciler.alignKubernetesProviderCase(
        lowerCaseStored, providerStoringKubernetesProvider("gke"));
    assertEquals("gke", kubernetesProviderIn(lowerCaseStored));
  }

  // Only the case is reconciled - a genuine change of provider type has to stay visible so YBA can
  // reject it.
  @Test
  public void testEditDoesNotMaskAGenuineKubernetesProviderChange() {
    JsonNode payload = payloadWithKubernetesProvider("eks");
    ybProviderReconciler.alignKubernetesProviderCase(
        payload, providerStoringKubernetesProvider("GKE"));
    assertEquals("eks", kubernetesProviderIn(payload));
  }

  // The provider stays usable on its old settings so YBA still calls it READY, but the CR asked
  // for settings that were never applied. Reporting that as READY hides a reconciler that retries
  // and never converges, so the CR only returns to READY once an edit succeeds.
  @Test
  public void testFailedEditLeavesCrInErrorEvenThoughProviderIsReady() {
    YBProvider providerCr = createYBProviderCrWithKubeConfigSecret(NEW_KUBECONFIG_SECRET);
    stubKubeConfigSecret(NEW_KUBECONFIG_SECRET, NEW_KUBECONFIG);
    testProvider.setUsabilityState(Provider.UsabilityState.READY);
    testProvider.save();
    // A CR that already reconciled cleanly once carries a READY status.
    ybProviderReconciler.syncProviderStatus(providerCr, testProvider);
    assertEquals(Provider.UsabilityState.READY.toString(), providerCr.getStatus().getState());

    when(operatorUtils.getProviderReqFromProviderDetails(any())).thenReturn(new Provider());
    when(cloudProviderHandler.editProvider(
            any(Customer.class),
            any(Provider.class),
            any(Provider.class),
            anyBoolean(),
            anyBoolean(),
            any()))
        .thenThrow(
            new PlatformServiceException(
                BAD_REQUEST, "Modifying zone us-west1-a details is not allowed"));

    assertThrows(
        PlatformServiceException.class,
        () -> ybProviderReconciler.editProviderFromCRD(testCustomer, providerCr, testProvider));

    assertEquals(
        "a failed edit must not leave the CR reading READY",
        Provider.UsabilityState.ERROR.toString(),
        providerCr.getStatus().getState());
    assertTrue(
        "the reason stays on the status",
        providerCr.getStatus().getMessage().contains("Provider edit failed"));
    assertEquals(
        "YBA still considers the provider itself usable",
        Provider.UsabilityState.READY,
        testProvider.getUsabilityState());
  }
}
