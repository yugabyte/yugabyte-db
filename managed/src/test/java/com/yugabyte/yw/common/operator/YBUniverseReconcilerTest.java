package com.yugabyte.yw.common.operator;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.ArgumentMatchers.isNull;

import com.yugabyte.yw.common.CustomerTaskManager;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.ValidatingFormFactory;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.operator.OperatorStatusUpdater.UniverseState;
import com.yugabyte.yw.common.operator.utils.KubernetesClientFactory;
import com.yugabyte.yw.common.operator.utils.KubernetesEnvironmentVariables;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.common.operator.utils.OperatorWorkQueue;
import com.yugabyte.yw.common.operator.utils.UniverseImporter;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.controllers.handlers.CloudProviderHandler;
import com.yugabyte.yw.controllers.handlers.UniverseActionsHandler;
import com.yugabyte.yw.controllers.handlers.UniverseCRUDHandler;
import com.yugabyte.yw.controllers.handlers.UpgradeUniverseHandler;
import com.yugabyte.yw.forms.KubernetesOverridesUpgradeParams;
import com.yugabyte.yw.forms.KubernetesProviderFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseResp;
import com.yugabyte.yw.forms.YbcThrottleParametersResponse;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.OperatorResource;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails.CloudInfo;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.TaskInfo.State;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.provider.KubernetesInfo;
import io.fabric8.kubernetes.api.model.IntOrString;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.NonNamespaceOperation;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.fabric8.kubernetes.client.informers.cache.Indexer;
import io.fabric8.kubernetes.client.utils.Serialization;
import io.yugabyte.operator.v1alpha1.YBUniverse;
import io.yugabyte.operator.v1alpha1.ybuniversespec.KubernetesOverrides;
import io.yugabyte.operator.v1alpha1.ybuniversespec.kubernetesoverrides.Resource;
import io.yugabyte.operator.v1alpha1.ybuniversespec.kubernetesoverrides.resource.Master;
import io.yugabyte.operator.v1alpha1.ybuniversespec.kubernetesoverrides.resource.master.Limits;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.MockedStatic;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
@Slf4j
public class YBUniverseReconcilerTest extends FakeDBApplication {
  @Mock KubernetesClient client;

  @Mock
  MixedOperation<
          YBUniverse,
          KubernetesResourceList<YBUniverse>,
          io.fabric8.kubernetes.client.dsl.Resource<YBUniverse>>
      ybUniverseClient;

  @Mock
  NonNamespaceOperation<
          YBUniverse,
          KubernetesResourceList<YBUniverse>,
          io.fabric8.kubernetes.client.dsl.Resource<YBUniverse>>
      inNamespaceYBUClient;

  @Mock io.fabric8.kubernetes.client.dsl.Resource<YBUniverse> ybUniverseResource;
  @Mock RuntimeConfGetter confGetter;
  @Mock RuntimeConfGetter confGetterForOperatorUtils;
  @Mock YBInformerFactory informerFactory;
  @Mock SharedIndexInformer<YBUniverse> ybUniverseInformer;
  @Mock Indexer<YBUniverse> indexer;
  @Mock UniverseCRUDHandler universeCRUDHandler;
  @Mock CloudProviderHandler cloudProviderHandler;
  @Mock CustomerTaskManager customerTaskManager;
  @Mock UpgradeUniverseHandler upgradeUniverseHandler;
  @Mock KubernetesOperatorStatusUpdater kubernetesStatusUpdator;
  @Mock ReleaseManager releaseManager;
  @Mock UniverseActionsHandler universeActionsHandler;
  @Mock YbcManager ybcManager;
  @Mock ValidatingFormFactory validatingFormFactory;
  @Mock YBClientService ybClientService;
  @Mock KubernetesClientFactory kubernetesClientFactory;
  @Mock UniverseImporter universeImporter;
  @Mock KubernetesManagerFactory mockKubernetesManagerFactory;
  MockedStatic<KubernetesEnvironmentVariables> envVars;

  YBUniverseReconciler ybUniverseReconciler;
  OperatorUtils operatorUtils;
  Customer defaultCustomer;
  Universe defaultUniverse;
  Users defaultUsers;
  Provider defaultProvider;

  private final String universeName = "test-universe";
  private final String namespace = "test-namespace";

