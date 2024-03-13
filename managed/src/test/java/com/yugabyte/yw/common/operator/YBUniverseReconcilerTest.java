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
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.operator.OperatorStatusUpdater.UniverseState;
import com.yugabyte.yw.common.operator.utils.KubernetesEnvironmentVariables;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.common.operator.utils.OperatorWorkQueue;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.controllers.handlers.CloudProviderHandler;
import com.yugabyte.yw.controllers.handlers.UniverseCRUDHandler;
import com.yugabyte.yw.controllers.handlers.UpgradeUniverseHandler;
import com.yugabyte.yw.forms.KubernetesProviderFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseResp;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.TaskInfo.State;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.TaskType;
import io.fabric8.kubernetes.api.model.IntOrString;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.NonNamespaceOperation;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.fabric8.kubernetes.client.informers.cache.Indexer;
import io.yugabyte.operator.v1alpha1.YBUniverse;
import io.yugabyte.operator.v1alpha1.YBUniverseSpec;
import io.yugabyte.operator.v1alpha1.YBUniverseStatus;
import io.yugabyte.operator.v1alpha1.ybuniversespec.DeviceInfo;
import io.yugabyte.operator.v1alpha1.ybuniversespec.KubernetesOverrides;
import io.yugabyte.operator.v1alpha1.ybuniversespec.kubernetesoverrides.Resource;
import io.yugabyte.operator.v1alpha1.ybuniversespec.kubernetesoverrides.resource.Master;
import io.yugabyte.operator.v1alpha1.ybuniversespec.kubernetesoverrides.resource.master.Limits;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import javax.annotation.Nullable;
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

  MockedStatic<KubernetesEnvironmentVariables> envVars;

  YBUniverseReconciler ybUniverseReconciler;
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
    envVars = Mockito.mockStatic(KubernetesEnvironmentVariables.class);
    envVars.when(KubernetesEnvironmentVariables::getServiceHost).thenReturn("host");
    envVars.when(KubernetesEnvironmentVariables::getServicePort).thenReturn("1234");
    OperatorUtils operatorUtils = new OperatorUtils(confGetterForOperatorUtils);
    Mockito.when(confGetter.getGlobalConf(any())).thenReturn(true);
    Mockito.when(
            confGetterForOperatorUtils.getGlobalConf(GlobalConfKeys.KubernetesOperatorCustomerUUID))
        .thenReturn("");
    ybUniverseReconciler =
        new YBUniverseReconciler(
            client,
            informerFactory,
            namespace,
            new OperatorWorkQueue(1, 5, 10),
            universeCRUDHandler,
            upgradeUniverseHandler,
            cloudProviderHandler,
            null,
            kubernetesStatusUpdator,
            confGetter,
            customerTaskManager,
            operatorUtils);
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
    YBUniverse universe = createYbUniverse();
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
    YBUniverse universe = createYbUniverse(universeName);
    ybUniverseReconciler.reconcile(universe, OperatorWorkQueue.ResourceAction.CREATE);

    Mockito.verify(kubernetesStatusUpdator, Mockito.times(1))
        .createYBUniverseEventStatus(
            isNull(),
            any(KubernetesResourceDetails.class),
            eq(TaskType.CreateKubernetesUniverse.name()));
    Mockito.verify(universeCRUDHandler, Mockito.times(1))
        .createUniverse(Mockito.eq(defaultCustomer), any(UniverseDefinitionTaskParams.class));
    Mockito.verify(ybUniverseResource, Mockito.atLeast(1)).patch(any(YBUniverse.class));
  }

  @Test
  public void testReconcileCreateAlreadyExists() throws Exception {
    // We don't find any customer task entry for create universe.
    String universeName = "test-universe-already-exists";
    YBUniverse ybUniverse = createYbUniverse(universeName);
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
    YBUniverse universe = createYbUniverse(universeName);
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
    Mockito.when(cloudProviderHandler.suggestedKubernetesConfigs()).thenReturn(providerData);
    // Create a provider with the name following `YBUniverseReconciler.getProviderName` format
    Mockito.when(cloudProviderHandler.createKubernetes(defaultCustomer, providerData))
        .thenAnswer(
            invocation -> {
              return ModelFactory.kubernetesProvider(
                  defaultCustomer, "prov-" + autoProviderNameSuffix);
            });
    universe.getSpec().setProviderName("");
    ybUniverseReconciler.reconcile(universe, OperatorWorkQueue.ResourceAction.CREATE);

    Mockito.verify(cloudProviderHandler, Mockito.times(1)).suggestedKubernetesConfigs();
    Mockito.verify(cloudProviderHandler, Mockito.times(1))
        .createKubernetes(defaultCustomer, providerData);
    Mockito.verify(universeCRUDHandler, Mockito.times(1))
        .createUniverse(Mockito.eq(defaultCustomer), any(UniverseDefinitionTaskParams.class));
    Mockito.verify(ybUniverseResource, Mockito.atLeast(1)).patch(any(YBUniverse.class));
  }

  @Test
  public void testCreateTaskRetryOnPlacementModificationTaskSet() throws Exception {
    String universeName = "test-retry-universe";
    YBUniverse ybUniverseOriginal = createYbUniverse(universeName);
    UniverseDefinitionTaskParams taskParams =
        ybUniverseReconciler.createTaskParams(ybUniverseOriginal, defaultCustomer.getUuid());
    Universe universe = Universe.create(taskParams, defaultCustomer.getId());
    ModelFactory.addNodesToUniverse(universe.getUniverseUUID(), 1);
    TaskInfo taskInfo = new TaskInfo(TaskType.CreateKubernetesUniverse, null);
    taskInfo.setDetails(Json.toJson(taskParams));
    taskInfo.setOwner("localhost");
    taskInfo.save();
    taskInfo.refresh();

    UniverseDefinitionTaskParams uTaskParams = universe.getUniverseDetails();
    uTaskParams.placementModificationTaskUuid = taskInfo.getTaskUUID();
    universe.setUniverseDetails(uTaskParams);
    universe.save();
    ybUniverseReconciler.reconcile(ybUniverseOriginal, OperatorWorkQueue.ResourceAction.CREATE);

    Mockito.verify(customerTaskManager, Mockito.times(1))
        .retryCustomerTask(
            Mockito.eq(defaultCustomer.getUuid()), Mockito.eq(taskInfo.getTaskUUID()));
  }

  @Test
  public void testEditTaskRetryOnPlacementModificationTaskSet() throws Exception {
    String universeName = "test-retry-universe";
    YBUniverse ybUniverseOriginal = createYbUniverse(universeName);
    UniverseDefinitionTaskParams taskParams =
        ybUniverseReconciler.createTaskParams(ybUniverseOriginal, defaultCustomer.getUuid());
    Universe universe = Universe.create(taskParams, defaultCustomer.getId());
    ModelFactory.addNodesToUniverse(universe.getUniverseUUID(), 1);
    TaskInfo taskInfo = new TaskInfo(TaskType.SoftwareKubernetesUpgrade, null);
    taskInfo.setDetails(Json.toJson(taskParams));
    taskInfo.setOwner("localhost");
    taskInfo.save();
    taskInfo.refresh();

    UniverseDefinitionTaskParams uTaskParams = universe.getUniverseDetails();
    uTaskParams.placementModificationTaskUuid = taskInfo.getTaskUUID();
    universe.setUniverseDetails(uTaskParams);
    universe.save();
    ybUniverseReconciler.reconcile(ybUniverseOriginal, OperatorWorkQueue.ResourceAction.UPDATE);

    Mockito.verify(customerTaskManager, Mockito.times(1))
        .retryCustomerTask(
            Mockito.eq(defaultCustomer.getUuid()), Mockito.eq(taskInfo.getTaskUUID()));
  }

  @Test
  public void testRequeueOnUniverseLocked() throws Exception {
    String universeName = "test-locked-universe";
    YBUniverse ybUniverseOriginal = createYbUniverse(universeName);
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
    YBUniverse ybUniverseOriginal = createYbUniverse(universeName);
    UniverseDefinitionTaskParams taskParams =
        ybUniverseReconciler.createTaskParams(ybUniverseOriginal, defaultCustomer.getUuid());
    Universe oldUniverse = Universe.create(taskParams, defaultCustomer.getId());
    ModelFactory.addNodesToUniverse(oldUniverse.getUniverseUUID(), 1);
    TaskInfo taskInfo = new TaskInfo(TaskType.CreateKubernetesUniverse, null);
    taskInfo.setDetails(Json.toJson(taskParams));
    taskInfo.setTaskState(State.Failure);
    taskInfo.setOwner("localhost");
    taskInfo.save();
    taskInfo.refresh();
    CustomerTask.create(
        defaultCustomer,
        oldUniverse.getUniverseUUID(),
        taskInfo.getTaskUUID(),
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
    YBUniverse ybUniverse = createYbUniverse(universeName);
    ybUniverse.getSpec().setProviderName("");
    KubernetesProviderFormData providerData = new KubernetesProviderFormData();
    Mockito.when(cloudProviderHandler.suggestedKubernetesConfigs()).thenReturn(providerData);
    Mockito.when(
            cloudProviderHandler.createKubernetes(
                any(Customer.class), any(KubernetesProviderFormData.class)))
        .thenThrow(new RuntimeException());
    try {
      UniverseDefinitionTaskParams taskParams =
          ybUniverseReconciler.createTaskParams(ybUniverse, defaultCustomer.getUuid());
    } catch (Exception e) {
    }
    Mockito.verify(kubernetesStatusUpdator, Mockito.times(1))
        .updateUniverseState(
            any(KubernetesResourceDetails.class), eq(UniverseState.ERROR_CREATING));
  }

  @Test
  public void testMultipleSpecUpdatePickEditFirst() throws Exception {
    String universeName = "test-multiple-spec-updates";
    YBUniverse ybUniverse = createYbUniverse(universeName);
    UniverseDefinitionTaskParams taskParams =
        ybUniverseReconciler.createTaskParams(ybUniverse, defaultCustomer.getUuid());
    Universe oldUniverse = Universe.create(taskParams, defaultCustomer.getId());

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
    ArgumentCaptor<UniverseDefinitionTaskParams> uDTCaptor =
        ArgumentCaptor.forClass(UniverseDefinitionTaskParams.class);
    Mockito.verify(universeCRUDHandler, Mockito.times(1))
        .update(any(Customer.class), any(Universe.class), uDTCaptor.capture());
    assertTrue(uDTCaptor.getValue().getPrimaryCluster().userIntent.deviceInfo.volumeSize == 20L);
    // Verify upgrade handler is not called
    Mockito.verifyNoInteractions(upgradeUniverseHandler);
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

    String overridesString = ybUniverseReconciler.getKubernetesOverridesString(overrides);
    assertEquals(expectedString, overridesString);
    assertTrue(overridesString.length() > 0);
  }

  @Test
  public void testParseKubernetesOverridesWithAdditionalProperty() {
    KubernetesOverrides overrides = createKubernetesOverrides();

    Map<String, Object> additionalPropertiesMap = new HashMap<>();
    additionalPropertiesMap.put("foo", "bar");
    overrides.setAdditionalProperties(additionalPropertiesMap);

    String overridesString = ybUniverseReconciler.getKubernetesOverridesString(overrides);
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
    limit.setCpu(new IntOrString(new Integer(4)));
    Master masterResource = new Master();
    masterResource.setLimits(limit);
    resource.setMaster(masterResource);
    overrides.setResource(resource);

    return overrides;
  }

  private YBUniverse createYbUniverse() {
    return createYbUniverse(null);
  }

  private YBUniverse createYbUniverse(@Nullable String univName) {
    YBUniverse universe = new YBUniverse();
    ObjectMeta metadata = new ObjectMeta();
    metadata.setName(univName == null ? universeName : univName);
    metadata.setNamespace(namespace);
    metadata.setUid(UUID.randomUUID().toString());
    metadata.setGeneration((long) 123);
    YBUniverseStatus status = new YBUniverseStatus();
    YBUniverseSpec spec = new YBUniverseSpec();
    List<String> zones = new ArrayList<>();
    zones.add("one");
    zones.add("two");
    spec.setYbSoftwareVersion("2.21.0.0-b1");
    spec.setNumNodes(1L);
    spec.setZoneFilter(zones);
    spec.setReplicationFactor((long) 1);
    spec.setEnableYSQL(true);
    spec.setEnableYCQL(false);
    spec.setEnableNodeToNodeEncrypt(false);
    spec.setEnableClientToNodeEncrypt(false);
    spec.setYsqlPassword(null);
    spec.setYcqlPassword(null);
    spec.setProviderName(defaultProvider.getName());
    DeviceInfo deviceInfo = new DeviceInfo();
    deviceInfo.setVolumeSize(10L);
    spec.setDeviceInfo(deviceInfo);

    universe.setMetadata(metadata);
    universe.setStatus(status);
    universe.setSpec(spec);
    return universe;
  }
}
