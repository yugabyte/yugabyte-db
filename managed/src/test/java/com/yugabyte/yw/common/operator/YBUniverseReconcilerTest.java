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
import com.yugabyte.yw.forms.UniverseConfigureTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.AZOverrides;
import com.yugabyte.yw.forms.UniverseResp;
import com.yugabyte.yw.forms.YbcThrottleParametersResponse;
import com.yugabyte.yw.models.AvailabilityZone;
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

    tserverVolume.setPerAZ(perAZMap);
    ybUniverse.getSpec().setTserverVolume(tserverVolume);

    // Create existing universe
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

    // Remove old deviceInfo and set tserverVolume WITHOUT perAZ
    ybUniverse.getSpec().setDeviceInfo(null);
    io.yugabyte.operator.v1alpha1.ybuniversespec.TserverVolume tserverVolume =
        new io.yugabyte.operator.v1alpha1.ybuniversespec.TserverVolume();
    tserverVolume.setVolumeSize(100L);
    tserverVolume.setNumVolumes(3L);
    tserverVolume.setStorageClass("base-storage");
    // perAZ is null - should not apply AZ overrides
    tserverVolume.setPerAZ(null);
    ybUniverse.getSpec().setTserverVolume(tserverVolume);

    // Create availability zones first
    Region region = Region.create(defaultProvider, "region-1", "Region 1", "default-image");
    AvailabilityZone az = AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");

    // Create existing universe
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
}