  @Before
  public void beforeTest() {
    Mockito.when(informerFactory.getSharedIndexInformer(YBUniverse.class, client))
        .thenReturn(ybUniverseInformer);
    Mockito.when(ybUniverseInformer.getIndexer()).thenReturn(indexer);

    // Setup patch mock
    Mockito.when(client.resources(YBUniverse.class)).thenReturn(ybUniverseClient);
    Mockito.when(ybUniverseClient.inNamespace(anyString())).thenReturn(inNamespaceYBUClient);
    Mockito.when(inNamespaceYBUClient.withName(anyString())).thenReturn(ybUniverseResource);
    YbcThrottleParametersResponse throttleResponse = new YbcThrottleParametersResponse();
    throttleResponse.setThrottleParamsMap(new HashMap<>());
    Mockito.when(ybcManager.getThrottleParams(any())).thenReturn(throttleResponse);

    envVars = Mockito.mockStatic(KubernetesEnvironmentVariables.class);
    envVars.when(KubernetesEnvironmentVariables::isYbaRunningInKubernetes).thenReturn(true);
    operatorUtils =
        Mockito.spy(
            new OperatorUtils(
                confGetterForOperatorUtils,
                releaseManager,
                ybcManager,
                validatingFormFactory,
                ybClientService,
                kubernetesClientFactory,
                universeImporter,
                mockKubernetesManagerFactory));
    // Mockito.when(confGetter.getGlobalConf(any())).thenReturn(true);
    Mockito.when(
            confGetterForOperatorUtils.getGlobalConf(GlobalConfKeys.KubernetesOperatorCustomerUUID))
        .thenReturn("");
    ybUniverseReconciler =
        new YBUniverseReconciler(
            client,
            informerFactory,
            namespace,
            new OperatorWorkQueue(1, 5, 10, "YBUniverse"),
            universeCRUDHandler,
            upgradeUniverseHandler,
            cloudProviderHandler,
            null,
            kubernetesStatusUpdator,
            confGetter,
            customerTaskManager,
            operatorUtils,
            universeActionsHandler,
            ybcManager);
    // reconcilerFactory.getYBUniverseReconciler(client);

    // Setup Defaults
    defaultCustomer = ModelFactory.testCustomer();
    defaultUniverse = ModelFactory.createUniverse(universeName, defaultCustomer.getId());
    defaultUsers = ModelFactory.testUser(defaultCustomer);
    defaultProvider = ModelFactory.kubernetesProvider(defaultCustomer);
  }

  @After
  public void afterTest() {
    envVars.close();
  }

  // Reconcile Tests
  @Test
  public void testReconcileDeleteAlreadyDeleted() {
    YBUniverse universe = ModelFactory.createYbUniverse(defaultProvider);
    String univName =
        universeName
            + "-"
            + Integer.toString(
                Math.abs(
                    universeName
                        .concat(universe.getMetadata().getNamespace())
                        .concat(universe.getMetadata().getUid())
                        .hashCode()));
    universe.setStatus(null);
    Mockito.lenient()
        .when(universeCRUDHandler.findByName(defaultCustomer, univName))
        .thenReturn(null);
    ybUniverseReconciler.reconcile(universe, OperatorWorkQueue.ResourceAction.DELETE);
    // Not sure what to check here, most items are null and we just return.
  }

  // TODO: Need more delete tests, but that code should be refactored a bit.

  @Test
  public void testReconcileCreate() {
    String universeName = "test-create-reconcile-universe";
    YBUniverse universe = ModelFactory.createYbUniverse(universeName, defaultProvider);
    ybUniverseReconciler.reconcile(universe, OperatorWorkQueue.ResourceAction.CREATE);

    Mockito.verify(kubernetesStatusUpdator, Mockito.times(1))
        .createYBUniverseEventStatus(
            isNull(),
            any(KubernetesResourceDetails.class),
            eq(TaskType.CreateKubernetesUniverse.name()));
    Mockito.verify(universeCRUDHandler, Mockito.times(1))
        .createUniverse(Mockito.eq(defaultCustomer), any(UniverseDefinitionTaskParams.class));
    Mockito.verify(ybUniverseResource, Mockito.atLeast(1)).patch(any(YBUniverse.class));

    // Verify OperatorResource entries were persisted in the database
    List<OperatorResource> allResources = OperatorResource.getAll();
    assertEquals(1, allResources.size());
    assertTrue(
        "OperatorResource name should contain the universe name",
        allResources.get(0).getName().contains(universeName));
    YBUniverse rUniverse = Serialization.unmarshal(allResources.get(0).getData(), YBUniverse.class);
    assertEquals(universeName, rUniverse.getMetadata().getName());
    assertEquals(namespace, rUniverse.getMetadata().getNamespace());
    assertEquals("2.21.0.0-b1", rUniverse.getSpec().getYbSoftwareVersion());
    assertEquals(Long.valueOf(1), rUniverse.getSpec().getNumNodes());
    assertEquals(Long.valueOf(1), rUniverse.getSpec().getReplicationFactor());
    assertTrue(rUniverse.getSpec().getEnableYSQL());
  }

