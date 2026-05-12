package com.yugabyte.yw.common.operator;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.ArgumentMatchers.isNull;

import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
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
import com.yugabyte.yw.forms.KubernetesToggleImmutableYbcParams;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.AZOverrides;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.PerProcessDetails;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntentOverrides;
import com.yugabyte.yw.forms.UniverseResp;
import com.yugabyte.yw.forms.YbcThrottleParametersResponse;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.AvailabilityZoneDetails;
import com.yugabyte.yw.models.AvailabilityZoneDetails.AZCloudInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.OperatorResource;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails.CloudInfo;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.TaskInfo.State;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.provider.KubernetesInfo;
import com.yugabyte.yw.models.helpers.provider.region.KubernetesRegionInfo;
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
import java.util.Set;
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
  public void testEditUniverseTriggersToggleYbcWhenUseYbdbInbuiltYbcToggledInCr() throws Exception {
    String universeName = "test-toggle-ybc-universe";
    YBUniverse ybUniverse = ModelFactory.createYbUniverse(universeName, defaultProvider);
    UniverseDefinitionTaskParams taskParams =
        ybUniverseReconciler.createTaskParams(ybUniverse, defaultCustomer.getUuid());
    Universe oldUniverse = Universe.create(taskParams, defaultCustomer.getId());
    // Universe has useYbdbInbuiltYbc = false by default; ensure it stays false
    assertFalse(
        oldUniverse.getUniverseDetails().getPrimaryCluster().userIntent.isUseYbdbInbuiltYbc());

    Mockito.when(
            confGetter.getConfForScope(
                any(Universe.class), eq(UniverseConfKeys.rollingOpsWaitAfterEachPodMs)))
        .thenReturn(10000);

    // Toggle useYbdbInbuiltYbc to true in the CR
    ybUniverse.getSpec().setUseYbdbInbuiltYbc(true);

    ybUniverseReconciler.editUniverse(defaultCustomer, oldUniverse, ybUniverse);

    ArgumentCaptor<KubernetesToggleImmutableYbcParams> paramsCaptor =
        ArgumentCaptor.forClass(KubernetesToggleImmutableYbcParams.class);
    Mockito.verify(upgradeUniverseHandler, Mockito.times(1))
        .kubernetesToggleImmutableYbc(paramsCaptor.capture(), eq(defaultCustomer), eq(oldUniverse));
    assertTrue(
        "toggle params should have useYbdbInbuiltYbc true",
        paramsCaptor.getValue().isUseYbdbInbuiltYbc());
    assertEquals(oldUniverse.getUniverseUUID(), paramsCaptor.getValue().getUniverseUUID());
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

  /*--- Tests for tserverVolume/masterVolume deviceInfo changes ---*/

  @Test
  public void testDeviceInfoChangeTriggeredFromTserverVolume() throws Exception {
    String universeName = "test-tserver-volume-deviceinfo";
    YBUniverse ybUniverse = ModelFactory.createYbUniverse(universeName, defaultProvider);

    // Remove old deviceInfo and set tserverVolume instead
    ybUniverse.getSpec().setDeviceInfo(null);
    io.yugabyte.operator.v1alpha1.ybuniversespec.TserverVolume tserverVolume =
        new io.yugabyte.operator.v1alpha1.ybuniversespec.TserverVolume();
    tserverVolume.setVolumeSize(100L);
    tserverVolume.setNumVolumes(3L);
    tserverVolume.setStorageClass("fast-ssd");
    ybUniverse.getSpec().setTserverVolume(tserverVolume);

    // Create availability zones first
    Region region = Region.create(defaultProvider, "region-1", "Region 1", "default-image");
    AvailabilityZone az = AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");

    // Create existing universe with different deviceInfo
    UniverseDefinitionTaskParams taskParams =
        ybUniverseReconciler.createTaskParams(ybUniverse, defaultCustomer.getUuid());
    // Modify deviceInfo to be different
    taskParams.getPrimaryCluster().userIntent.deviceInfo.volumeSize = 50;
    taskParams.getPrimaryCluster().userIntent.deviceInfo.numVolumes = 1;
    Universe existingUniverse = Universe.create(taskParams, defaultCustomer.getId());
    existingUniverse = ModelFactory.addNodesToUniverse(existingUniverse.getUniverseUUID(), 3);

    // Update nodes to use the real AZ UUID and ensure placementInfo is set up properly
    Universe.saveDetails(
        existingUniverse.getUniverseUUID(),
        u -> {
          UniverseDefinitionTaskParams details = u.getUniverseDetails();
          // Update all nodes to use the real AZ UUID
          for (NodeDetails node : details.nodeDetailsSet) {
            if (node.isInPlacement(details.getPrimaryCluster().uuid)) {
              node.azUuid = az.getUuid();
              node.cloudInfo.az = az.getCode();
              node.cloudInfo.region = region.getCode();
            }
          }
          // Create placementInfo from the nodes
          Map<UUID, Integer> azToNumNodesMap = new HashMap<>();
          for (NodeDetails node : details.nodeDetailsSet) {
            if (node.isInPlacement(details.getPrimaryCluster().uuid)) {
              azToNumNodesMap.put(node.azUuid, azToNumNodesMap.getOrDefault(node.azUuid, 0) + 1);
            }
          }
          if (!azToNumNodesMap.isEmpty()) {
            details.getPrimaryCluster().placementInfo =
                ModelFactory.constructPlacementInfoObject(azToNumNodesMap);
          }
          u.setUniverseDetails(details);
        });
    existingUniverse = Universe.getOrBadRequest(existingUniverse.getUniverseUUID());

    Mockito.when(
            confGetter.getConfForScope(
                any(Universe.class), eq(UniverseConfKeys.rollingOpsWaitAfterEachPodMs)))
        .thenReturn(10000);

    // Call editUniverse - should trigger deviceInfo change
    ybUniverseReconciler.editUniverse(defaultCustomer, existingUniverse, ybUniverse);

    // Verify that universeCRUDHandler.configure and update were called (EditKubernetesUniverse
    // task)
    ArgumentCaptor<UniverseConfigureTaskParams> configureParamsCaptor =
        ArgumentCaptor.forClass(UniverseConfigureTaskParams.class);
    Mockito.verify(universeCRUDHandler, Mockito.times(1))
        .configure(eq(defaultCustomer), configureParamsCaptor.capture());
    Mockito.verify(universeCRUDHandler, Mockito.times(1))
        .update(eq(defaultCustomer), eq(existingUniverse), configureParamsCaptor.capture());

    // Verify deviceInfo was updated from tserverVolume
    UniverseConfigureTaskParams capturedParams = configureParamsCaptor.getAllValues().get(0);
    assertEquals(
        100, capturedParams.getPrimaryCluster().userIntent.deviceInfo.volumeSize.intValue());
    assertEquals(3, capturedParams.getPrimaryCluster().userIntent.deviceInfo.numVolumes.intValue());
    assertEquals("fast-ssd", capturedParams.getPrimaryCluster().userIntent.deviceInfo.storageClass);
  }

  @Test
  public void testDeviceInfoChangeTriggeredFromMasterVolume() throws Exception {
    String universeName = "test-master-volume-deviceinfo";
    YBUniverse ybUniverse = ModelFactory.createYbUniverse(universeName, defaultProvider);

    // Remove old deviceInfo and set masterVolume instead
    ybUniverse.getSpec().setDeviceInfo(null);
    io.yugabyte.operator.v1alpha1.ybuniversespec.MasterVolume masterVolume =
        new io.yugabyte.operator.v1alpha1.ybuniversespec.MasterVolume();
    masterVolume.setVolumeSize(75L);
    masterVolume.setNumVolumes(1L);
    masterVolume.setStorageClass("master-storage");
    ybUniverse.getSpec().setMasterVolume(masterVolume);

    // Create availability zones first
    Region region = Region.create(defaultProvider, "region-1", "Region 1", "default-image");
    AvailabilityZone az = AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");

    // Create existing universe with different masterDeviceInfo
    UniverseDefinitionTaskParams taskParams =
        ybUniverseReconciler.createTaskParams(ybUniverse, defaultCustomer.getUuid());
    taskParams.getPrimaryCluster().userIntent.deviceInfo = new DeviceInfo();
    taskParams.getPrimaryCluster().userIntent.deviceInfo.volumeSize = 100;
    // Modify masterDeviceInfo to be different
    taskParams.getPrimaryCluster().userIntent.masterDeviceInfo.volumeSize = 50;
    Universe existingUniverse = Universe.create(taskParams, defaultCustomer.getId());
    existingUniverse = ModelFactory.addNodesToUniverse(existingUniverse.getUniverseUUID(), 3);

    // Update nodes to use the real AZ UUID and ensure placementInfo is set up properly
    Universe.saveDetails(
        existingUniverse.getUniverseUUID(),
        u -> {
          UniverseDefinitionTaskParams details = u.getUniverseDetails();
          // Update all nodes to use the real AZ UUID
          for (NodeDetails node : details.nodeDetailsSet) {
            if (node.isInPlacement(details.getPrimaryCluster().uuid)) {
              node.azUuid = az.getUuid();
              node.cloudInfo.az = az.getCode();
              node.cloudInfo.region = region.getCode();
            }
          }
          // Create placementInfo from the nodes
          Map<UUID, Integer> azToNumNodesMap = new HashMap<>();
          for (NodeDetails node : details.nodeDetailsSet) {
            if (node.isInPlacement(details.getPrimaryCluster().uuid)) {
              azToNumNodesMap.put(node.azUuid, azToNumNodesMap.getOrDefault(node.azUuid, 0) + 1);
            }
          }
          if (!azToNumNodesMap.isEmpty()) {
            details.getPrimaryCluster().placementInfo =
                ModelFactory.constructPlacementInfoObject(azToNumNodesMap);
          }
          u.setUniverseDetails(details);
        });
    existingUniverse = Universe.getOrBadRequest(existingUniverse.getUniverseUUID());

    Mockito.when(
            confGetter.getConfForScope(
                any(Universe.class), eq(UniverseConfKeys.rollingOpsWaitAfterEachPodMs)))
        .thenReturn(10000);

    // Call editUniverse - should trigger deviceInfo change
    ybUniverseReconciler.editUniverse(defaultCustomer, existingUniverse, ybUniverse);

    // Verify that universeCRUDHandler.configure and update were called
    ArgumentCaptor<UniverseConfigureTaskParams> configureParamsCaptor =
        ArgumentCaptor.forClass(UniverseConfigureTaskParams.class);
    Mockito.verify(universeCRUDHandler, Mockito.times(1))
        .configure(eq(defaultCustomer), configureParamsCaptor.capture());
    Mockito.verify(universeCRUDHandler, Mockito.times(1))
        .update(eq(defaultCustomer), eq(existingUniverse), configureParamsCaptor.capture());

    // Verify masterDeviceInfo was updated from masterVolume
    UniverseConfigureTaskParams capturedParams = configureParamsCaptor.getAllValues().get(0);
    assertEquals(
        75, capturedParams.getPrimaryCluster().userIntent.masterDeviceInfo.volumeSize.intValue());
    assertEquals(
        1, capturedParams.getPrimaryCluster().userIntent.masterDeviceInfo.numVolumes.intValue());
    assertEquals(
        "master-storage",
        capturedParams.getPrimaryCluster().userIntent.masterDeviceInfo.storageClass);
  }

  @Test
  public void testAZOverridesAppliedWhenPerAZPresent() throws Exception {
    String universeName = "test-peraz-overrides";
    YBUniverse ybUniverse = ModelFactory.createYbUniverse(universeName, defaultProvider);

    // Create availability zones in the provider
    Region region = Region.create(defaultProvider, "region-1", "Region 1", "default-image");
    AvailabilityZone az1 = AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
    AvailabilityZone az2 = AvailabilityZone.createOrThrow(region, "az-2", "AZ 2", "subnet-2");

    // Remove old deviceInfo and set tserverVolume with perAZ
    ybUniverse.getSpec().setDeviceInfo(null);
    io.yugabyte.operator.v1alpha1.ybuniversespec.TserverVolume tserverVolume =
        new io.yugabyte.operator.v1alpha1.ybuniversespec.TserverVolume();
    tserverVolume.setVolumeSize(100L);
    tserverVolume.setNumVolumes(3L);
    tserverVolume.setStorageClass("base-storage");

    // Add perAZ overrides
    Map<String, io.yugabyte.operator.v1alpha1.ybuniversespec.tservervolume.PerAZ> perAZMap =
        new HashMap<>();
    io.yugabyte.operator.v1alpha1.ybuniversespec.tservervolume.PerAZ az1Volume =
        new io.yugabyte.operator.v1alpha1.ybuniversespec.tservervolume.PerAZ();
    az1Volume.setVolumeSize(200L);
    az1Volume.setStorageClass("az1-storage");
    perAZMap.put("az-1", az1Volume);

    io.yugabyte.operator.v1alpha1.ybuniversespec.tservervolume.PerAZ az2Volume =
        new io.yugabyte.operator.v1alpha1.ybuniversespec.tservervolume.PerAZ();
    az2Volume.setNumVolumes(5L);
    perAZMap.put("az-2", az2Volume);

    // Create existing universe WITHOUT perAZ overrides first
    ybUniverse.getSpec().setTserverVolume(tserverVolume);
    UniverseDefinitionTaskParams taskParams =
        ybUniverseReconciler.createTaskParams(ybUniverse, defaultCustomer.getUuid());
    Universe existingUniverse = Universe.create(taskParams, defaultCustomer.getId());
    existingUniverse = ModelFactory.addNodesToUniverse(existingUniverse.getUniverseUUID(), 3);

    // Update nodes to use the real AZ UUIDs (distribute across az1 and az2) and ensure
    // placementInfo is set up properly
    final AvailabilityZone finalAz1 = az1;
    final AvailabilityZone finalAz2 = az2;
    Universe.saveDetails(
        existingUniverse.getUniverseUUID(),
        u -> {
          UniverseDefinitionTaskParams details = u.getUniverseDetails();
          // Update all nodes to use the real AZ UUIDs, distribute across az1 and az2
          int nodeIndex = 0;
          for (NodeDetails node : details.nodeDetailsSet) {
            if (node.isInPlacement(details.getPrimaryCluster().uuid)) {
              // Alternate between az1 and az2
              AvailabilityZone targetAz = (nodeIndex % 2 == 0) ? finalAz1 : finalAz2;
              node.azUuid = targetAz.getUuid();
              node.cloudInfo.az = targetAz.getCode();
              node.cloudInfo.region = targetAz.getRegion().getCode();
              nodeIndex++;
            }
          }
          // Create placementInfo from the nodes
          Map<UUID, Integer> azToNumNodesMap = new HashMap<>();
          for (NodeDetails node : details.nodeDetailsSet) {
            if (node.isInPlacement(details.getPrimaryCluster().uuid)) {
              azToNumNodesMap.put(node.azUuid, azToNumNodesMap.getOrDefault(node.azUuid, 0) + 1);
            }
          }
          if (!azToNumNodesMap.isEmpty()) {
            details.getPrimaryCluster().placementInfo =
                ModelFactory.constructPlacementInfoObject(azToNumNodesMap);
          }
          u.setUniverseDetails(details);
        });
    existingUniverse = Universe.getOrBadRequest(existingUniverse.getUniverseUUID());

    // Now add perAZ overrides to the CR to trigger a device info change
    tserverVolume.setPerAZ(perAZMap);
    ybUniverse.getSpec().setTserverVolume(tserverVolume);

    Mockito.when(
            confGetter.getConfForScope(
                any(Universe.class), eq(UniverseConfKeys.rollingOpsWaitAfterEachPodMs)))
        .thenReturn(10000);

    // Call editUniverse - should trigger deviceInfo change due to new perAZ overrides
    ybUniverseReconciler.editUniverse(defaultCustomer, existingUniverse, ybUniverse);

    // Verify that universeCRUDHandler.configure and update were called
    ArgumentCaptor<UniverseConfigureTaskParams> configureParamsCaptor =
        ArgumentCaptor.forClass(UniverseConfigureTaskParams.class);
    Mockito.verify(universeCRUDHandler, Mockito.times(1))
        .configure(eq(defaultCustomer), configureParamsCaptor.capture());
    Mockito.verify(universeCRUDHandler, Mockito.times(1))
        .update(eq(defaultCustomer), eq(existingUniverse), configureParamsCaptor.capture());

    // Verify AZ overrides were applied
    UniverseConfigureTaskParams capturedParams = configureParamsCaptor.getAllValues().get(0);
    assertNotNull(capturedParams.getPrimaryCluster().userIntent.getUserIntentOverrides());
    assertNotNull(
        capturedParams.getPrimaryCluster().userIntent.getUserIntentOverrides().getAzOverrides());

    // Verify az1 overrides
    AZOverrides az1Overrides =
        capturedParams
            .getPrimaryCluster()
            .userIntent
            .getUserIntentOverrides()
            .getAzOverrides()
            .get(az1.getUuid());
    assertNotNull(az1Overrides);
    assertNotNull(az1Overrides.getPerProcess());
    assertNotNull(az1Overrides.getPerProcess().get(ServerType.TSERVER));
    DeviceInfo az1DeviceInfo = az1Overrides.getPerProcess().get(ServerType.TSERVER).getDeviceInfo();
    assertEquals(200, az1DeviceInfo.volumeSize.intValue());
    assertEquals("az1-storage", az1DeviceInfo.storageClass);

    // Verify az2 overrides
    AZOverrides az2Overrides =
        capturedParams
            .getPrimaryCluster()
            .userIntent
            .getUserIntentOverrides()
            .getAzOverrides()
            .get(az2.getUuid());
    assertNotNull(az2Overrides);
    assertNotNull(az2Overrides.getPerProcess());
    assertNotNull(az2Overrides.getPerProcess().get(ServerType.TSERVER));
    DeviceInfo az2DeviceInfo = az2Overrides.getPerProcess().get(ServerType.TSERVER).getDeviceInfo();
    assertEquals(5, az2DeviceInfo.numVolumes.intValue());
  }

  @Test
  public void testAZOverridesNotAppliedWhenPerAZNotPresent() throws Exception {
    String universeName = "test-no-peraz-overrides";
    YBUniverse ybUniverse = ModelFactory.createYbUniverse(universeName, defaultProvider);

    // Remove old deviceInfo and set tserverVolume WITHOUT perAZ (initial smaller volume)
    ybUniverse.getSpec().setDeviceInfo(null);
    io.yugabyte.operator.v1alpha1.ybuniversespec.TserverVolume tserverVolume =
        new io.yugabyte.operator.v1alpha1.ybuniversespec.TserverVolume();
    tserverVolume.setVolumeSize(50L);
    tserverVolume.setNumVolumes(3L);
    tserverVolume.setStorageClass("base-storage");
    tserverVolume.setPerAZ(null);
    ybUniverse.getSpec().setTserverVolume(tserverVolume);

    // Create availability zones first
    Region region = Region.create(defaultProvider, "region-1", "Region 1", "default-image");
    AvailabilityZone az = AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");

    // Create existing universe with the initial volume size
    UniverseDefinitionTaskParams taskParams =
        ybUniverseReconciler.createTaskParams(ybUniverse, defaultCustomer.getUuid());
    Universe existingUniverse = Universe.create(taskParams, defaultCustomer.getId());
    existingUniverse = ModelFactory.addNodesToUniverse(existingUniverse.getUniverseUUID(), 3);

    // Update nodes to use the real AZ UUID and ensure placementInfo is set up properly
    final AvailabilityZone finalAz = az;
    Universe.saveDetails(
        existingUniverse.getUniverseUUID(),
        u -> {
          UniverseDefinitionTaskParams details = u.getUniverseDetails();
          // Update all nodes to use the real AZ UUID
          for (NodeDetails node : details.nodeDetailsSet) {
            if (node.isInPlacement(details.getPrimaryCluster().uuid)) {
              node.azUuid = finalAz.getUuid();
              node.cloudInfo.az = finalAz.getCode();
              node.cloudInfo.region = finalAz.getRegion().getCode();
            }
          }
          // Create placementInfo from the nodes
          Map<UUID, Integer> azToNumNodesMap = new HashMap<>();
          for (NodeDetails node : details.nodeDetailsSet) {
            if (node.isInPlacement(details.getPrimaryCluster().uuid)) {
              azToNumNodesMap.put(node.azUuid, azToNumNodesMap.getOrDefault(node.azUuid, 0) + 1);
            }
          }
          if (!azToNumNodesMap.isEmpty()) {
            details.getPrimaryCluster().placementInfo =
                ModelFactory.constructPlacementInfoObject(azToNumNodesMap);
          }
          u.setUniverseDetails(details);
        });
    existingUniverse = Universe.getOrBadRequest(existingUniverse.getUniverseUUID());

    // Now update the CR to a larger volume size (still no perAZ) to trigger an edit
    tserverVolume.setVolumeSize(100L);
    ybUniverse.getSpec().setTserverVolume(tserverVolume);

    Mockito.when(
            confGetter.getConfForScope(
                any(Universe.class), eq(UniverseConfKeys.rollingOpsWaitAfterEachPodMs)))
        .thenReturn(10000);

    // Call editUniverse - should detect volumeSize change
    ybUniverseReconciler.editUniverse(defaultCustomer, existingUniverse, ybUniverse);

    // Verify that universeCRUDHandler.configure and update were called
    ArgumentCaptor<UniverseConfigureTaskParams> configureParamsCaptor =
        ArgumentCaptor.forClass(UniverseConfigureTaskParams.class);
    Mockito.verify(universeCRUDHandler, Mockito.times(1))
        .configure(eq(defaultCustomer), configureParamsCaptor.capture());
    Mockito.verify(universeCRUDHandler, Mockito.times(1))
        .update(eq(defaultCustomer), eq(existingUniverse), configureParamsCaptor.capture());

    // Verify AZ overrides were NOT applied (should be null or empty)
    UniverseConfigureTaskParams capturedParams = configureParamsCaptor.getAllValues().get(0);
    if (capturedParams.getPrimaryCluster().userIntent.getUserIntentOverrides() != null
        && capturedParams.getPrimaryCluster().userIntent.getUserIntentOverrides().getAzOverrides()
            != null) {
      // If AZ overrides exist, they should be empty (no perAZ volume overrides)
      assertTrue(
          capturedParams
                  .getPrimaryCluster()
                  .userIntent
                  .getUserIntentOverrides()
                  .getAzOverrides()
                  .isEmpty()
              || capturedParams
                  .getPrimaryCluster()
                  .userIntent
                  .getUserIntentOverrides()
                  .getAzOverrides()
                  .values()
                  .stream()
                  .noneMatch(
                      azOverrides ->
                          azOverrides.getPerProcess() != null
                              && azOverrides.getPerProcess().containsKey(ServerType.TSERVER)
                              && azOverrides
                                      .getPerProcess()
                                      .get(
                                          com.yugabyte.yw.commissioner.tasks.UniverseTaskBase
                                              .ServerType.TSERVER)
                                      .getDeviceInfo()
                                  != null));
    }

    // Verify base deviceInfo was still updated
    assertEquals(
        100, capturedParams.getPrimaryCluster().userIntent.deviceInfo.volumeSize.intValue());
    assertEquals(3, capturedParams.getPrimaryCluster().userIntent.deviceInfo.numVolumes.intValue());
    assertEquals(
        "base-storage", capturedParams.getPrimaryCluster().userIntent.deviceInfo.storageClass);
  }

  @Test
  public void testMasterVolumePerAZOverridesApplied() throws Exception {
    String universeName = "test-master-peraz-overrides";
    YBUniverse ybUniverse = ModelFactory.createYbUniverse(universeName, defaultProvider);

    // Create availability zones in the provider
    Region region = Region.create(defaultProvider, "region-1", "Region 1", "default-image");
    AvailabilityZone az = AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");

    // Remove old deviceInfo and set masterVolume with perAZ
    ybUniverse.getSpec().setDeviceInfo(null);
    io.yugabyte.operator.v1alpha1.ybuniversespec.MasterVolume masterVolume =
        new io.yugabyte.operator.v1alpha1.ybuniversespec.MasterVolume();
    masterVolume.setVolumeSize(50L);
    masterVolume.setNumVolumes(1L);
    masterVolume.setStorageClass("base-master-storage");

    // Add perAZ overrides for master
    Map<String, io.yugabyte.operator.v1alpha1.ybuniversespec.mastervolume.PerAZ> perAZMap =
        new HashMap<>();
    io.yugabyte.operator.v1alpha1.ybuniversespec.mastervolume.PerAZ az1Volume =
        new io.yugabyte.operator.v1alpha1.ybuniversespec.mastervolume.PerAZ();
    az1Volume.setVolumeSize(100L);
    az1Volume.setStorageClass("az1-master-storage");
    perAZMap.put("az-1", az1Volume);

    masterVolume.setPerAZ(perAZMap);
    ybUniverse.getSpec().setMasterVolume(masterVolume);

    // Create existing universe
    UniverseDefinitionTaskParams taskParams =
        ybUniverseReconciler.createTaskParams(ybUniverse, defaultCustomer.getUuid());
    taskParams.getPrimaryCluster().userIntent.deviceInfo = new DeviceInfo();
    taskParams.getPrimaryCluster().userIntent.deviceInfo.volumeSize = 100;
    Universe existingUniverse = Universe.create(taskParams, defaultCustomer.getId());
    existingUniverse = ModelFactory.addNodesToUniverse(existingUniverse.getUniverseUUID(), 3);

    // Update nodes to use the real AZ UUID and ensure placementInfo is set up properly
    final AvailabilityZone finalAz = az;
    Universe.saveDetails(
        existingUniverse.getUniverseUUID(),
        u -> {
          UniverseDefinitionTaskParams details = u.getUniverseDetails();
          // Update all nodes to use the real AZ UUID
          for (NodeDetails node : details.nodeDetailsSet) {
            if (node.isInPlacement(details.getPrimaryCluster().uuid)) {
              node.azUuid = finalAz.getUuid();
              node.cloudInfo.az = finalAz.getCode();
              node.cloudInfo.region = finalAz.getRegion().getCode();
            }
          }
          // Create placementInfo from the nodes
          Map<UUID, Integer> azToNumNodesMap = new HashMap<>();
          for (NodeDetails node : details.nodeDetailsSet) {
            if (node.isInPlacement(details.getPrimaryCluster().uuid)) {
              azToNumNodesMap.put(node.azUuid, azToNumNodesMap.getOrDefault(node.azUuid, 0) + 1);
            }
          }
          if (!azToNumNodesMap.isEmpty()) {
            details.getPrimaryCluster().placementInfo =
                ModelFactory.constructPlacementInfoObject(azToNumNodesMap);
          }
          u.setUniverseDetails(details);
        });
    existingUniverse = Universe.getOrBadRequest(existingUniverse.getUniverseUUID());

    Mockito.when(
            confGetter.getConfForScope(
                any(Universe.class), eq(UniverseConfKeys.rollingOpsWaitAfterEachPodMs)))
        .thenReturn(10000);

    // Call editUniverse
    ybUniverseReconciler.editUniverse(defaultCustomer, existingUniverse, ybUniverse);

    // Verify that universeCRUDHandler.configure and update were called
    ArgumentCaptor<UniverseConfigureTaskParams> configureParamsCaptor =
        ArgumentCaptor.forClass(UniverseConfigureTaskParams.class);
    Mockito.verify(universeCRUDHandler, Mockito.times(1))
        .configure(eq(defaultCustomer), configureParamsCaptor.capture());
    Mockito.verify(universeCRUDHandler, Mockito.times(1))
        .update(eq(defaultCustomer), eq(existingUniverse), configureParamsCaptor.capture());

    // Verify master AZ overrides were applied
    UniverseConfigureTaskParams capturedParams = configureParamsCaptor.getAllValues().get(0);
    assertNotNull(capturedParams.getPrimaryCluster().userIntent.getUserIntentOverrides());
    assertNotNull(
        capturedParams.getPrimaryCluster().userIntent.getUserIntentOverrides().getAzOverrides());

    // Verify az1 master overrides
    AZOverrides az1Overrides =
        capturedParams
            .getPrimaryCluster()
            .userIntent
            .getUserIntentOverrides()
            .getAzOverrides()
            .get(az.getUuid());
    assertNotNull(az1Overrides);
    assertNotNull(az1Overrides.getPerProcess());
    assertNotNull(az1Overrides.getPerProcess().get(ServerType.MASTER));
    DeviceInfo az1MasterDeviceInfo =
        az1Overrides.getPerProcess().get(ServerType.MASTER).getDeviceInfo();
    assertEquals(100, az1MasterDeviceInfo.volumeSize.intValue());
    assertEquals("az1-master-storage", az1MasterDeviceInfo.storageClass);
  }

  /*--- Tests for create-flow applyKubernetesOperatorVolumeOverrides logic ---*/

  /**
   * Helper: builds a Kubernetes AZ with the given storage class set on its KubernetesRegionInfo, so
   * that {@code CloudInfoInterface.fetchEnvVars(zone)} surfaces it as the {@code STORAGE_CLASS} env
   * var consumed by {@code KubernetesUtil.generateVolumeOverridesForUserIntent}.
   */
  private AvailabilityZone createK8sAZ(Region region, String code, String storageClass) {
    AvailabilityZone az =
        AvailabilityZone.createOrThrow(region, code, code.toUpperCase(), "subnet-" + code);
    az.setDetails(new AvailabilityZoneDetails());
    az.getDetails().setCloudInfo(new AZCloudInfo());
    KubernetesRegionInfo k8sInfo = new KubernetesRegionInfo();
    if (storageClass != null) {
      k8sInfo.setKubernetesStorageClass(storageClass);
    }
    az.getDetails().getCloudInfo().setKubernetes(k8sInfo);
    az.save();
    return az;
  }

  /**
   * Helper: builds a minimal {@link UniverseConfigureTaskParams} suitable for invoking {@code
   * applyKubernetesOperatorVolumeOverrides} - one PRIMARY cluster with the given userIntent and a
   * placementInfo containing the provided AZs (1 node each).
   */
  private UniverseConfigureTaskParams buildPrimaryClusterTaskParams(
      UserIntent userIntent, AvailabilityZone... zones) {
    UniverseConfigureTaskParams taskParams = new UniverseConfigureTaskParams();
    Cluster cluster = new Cluster(ClusterType.PRIMARY, userIntent);
    Map<UUID, Integer> azToNodes = new HashMap<>();
    for (AvailabilityZone z : zones) {
      azToNodes.put(z.getUuid(), 1);
    }
    cluster.placementInfo = ModelFactory.constructPlacementInfoObject(azToNodes);
    taskParams.clusters.add(cluster);
    taskParams.isKubernetesOperatorControlled = true;
    return taskParams;
  }

  @Test
  public void testCreateUniverseFallsBackToProviderStorageClassWhenNoPerAZ() throws Exception {
    String universeName = "test-create-no-peraz-fallbackused";

    // Provider-level (per-AZ) storage class - this is what is reachable through
    // CloudInfoInterface.fetchEnvVars(zone) and so what the fallback path picks up.
    String providerStorageClass = "provider-default-sc";
    Region region = Region.create(defaultProvider, "region-1", "Region 1", "default-image");
    AvailabilityZone az1 = createK8sAZ(region, "az-1", providerStorageClass);
    AvailabilityZone az2 = createK8sAZ(region, "az-2", providerStorageClass);

    // CR with base tserverVolume and NO perAZ overrides.
    YBUniverse ybUniverse = ModelFactory.createYbUniverse(universeName, defaultProvider);
    ybUniverse.getSpec().setDeviceInfo(null);
    io.yugabyte.operator.v1alpha1.ybuniversespec.TserverVolume tserverVolume =
        new io.yugabyte.operator.v1alpha1.ybuniversespec.TserverVolume();
    tserverVolume.setVolumeSize(100L);
    tserverVolume.setNumVolumes(1L);
    ybUniverse.getSpec().setTserverVolume(tserverVolume);

    // Build minimal task params reflecting what CRUDHandler.configure would produce on create -
    // primary cluster with finalized placementInfo and a userIntent with base deviceInfo.
    UserIntent userIntent = new UserIntent();
    userIntent.universeName = universeName;
    userIntent.provider = defaultProvider.getUuid().toString();
    userIntent.deviceInfo = new DeviceInfo();
    userIntent.deviceInfo.volumeSize = 100;
    userIntent.deviceInfo.numVolumes = 1;
    userIntent.masterDeviceInfo = new DeviceInfo();
    userIntent.masterDeviceInfo.volumeSize = 50;
    userIntent.masterDeviceInfo.numVolumes = 1;
    UniverseConfigureTaskParams taskParams = buildPrimaryClusterTaskParams(userIntent, az1, az2);

    // Create-path: existingUniverse is null.
    ybUniverseReconciler.applyKubernetesOperatorVolumeOverrides(
        taskParams, ybUniverse, defaultCustomer.getUuid(), null /* existingUniverse */);

    // Both AZs should get tserver+master overrides whose deviceInfo.storageClass is the
    // provider-level storage class (no perAZ entry was provided for any server type).
    assertNotNull(taskParams.getPrimaryCluster().userIntent.getUserIntentOverrides());
    Map<UUID, AZOverrides> azOverrides =
        taskParams.getPrimaryCluster().userIntent.getUserIntentOverrides().getAzOverrides();
    assertNotNull(azOverrides);
    assertEquals(2, azOverrides.size());
    for (UUID azUuid : Set.of(az1.getUuid(), az2.getUuid())) {
      AZOverrides azOv = azOverrides.get(azUuid);
      assertNotNull("Expected azOverrides for " + azUuid, azOv);
      assertNotNull(azOv.getPerProcess());
      // tserver fallback applied
      assertNotNull(azOv.getPerProcess().get(ServerType.TSERVER));
      DeviceInfo tserverDi = azOv.getPerProcess().get(ServerType.TSERVER).getDeviceInfo();
      assertNotNull(tserverDi);
      assertEquals(providerStorageClass, tserverDi.storageClass);
      // master fallback applied
      assertNotNull(azOv.getPerProcess().get(ServerType.MASTER));
      DeviceInfo masterDi = azOv.getPerProcess().get(ServerType.MASTER).getDeviceInfo();
      assertNotNull(masterDi);
      assertEquals(providerStorageClass, masterDi.storageClass);
    }
  }

  @Test
  public void testCreateUniversePerAZOverridesAppliedOnlyToSpecifiedAZs() throws Exception {
    String universeName = "test-create-peraz-only-specified";

    // Two AZs, no provider-level storage class so we can isolate the perAZ behavior.
    Region region = Region.create(defaultProvider, "region-1", "Region 1", "default-image");
    AvailabilityZone az1 = createK8sAZ(region, "az-1", null /* storageClass */);
    AvailabilityZone az2 = createK8sAZ(region, "az-2", null /* storageClass */);

    // CR has base tserver volume + perAZ override for az-1 only (master has no perAZ).
    YBUniverse ybUniverse = ModelFactory.createYbUniverse(universeName, defaultProvider);
    ybUniverse.getSpec().setDeviceInfo(null);
    io.yugabyte.operator.v1alpha1.ybuniversespec.TserverVolume tserverVolume =
        new io.yugabyte.operator.v1alpha1.ybuniversespec.TserverVolume();
    tserverVolume.setVolumeSize(100L);
    tserverVolume.setNumVolumes(1L);
    tserverVolume.setStorageClass("base-tserver-sc");
    Map<String, io.yugabyte.operator.v1alpha1.ybuniversespec.tservervolume.PerAZ> perAZMap =
        new HashMap<>();
    io.yugabyte.operator.v1alpha1.ybuniversespec.tservervolume.PerAZ az1PerAZ =
        new io.yugabyte.operator.v1alpha1.ybuniversespec.tservervolume.PerAZ();
    az1PerAZ.setVolumeSize(250L);
    az1PerAZ.setStorageClass("az1-special-sc");
    perAZMap.put("az-1", az1PerAZ);
    tserverVolume.setPerAZ(perAZMap);
    ybUniverse.getSpec().setTserverVolume(tserverVolume);

    // Base userIntent reflects the base tserverVolume values - which is what nodes in AZs
    // without a perAZ entry will end up using (no override is added for them).
    UserIntent userIntent = new UserIntent();
    userIntent.universeName = universeName;
    userIntent.provider = defaultProvider.getUuid().toString();
    userIntent.deviceInfo = new DeviceInfo();
    userIntent.deviceInfo.volumeSize = 100;
    userIntent.deviceInfo.numVolumes = 1;
    userIntent.deviceInfo.storageClass = "base-tserver-sc";
    userIntent.masterDeviceInfo = new DeviceInfo();
    userIntent.masterDeviceInfo.volumeSize = 50;
    userIntent.masterDeviceInfo.numVolumes = 1;
    UniverseConfigureTaskParams taskParams = buildPrimaryClusterTaskParams(userIntent, az1, az2);

    ybUniverseReconciler.applyKubernetesOperatorVolumeOverrides(
        taskParams, ybUniverse, defaultCustomer.getUuid(), null /* existingUniverse */);

    // Expectation: tserver has perAZ in spec, so its overrides come exclusively from perAZ -
    //   * az-1 has TSERVER override with the perAZ-specified deviceInfo,
    //   * az-2 has NO TSERVER entry (it falls back to userIntent.deviceInfo at runtime).
    // Master has no perAZ in spec, so it goes through the fallback path - but with no provider
    // storage class and no helm overrides set, the fallback produces no master entries either.
    UserIntent resultIntent = taskParams.getPrimaryCluster().userIntent;
    assertNotNull(resultIntent.getUserIntentOverrides());
    Map<UUID, AZOverrides> azOverrides = resultIntent.getUserIntentOverrides().getAzOverrides();
    assertNotNull(azOverrides);

    // az-1 should have a TSERVER override matching the perAZ entry.
    AZOverrides az1Ov = azOverrides.get(az1.getUuid());
    assertNotNull(az1Ov);
    assertNotNull(az1Ov.getPerProcess());
    assertNotNull(az1Ov.getPerProcess().get(ServerType.TSERVER));
    DeviceInfo az1TserverDi = az1Ov.getPerProcess().get(ServerType.TSERVER).getDeviceInfo();
    assertEquals(250, az1TserverDi.volumeSize.intValue());
    assertEquals("az1-special-sc", az1TserverDi.storageClass);

    // az-2 should NOT carry a TSERVER override - the perAZ map didn't include it and the
    // fallback path is skipped for tserver because perAZ is present in the spec.
    AZOverrides az2Ov = azOverrides.get(az2.getUuid());
    assertTrue(
        "az-2 should not have a TSERVER override; base userIntent.deviceInfo is used instead",
        az2Ov == null
            || az2Ov.getPerProcess() == null
            || !az2Ov.getPerProcess().containsKey(ServerType.TSERVER));

    // Base tserver deviceInfo on userIntent must remain untouched - that's what AZs without
    // a perAZ entry rely on.
    assertEquals(100, resultIntent.deviceInfo.volumeSize.intValue());
    assertEquals(1, resultIntent.deviceInfo.numVolumes.intValue());
    assertEquals("base-tserver-sc", resultIntent.deviceInfo.storageClass);
  }

  /*--- Tests for edit-flow applyKubernetesOperatorVolumeOverrides logic ---*/

  /**
   * Helper: builds an "existing" universe with a primary cluster whose placementInfo references the
   * given AZs (1 node each). The Universe is persisted to the test DB and returned, suitable for
   * use as the {@code existingUniverse} arg to {@code applyKubernetesOperatorVolumeOverrides}.
   */
  private Universe createExistingPrimaryUniverse(
      String universeName, UserIntent userIntent, AvailabilityZone... zones) {
    UniverseDefinitionTaskParams details = new UniverseDefinitionTaskParams();
    details.setUniverseUUID(UUID.randomUUID());
    Cluster cluster = new Cluster(ClusterType.PRIMARY, userIntent);
    Map<UUID, Integer> azToNodes = new HashMap<>();
    for (AvailabilityZone z : zones) {
      azToNodes.put(z.getUuid(), 1);
    }
    cluster.placementInfo = ModelFactory.constructPlacementInfoObject(azToNodes);
    details.clusters.add(cluster);
    details.isKubernetesOperatorControlled = true;
    return Universe.create(details, defaultCustomer.getId());
  }

  /** Helper: sets a tserver perAZ deviceInfo entry on userIntent.userIntentOverrides for an AZ. */
  private void seedTserverAZOverride(
      UserIntent userIntent, UUID azUuid, int volumeSize, String storageClass) {
    if (userIntent.getUserIntentOverrides() == null) {
      userIntent.setUserIntentOverrides(new UserIntentOverrides());
    }
    UserIntentOverrides overrides = userIntent.getUserIntentOverrides();
    Map<UUID, AZOverrides> azMap =
        overrides.getAzOverrides() != null ? overrides.getAzOverrides() : new HashMap<>();
    AZOverrides azOv = azMap.getOrDefault(azUuid, new AZOverrides());
    Map<ServerType, PerProcessDetails> perProcess =
        azOv.getPerProcess() != null ? azOv.getPerProcess() : new HashMap<>();
    PerProcessDetails ppd = new PerProcessDetails();
    DeviceInfo di = new DeviceInfo();
    di.volumeSize = volumeSize;
    di.numVolumes = 1;
    di.storageClass = storageClass;
    ppd.setDeviceInfo(di);
    perProcess.put(ServerType.TSERVER, ppd);
    azOv.setPerProcess(perProcess);
    azMap.put(azUuid, azOv);
    overrides.setAzOverrides(azMap);
  }

  @Test
  public void testEditUniverseExistingAZOverridesUpdatedWhenPerAZModified() throws Exception {
    String universeName = "test-edit-peraz-modified";

    Region region = Region.create(defaultProvider, "region-1", "Region 1", "default-image");
    AvailabilityZone az1 = createK8sAZ(region, "az-1", null /* storageClass */);
    AvailabilityZone az2 = createK8sAZ(region, "az-2", null /* storageClass */);

    // Existing universe has both AZs and previously stored tserver perAZ overrides.
    UserIntent existingUserIntent = new UserIntent();
    existingUserIntent.universeName = universeName;
    existingUserIntent.provider = defaultProvider.getUuid().toString();
    existingUserIntent.deviceInfo = new DeviceInfo();
    existingUserIntent.deviceInfo.volumeSize = 100;
    existingUserIntent.deviceInfo.numVolumes = 1;
    existingUserIntent.deviceInfo.storageClass = "base-tserver-sc";
    seedTserverAZOverride(existingUserIntent, az1.getUuid(), 100, "az1-old-sc");
    seedTserverAZOverride(existingUserIntent, az2.getUuid(), 100, "az2-old-sc");
    Universe existingUniverse =
        createExistingPrimaryUniverse(universeName, existingUserIntent, az1, az2);

    // CR spec carries the same AZs but with NEW perAZ values for both az1 and az2.
    YBUniverse ybUniverse = ModelFactory.createYbUniverse(universeName, defaultProvider);
    ybUniverse.getSpec().setDeviceInfo(null);
    io.yugabyte.operator.v1alpha1.ybuniversespec.TserverVolume tserverVolume =
        new io.yugabyte.operator.v1alpha1.ybuniversespec.TserverVolume();
    tserverVolume.setVolumeSize(100L);
    tserverVolume.setNumVolumes(1L);
    tserverVolume.setStorageClass("base-tserver-sc");
    Map<String, io.yugabyte.operator.v1alpha1.ybuniversespec.tservervolume.PerAZ> perAZMap =
        new HashMap<>();
    io.yugabyte.operator.v1alpha1.ybuniversespec.tservervolume.PerAZ az1New =
        new io.yugabyte.operator.v1alpha1.ybuniversespec.tservervolume.PerAZ();
    az1New.setVolumeSize(250L);
    az1New.setStorageClass("az1-new-sc");
    perAZMap.put("az-1", az1New);
    io.yugabyte.operator.v1alpha1.ybuniversespec.tservervolume.PerAZ az2New =
        new io.yugabyte.operator.v1alpha1.ybuniversespec.tservervolume.PerAZ();
    az2New.setVolumeSize(350L);
    az2New.setStorageClass("az2-new-sc");
    perAZMap.put("az-2", az2New);
    tserverVolume.setPerAZ(perAZMap);
    ybUniverse.getSpec().setTserverVolume(tserverVolume);

    // The new taskParams cluster userIntent carries over the previously stored overrides
    // (mirrors what the operator passes in on edit).
    UserIntent newUserIntent = new UserIntent();
    newUserIntent.universeName = universeName;
    newUserIntent.provider = defaultProvider.getUuid().toString();
    newUserIntent.deviceInfo = new DeviceInfo();
    newUserIntent.deviceInfo.volumeSize = 100;
    newUserIntent.deviceInfo.numVolumes = 1;
    newUserIntent.deviceInfo.storageClass = "base-tserver-sc";
    seedTserverAZOverride(newUserIntent, az1.getUuid(), 100, "az1-old-sc");
    seedTserverAZOverride(newUserIntent, az2.getUuid(), 100, "az2-old-sc");
    UniverseConfigureTaskParams taskParams = buildPrimaryClusterTaskParams(newUserIntent, az1, az2);

    ybUniverseReconciler.applyKubernetesOperatorVolumeOverrides(
        taskParams, ybUniverse, defaultCustomer.getUuid(), existingUniverse);

    // Expectation: tserver has perAZ in the spec, so existing tserver entries are wiped and
    // both az1 and az2 - even though they are retained from the saved placement - get the
    // freshly specified perAZ values.
    UserIntent resultIntent = taskParams.getPrimaryCluster().userIntent;
    assertNotNull(resultIntent.getUserIntentOverrides());
    Map<UUID, AZOverrides> azOverrides = resultIntent.getUserIntentOverrides().getAzOverrides();
    assertNotNull(azOverrides);

    AZOverrides az1Ov = azOverrides.get(az1.getUuid());
    assertNotNull(az1Ov);
    assertNotNull(az1Ov.getPerProcess());
    assertNotNull(az1Ov.getPerProcess().get(ServerType.TSERVER));
    DeviceInfo az1Di = az1Ov.getPerProcess().get(ServerType.TSERVER).getDeviceInfo();
    assertEquals(250, az1Di.volumeSize.intValue());
    assertEquals("az1-new-sc", az1Di.storageClass);

    AZOverrides az2Ov = azOverrides.get(az2.getUuid());
    assertNotNull(az2Ov);
    assertNotNull(az2Ov.getPerProcess());
    assertNotNull(az2Ov.getPerProcess().get(ServerType.TSERVER));
    DeviceInfo az2Di = az2Ov.getPerProcess().get(ServerType.TSERVER).getDeviceInfo();
    assertEquals(350, az2Di.volumeSize.intValue());
    assertEquals("az2-new-sc", az2Di.storageClass);
  }

  @Test
  public void testEditUniverseNewAZUsesPerAZIfPresentOtherwiseFallback() throws Exception {
    String universeName = "test-edit-new-az-peraz-and-fallback";

    // Provider-level storage class is set on both AZs - this is what the master fallback path
    // picks up for the newly added AZ.
    String providerStorageClass = "provider-default-sc";
    Region region = Region.create(defaultProvider, "region-1", "Region 1", "default-image");
    AvailabilityZone az1 = createK8sAZ(region, "az-1", providerStorageClass);
    AvailabilityZone az2 = createK8sAZ(region, "az-2", providerStorageClass);

    // Existing universe has only az1. The previously stored tserver perAZ override for az1 and
    // a master override for az1 simulate the post-create / post-prior-edit state.
    UserIntent existingUserIntent = new UserIntent();
    existingUserIntent.universeName = universeName;
    existingUserIntent.provider = defaultProvider.getUuid().toString();
    existingUserIntent.deviceInfo = new DeviceInfo();
    existingUserIntent.deviceInfo.volumeSize = 100;
    existingUserIntent.deviceInfo.numVolumes = 1;
    existingUserIntent.deviceInfo.storageClass = "base-tserver-sc";
    existingUserIntent.masterDeviceInfo = new DeviceInfo();
    existingUserIntent.masterDeviceInfo.volumeSize = 50;
    existingUserIntent.masterDeviceInfo.numVolumes = 1;
    seedTserverAZOverride(existingUserIntent, az1.getUuid(), 200, "az1-tserver-existing");
    // Stash an existing master override for az1 so we can verify it is preserved (skipAZs).
    AZOverrides az1Existing =
        existingUserIntent.getUserIntentOverrides().getAzOverrides().get(az1.getUuid());
    PerProcessDetails masterPpd = new PerProcessDetails();
    DeviceInfo masterDi = new DeviceInfo();
    masterDi.storageClass = "az1-master-existing";
    masterPpd.setDeviceInfo(masterDi);
    az1Existing.getPerProcess().put(ServerType.MASTER, masterPpd);
    Universe existingUniverse =
        createExistingPrimaryUniverse(universeName, existingUserIntent, az1);

    // CR spec keeps the perAZ entry for az1 and adds one for the newly added az2. Master has
    // no perAZ block, so master overrides for the new AZ flow through the fallback path.
    YBUniverse ybUniverse = ModelFactory.createYbUniverse(universeName, defaultProvider);
    ybUniverse.getSpec().setDeviceInfo(null);
    io.yugabyte.operator.v1alpha1.ybuniversespec.TserverVolume tserverVolume =
        new io.yugabyte.operator.v1alpha1.ybuniversespec.TserverVolume();
    tserverVolume.setVolumeSize(100L);
    tserverVolume.setNumVolumes(1L);
    tserverVolume.setStorageClass("base-tserver-sc");
    Map<String, io.yugabyte.operator.v1alpha1.ybuniversespec.tservervolume.PerAZ> perAZMap =
        new HashMap<>();
    io.yugabyte.operator.v1alpha1.ybuniversespec.tservervolume.PerAZ az1Spec =
        new io.yugabyte.operator.v1alpha1.ybuniversespec.tservervolume.PerAZ();
    az1Spec.setVolumeSize(200L);
    az1Spec.setStorageClass("az1-tserver-existing");
    perAZMap.put("az-1", az1Spec);
    io.yugabyte.operator.v1alpha1.ybuniversespec.tservervolume.PerAZ az2Spec =
        new io.yugabyte.operator.v1alpha1.ybuniversespec.tservervolume.PerAZ();
    az2Spec.setVolumeSize(400L);
    az2Spec.setStorageClass("az2-tserver-new");
    perAZMap.put("az-2", az2Spec);
    tserverVolume.setPerAZ(perAZMap);
    ybUniverse.getSpec().setTserverVolume(tserverVolume);

    // The new taskParams cluster userIntent carries over the previously stored overrides;
    // placement now contains both az1 (retained) and az2 (newly added).
    UserIntent newUserIntent = new UserIntent();
    newUserIntent.universeName = universeName;
    newUserIntent.provider = defaultProvider.getUuid().toString();
    newUserIntent.deviceInfo = new DeviceInfo();
    newUserIntent.deviceInfo.volumeSize = 100;
    newUserIntent.deviceInfo.numVolumes = 1;
    newUserIntent.deviceInfo.storageClass = "base-tserver-sc";
    newUserIntent.masterDeviceInfo = new DeviceInfo();
    newUserIntent.masterDeviceInfo.volumeSize = 50;
    newUserIntent.masterDeviceInfo.numVolumes = 1;
    seedTserverAZOverride(newUserIntent, az1.getUuid(), 200, "az1-tserver-existing");
    AZOverrides az1Carried =
        newUserIntent.getUserIntentOverrides().getAzOverrides().get(az1.getUuid());
    PerProcessDetails carriedMasterPpd = new PerProcessDetails();
    DeviceInfo carriedMasterDi = new DeviceInfo();
    carriedMasterDi.storageClass = "az1-master-existing";
    carriedMasterPpd.setDeviceInfo(carriedMasterDi);
    az1Carried.getPerProcess().put(ServerType.MASTER, carriedMasterPpd);
    UniverseConfigureTaskParams taskParams = buildPrimaryClusterTaskParams(newUserIntent, az1, az2);

    ybUniverseReconciler.applyKubernetesOperatorVolumeOverrides(
        taskParams, ybUniverse, defaultCustomer.getUuid(), existingUniverse);

    UserIntent resultIntent = taskParams.getPrimaryCluster().userIntent;
    assertNotNull(resultIntent.getUserIntentOverrides());
    Map<UUID, AZOverrides> azOverrides = resultIntent.getUserIntentOverrides().getAzOverrides();
    assertNotNull(azOverrides);

    // Newly added az2: tserver perAZ value from the spec, master from the fallback path
    // (provider-level storage class) since master has no perAZ block.
    AZOverrides az2Ov = azOverrides.get(az2.getUuid());
    assertNotNull("Expected an azOverrides entry for newly added az2", az2Ov);
    assertNotNull(az2Ov.getPerProcess());
    assertNotNull(
        "Newly added az2 should pick up the tserver perAZ value from spec",
        az2Ov.getPerProcess().get(ServerType.TSERVER));
    DeviceInfo az2TserverDi = az2Ov.getPerProcess().get(ServerType.TSERVER).getDeviceInfo();
    assertEquals(400, az2TserverDi.volumeSize.intValue());
    assertEquals("az2-tserver-new", az2TserverDi.storageClass);

    assertNotNull(
        "Newly added az2 should pick up the master fallback override (no perAZ for master)",
        az2Ov.getPerProcess().get(ServerType.MASTER));
    DeviceInfo az2MasterDi = az2Ov.getPerProcess().get(ServerType.MASTER).getDeviceInfo();
    assertEquals(providerStorageClass, az2MasterDi.storageClass);

    // Retained az1 keeps its existing master override (skipAZs covers it for the master fallback)
    // and gets its tserver entry rewritten from the perAZ block in the spec.
    AZOverrides az1Ov = azOverrides.get(az1.getUuid());
    assertNotNull(az1Ov);
    assertNotNull(az1Ov.getPerProcess());
    assertNotNull(az1Ov.getPerProcess().get(ServerType.TSERVER));
    DeviceInfo az1TserverDi = az1Ov.getPerProcess().get(ServerType.TSERVER).getDeviceInfo();
    assertEquals(200, az1TserverDi.volumeSize.intValue());
    assertEquals("az1-tserver-existing", az1TserverDi.storageClass);
    assertNotNull(az1Ov.getPerProcess().get(ServerType.MASTER));
    DeviceInfo az1MasterDi = az1Ov.getPerProcess().get(ServerType.MASTER).getDeviceInfo();
    assertEquals("az1-master-existing", az1MasterDi.storageClass);
  }
}