  @Test
  public void testReconcileCreateAddsResourceToTrackedResources() {
    String universeName = "test-tracked-resources-universe";
    YBUniverse universe = ModelFactory.createYbUniverse(universeName, defaultProvider);
    assertTrue(
        "Tracked resources should be empty before reconcile",
        ybUniverseReconciler.getTrackedResources().isEmpty());

    ybUniverseReconciler.reconcile(universe, OperatorWorkQueue.ResourceAction.CREATE);

    assertEquals(
        "Tracked resources should contain the universe after CREATE",
        1,
        ybUniverseReconciler.getTrackedResources().size());
    KubernetesResourceDetails details =
        ybUniverseReconciler.getTrackedResources().iterator().next();
    assertEquals(universeName, details.name);
    assertEquals(universe.getMetadata().getNamespace(), details.namespace);

    // Verify OperatorResource entries were persisted in the database
    List<OperatorResource> allResources = OperatorResource.getAll();
    assertEquals(1, allResources.size());
    assertTrue(
        "OperatorResource name should contain the universe name",
        allResources.get(0).getName().contains(universeName));
    YBUniverse rUniverse = Serialization.unmarshal(allResources.get(0).getData(), YBUniverse.class);
    assertEquals(universeName, rUniverse.getMetadata().getName());
    assertEquals(namespace, rUniverse.getMetadata().getNamespace());
    assertEquals("2.21.0.0-b1", rUniverse.getSpec().getYbSoftwareVersion());
    assertEquals(Long.valueOf(1), rUniverse.getSpec().getNumNodes());
    assertEquals(Long.valueOf(1), rUniverse.getSpec().getReplicationFactor());
    assertTrue(rUniverse.getSpec().getEnableYSQL());
  }

  @Test
  public void testReconcileCreateAlreadyExists() throws Exception {
    // We don't find any customer task entry for create universe.
    String universeName = "test-universe-already-exists";
    YBUniverse ybUniverse = ModelFactory.createYbUniverse(universeName, defaultProvider);
    UniverseDefinitionTaskParams taskParams =
        ybUniverseReconciler.createTaskParams(ybUniverse, defaultCustomer.getUuid());
    Universe universe = Universe.create(taskParams, defaultCustomer.getId());
    ybUniverseReconciler.reconcile(ybUniverse, OperatorWorkQueue.ResourceAction.CREATE);
    Mockito.verify(universeCRUDHandler, Mockito.never())
        .createUniverse(Mockito.eq(defaultCustomer), any(UniverseDefinitionTaskParams.class));
  }

  @Test
  public void testReconcileCreateAutoProvider() {
    String universeName = "test-universe-create-provider";
    YBUniverse universe = ModelFactory.createYbUniverse(universeName, defaultProvider);
    String autoProviderNameSuffix =
        universeName
            + "-"
            + Integer.toString(
                Math.abs(
                    universeName
                        .concat(universe.getMetadata().getNamespace())
                        .concat(universe.getMetadata().getUid())
                        .hashCode()));
    KubernetesProviderFormData providerData = new KubernetesProviderFormData();
    providerData.name = "prov-" + autoProviderNameSuffix;
    providerData.config = new HashMap<>();
    providerData.config.put("KUBECONFIG_PROVIDER", "GKE");

    Mockito.when(cloudProviderHandler.suggestedKubernetesConfigs()).thenReturn(providerData);
    // Create a provider with the name following `YBUniverseReconciler.getProviderName` format
    Mockito.doAnswer(
            invocation -> {
              Provider mockProvider =
                  ModelFactory.kubernetesProvider(defaultCustomer, providerData.name);
              CloudInfo cloudInfo = new CloudInfo();
              cloudInfo.kubernetes = new KubernetesInfo();
              mockProvider.getDetails().setCloudInfo(cloudInfo);
              return null;
            })
        .when(operatorUtils)
        .createProviderCrFromProviderEbean(providerData, namespace, true);
    universe.getSpec().setProviderName("");
    ybUniverseReconciler.reconcile(universe, OperatorWorkQueue.ResourceAction.CREATE);
    ybUniverseReconciler.reconcile(universe, OperatorWorkQueue.ResourceAction.CREATE);
    Mockito.verify(cloudProviderHandler, Mockito.times(1)).suggestedKubernetesConfigs();
    Mockito.verify(universeCRUDHandler, Mockito.times(1))
        .createUniverse(Mockito.eq(defaultCustomer), any(UniverseDefinitionTaskParams.class));
    Mockito.verify(ybUniverseResource, Mockito.atLeast(1)).patch(any(YBUniverse.class));
  }

  @Test
  public void testCreateTaskRetryOnPlacementModificationTaskSet() throws Exception {
    String universeName = "test-retry-universe";
    YBUniverse ybUniverseOriginal = ModelFactory.createYbUniverse(universeName, defaultProvider);
    UniverseDefinitionTaskParams taskParams =
        ybUniverseReconciler.createTaskParams(ybUniverseOriginal, defaultCustomer.getUuid());
    Universe universe = Universe.create(taskParams, defaultCustomer.getId());
    universe = ModelFactory.addNodesToUniverse(universe.getUniverseUUID(), 1);
    taskParams = universe.getUniverseDetails();
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    TaskInfo taskInfo = new TaskInfo(TaskType.CreateKubernetesUniverse, null);
    taskInfo.setTaskParams(Json.toJson(taskParams));
    taskInfo.setOwner("localhost");
    taskInfo.save();
    taskInfo.refresh();

    UniverseDefinitionTaskParams uTaskParams = universe.getUniverseDetails();
    uTaskParams.placementModificationTaskUuid = taskInfo.getUuid();
    universe.setUniverseDetails(uTaskParams);
    universe.save();
    ybUniverseReconciler.reconcile(ybUniverseOriginal, OperatorWorkQueue.ResourceAction.CREATE);

    Mockito.verify(customerTaskManager, Mockito.times(1))
        .retryCustomerTask(Mockito.eq(defaultCustomer.getUuid()), Mockito.eq(taskInfo.getUuid()));
  }

  @Test
  public void testEditTaskRetryOnPlacementModificationTaskSet() throws Exception {
    String universeName = "test-retry-universe";
    YBUniverse ybUniverseOriginal = ModelFactory.createYbUniverse(universeName, defaultProvider);
    UniverseDefinitionTaskParams taskParams =
        ybUniverseReconciler.createTaskParams(ybUniverseOriginal, defaultCustomer.getUuid());
    Universe universe = Universe.create(taskParams, defaultCustomer.getId());
    universe = ModelFactory.addNodesToUniverse(universe.getUniverseUUID(), 1);
    TaskInfo taskInfo = new TaskInfo(TaskType.SoftwareKubernetesUpgrade, null);
    taskParams = universe.getUniverseDetails();
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    taskInfo.setTaskParams(Json.toJson(taskParams));
    taskInfo.setOwner("localhost");
    taskInfo.save();
    taskInfo.refresh();

    UniverseDefinitionTaskParams uTaskParams = universe.getUniverseDetails();
    uTaskParams.placementModificationTaskUuid = taskInfo.getUuid();
    universe.setUniverseDetails(uTaskParams);
    universe.save();
    ybUniverseReconciler.reconcile(ybUniverseOriginal, OperatorWorkQueue.ResourceAction.UPDATE);

    Mockito.verify(customerTaskManager, Mockito.times(1))
        .retryCustomerTask(Mockito.eq(defaultCustomer.getUuid()), Mockito.eq(taskInfo.getUuid()));
  }

  @Test
  public void testRequeueOnUniverseLocked() throws Exception {
    String universeName = "test-locked-universe";
    YBUniverse ybUniverseOriginal = ModelFactory.createYbUniverse(universeName, defaultProvider);
    UniverseDefinitionTaskParams taskParams =
        ybUniverseReconciler.createTaskParams(ybUniverseOriginal, defaultCustomer.getUuid());
    Universe universe = Universe.create(taskParams, defaultCustomer.getId());
    ModelFactory.addNodesToUniverse(universe.getUniverseUUID(), 1);

    UniverseDefinitionTaskParams uTaskParams = universe.getUniverseDetails();
    uTaskParams.updateInProgress = true;
    universe.setUniverseDetails(uTaskParams);
    universe.save();

    ybUniverseReconciler.reconcile(ybUniverseOriginal, OperatorWorkQueue.ResourceAction.NO_OP);
    Thread task =
        new Thread(
            () -> {
              Pair<String, OperatorWorkQueue.ResourceAction> queueItem =
                  ybUniverseReconciler.getOperatorWorkQueue().pop();
              assertEquals(queueItem.getSecond(), OperatorWorkQueue.ResourceAction.NO_OP);
              assertTrue(ybUniverseReconciler.getOperatorWorkQueue().isEmpty());
            });
    task.start();
    task.join();
  }

  @Test
  public void testCreateOnPreviousCreateTaskFailed() throws Exception {
    String universeName = "test-previous-task-failed-universe";
    YBUniverse ybUniverseOriginal = ModelFactory.createYbUniverse(universeName, defaultProvider);
    UniverseDefinitionTaskParams taskParams =
        ybUniverseReconciler.createTaskParams(ybUniverseOriginal, defaultCustomer.getUuid());
    Universe oldUniverse = Universe.create(taskParams, defaultCustomer.getId());
    ModelFactory.addNodesToUniverse(oldUniverse.getUniverseUUID(), 1);
    TaskInfo taskInfo = new TaskInfo(TaskType.CreateKubernetesUniverse, null);
    taskInfo.setTaskParams(Json.toJson(taskParams));
    taskInfo.setTaskState(State.Failure);
    taskInfo.setOwner("localhost");
    taskInfo.save();
    taskInfo.refresh();
    CustomerTask.create(
        defaultCustomer,
        oldUniverse.getUniverseUUID(),
        taskInfo.getUuid(),
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.Create,
        oldUniverse.getName());

    // Need a response object otherwise reconciler fails with null
    UniverseResp uResp = new UniverseResp(defaultUniverse, UUID.randomUUID());
    Mockito.when(
            universeCRUDHandler.createUniverse(
                Mockito.eq(defaultCustomer), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(uResp);

    // Change some param and verify latest spec is used after deleting currently created Universe
    // object
    ybUniverseOriginal.getSpec().setYbSoftwareVersion("2.21.0.0-b2");
    ybUniverseReconciler.reconcile(ybUniverseOriginal, OperatorWorkQueue.ResourceAction.CREATE);

    ArgumentCaptor<UniverseDefinitionTaskParams> uDTCaptor =
        ArgumentCaptor.forClass(UniverseDefinitionTaskParams.class);
    Mockito.verify(universeCRUDHandler, Mockito.times(1))
        .createUniverse(Mockito.eq(defaultCustomer), uDTCaptor.capture());

    String oldSoftwareVersion =
        oldUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    String newSoftwareVersion =
        uDTCaptor.getValue().getPrimaryCluster().userIntent.ybSoftwareVersion;
    // Verify new spec used
    assertFalse(newSoftwareVersion.equals(oldSoftwareVersion));
    // Verify old universe objct removed
    assertFalse(Universe.maybeGet(oldUniverse.getUniverseUUID()).isPresent());
  }

  @Test
  public void testCreateAutoProviderFailStatusUpdate() throws Exception {
    String universeName = "test-provider-create-fail";
    YBUniverse ybUniverse = ModelFactory.createYbUniverse(universeName, defaultProvider);
    ybUniverse.getSpec().setProviderName("");
    KubernetesProviderFormData providerData = new KubernetesProviderFormData();
    Mockito.when(cloudProviderHandler.suggestedKubernetesConfigs()).thenReturn(providerData);
    Mockito.doAnswer(
            invocation -> {
              Provider mockProvider =
                  ModelFactory.kubernetesProvider(defaultCustomer, providerData.name);
              mockProvider.setUsabilityState(Provider.UsabilityState.ERROR);
              return null;
            })
        .when(operatorUtils)
        .createProviderCrFromProviderEbean(providerData, namespace, true);
    // First reconcile will create auto provider CR
    ybUniverseReconciler.reconcile(ybUniverse, OperatorWorkQueue.ResourceAction.CREATE);
    try {
      // Second reconcile will fail because provider is not ready
      ybUniverseReconciler.reconcile(ybUniverse, OperatorWorkQueue.ResourceAction.CREATE);
    } catch (Exception e) {
    }
    Mockito.verify(kubernetesStatusUpdator, Mockito.times(1))
        .updateUniverseState(
            any(KubernetesResourceDetails.class), eq(UniverseState.ERROR_CREATING));
  }

  @Test
  public void testMultipleSpecUpdatePickOverrideBeforeEdit() throws Exception {
    String universeName = "test-multiple-spec-updates";
    YBUniverse ybUniverse = ModelFactory.createYbUniverse(universeName, defaultProvider);
    UniverseDefinitionTaskParams taskParams =
        ybUniverseReconciler.createTaskParams(ybUniverse, defaultCustomer.getUuid());
    Universe oldUniverse = Universe.create(taskParams, defaultCustomer.getId());

    Mockito.when(
            confGetter.getConfForScope(
                any(Universe.class), eq(UniverseConfKeys.rollingOpsWaitAfterEachPodMs)))
        .thenReturn(10000);
    // Update spec
    ybUniverse.getSpec().getDeviceInfo().setVolumeSize(20L);
    KubernetesOverrides ko = new KubernetesOverrides();
    Map<String, String> nodeSelectorMap = new HashMap<>();
    nodeSelectorMap.put("foo", "bar");
    ko.setNodeSelector(nodeSelectorMap);
    ybUniverse.getSpec().setKubernetesOverrides(ko);

    // Call edit
    ybUniverseReconciler.editUniverse(defaultCustomer, oldUniverse, ybUniverse);
    // Verify update is called
    ArgumentCaptor<KubernetesOverridesUpgradeParams> uDTCaptor =
        ArgumentCaptor.forClass(KubernetesOverridesUpgradeParams.class);
    Mockito.verify(upgradeUniverseHandler, Mockito.times(1))
        .upgradeKubernetesOverrides(uDTCaptor.capture(), any(Customer.class), any(Universe.class));
    assertTrue(uDTCaptor.getValue().universeOverrides.contains("bar"));
    // Verify upgrade handler is not called
    Mockito.verifyNoInteractions(universeCRUDHandler);
  }

  @Test
  public void testReconcileDeleteRemovesOperatorResource() {
    String universeName = "test-delete-tracked-universe";
    YBUniverse universe = ModelFactory.createYbUniverse(universeName, defaultProvider);

    // First CREATE to track the resource and persist OperatorResource
    ybUniverseReconciler.reconcile(universe, OperatorWorkQueue.ResourceAction.CREATE);
    assertEquals(1, OperatorResource.getAll().size());

    // Stub findByName to return empty list (universe "already deleted" in YBA).
    // The YBUniverse has no finalizers, so the delete thread spawn is skipped.
    Mockito.when(universeCRUDHandler.findByName(eq(defaultCustomer), anyString()))
        .thenReturn(Collections.emptyList());

    ybUniverseReconciler.reconcile(universe, OperatorWorkQueue.ResourceAction.DELETE);

    assertTrue(
        "Tracked resources should be empty after delete",
        ybUniverseReconciler.getTrackedResources().isEmpty());
    assertTrue(
        "OperatorResource entries should be removed after delete",
        OperatorResource.getAll().isEmpty());
  }

  @Test
  public void testParseKubernetesOverridesNoAdditionalProperty() {
    KubernetesOverrides overrides = createKubernetesOverrides();
    String expectedString =
        "---\n"
            + "nodeSelector:\n"
            + "  label: \"selector\"\n"
            + "resource:\n"
            + "  master:\n"
            + "    limits:\n"
            + "      cpu: 4\n";

    String overridesString = operatorUtils.getKubernetesOverridesString(overrides);
    assertEquals(expectedString, overridesString);
    assertTrue(overridesString.length() > 0);
  }

  @Test
  public void testParseKubernetesOverridesWithAdditionalProperty() {
    KubernetesOverrides overrides = createKubernetesOverrides();

    Map<String, Object> additionalPropertiesMap = new HashMap<>();
    additionalPropertiesMap.put("foo", "bar");
    overrides.setAdditionalProperties(additionalPropertiesMap);

    String overridesString = operatorUtils.getKubernetesOverridesString(overrides);
    assertTrue(overridesString.length() > 0);
    assertTrue(overridesString.contains("foo") && overridesString.contains("bar"));
  }

  private KubernetesOverrides createKubernetesOverrides() {
    KubernetesOverrides overrides = new KubernetesOverrides();
    Map<String, String> nodeSelectorMap = new HashMap<>();
    nodeSelectorMap.put("label", "selector");
    overrides.setNodeSelector(nodeSelectorMap);

    Resource resource = new Resource();
    Limits limit = new Limits();
    limit.setCpu(new IntOrString(Integer.valueOf(4)));
    Master masterResource = new Master();
    masterResource.setLimits(limit);
    resource.setMaster(masterResource);
    overrides.setResource(resource);

    return overrides;
  }
}
