package com.yugabyte.yw.common.operator;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.operator.utils.KubernetesEnvironmentVariables;
import com.yugabyte.yw.common.operator.utils.OperatorWorkQueue;
import com.yugabyte.yw.controllers.handlers.CloudProviderHandler;
import com.yugabyte.yw.controllers.handlers.UniverseCRUDHandler;
import com.yugabyte.yw.forms.KubernetesProviderFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseResp;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.TaskType;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.NonNamespaceOperation;
import io.fabric8.kubernetes.client.dsl.Resource;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.fabric8.kubernetes.client.informers.cache.Indexer;
import io.yugabyte.operator.v1alpha1.YBUniverse;
import io.yugabyte.operator.v1alpha1.YBUniverseSpec;
import io.yugabyte.operator.v1alpha1.YBUniverseStatus;
import io.yugabyte.operator.v1alpha1.ybuniversespec.DeviceInfo;
import io.yugabyte.operator.v1alpha1.ybuniversespec.MasterK8SNodeResourceSpec;
import io.yugabyte.operator.v1alpha1.ybuniversespec.TserverK8SNodeResourceSpec;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.MockedStatic;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class YBUniverseReconcilerTest extends FakeDBApplication {
  @Mock KubernetesClient client;

  @Mock
  MixedOperation<YBUniverse, KubernetesResourceList<YBUniverse>, Resource<YBUniverse>>
      ybUniverseClient;

  @Mock
  NonNamespaceOperation<YBUniverse, KubernetesResourceList<YBUniverse>, Resource<YBUniverse>>
      inNamespaceYBUClient;

  @Mock Resource<YBUniverse> ybUniverseResource;
  @Mock RuntimeConfGetter confGetter;
  @Mock YBInformerFactory informerFactory;
  @Mock SharedIndexInformer<YBUniverse> ybUniverseInformer;
  @Mock Indexer<YBUniverse> indexer;
  @Mock UniverseCRUDHandler universeCRUDHandler;
  @Mock CloudProviderHandler cloudProviderHandler;
  @Mock KubernetesOperatorStatusUpdater statusUpdater;

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
    Mockito.when(confGetter.getGlobalConf(any())).thenReturn(true);
    ybUniverseReconciler =
        new YBUniverseReconciler(
            client,
            informerFactory,
            universeName,
            universeCRUDHandler,
            null,
            cloudProviderHandler,
            null,
            statusUpdater,
            confGetter);
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
    universe.setStatus(null);
    Mockito.when(universeCRUDHandler.findByName(defaultCustomer, universeName)).thenReturn(null);
    ybUniverseReconciler.reconcile(universe, OperatorWorkQueue.ResourceAction.DELETE);
    // Not sure what to check here, most items are null and we just return.
  }

  // TODO: Need more delete tests, but that code should be refactored a bit.

  @Test
  public void testReconcileCreate() {
    UniverseResp uResp = new UniverseResp(defaultUniverse, UUID.randomUUID());
    Mockito.when(
            universeCRUDHandler.createUniverse(
                Mockito.eq(defaultCustomer), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(uResp);
    // Empty response to show the universe isn't yet created
    List<UniverseResp> findResp = new ArrayList<>();
    Mockito.when(universeCRUDHandler.findByName(defaultCustomer, universeName))
        .thenReturn(findResp);
    YBUniverse universe = createYbUniverse();
    ybUniverseReconciler.reconcile(universe, OperatorWorkQueue.ResourceAction.CREATE);

    Mockito.verify(statusUpdater, Mockito.times(1))
        .createYBUniverseEventStatus(null, null, TaskType.CreateKubernetesUniverse.name());
    Mockito.verify(universeCRUDHandler, Mockito.times(1)).findByName(defaultCustomer, universeName);
    Mockito.verify(universeCRUDHandler, Mockito.times(1))
        .createUniverse(Mockito.eq(defaultCustomer), any(UniverseDefinitionTaskParams.class));
    Mockito.verify(ybUniverseResource, Mockito.atLeast(1)).patch(any(YBUniverse.class));
  }

  @Test
  public void testReconcileCreateAlreadyExists() {
    UniverseResp uResp = new UniverseResp(defaultUniverse, UUID.randomUUID());
    // Empty response to show the universe isn't yet created
    List<UniverseResp> findResp = new ArrayList<>();
    findResp.add(uResp);
    Mockito.when(universeCRUDHandler.findByName(defaultCustomer, universeName))
        .thenReturn(findResp);
    YBUniverse universe = createYbUniverse();
    ybUniverseReconciler.reconcile(universe, OperatorWorkQueue.ResourceAction.CREATE);
    Mockito.verify(universeCRUDHandler, Mockito.times(1)).findByName(defaultCustomer, universeName);
    Mockito.verify(universeCRUDHandler, Mockito.never())
        .createUniverse(Mockito.eq(defaultCustomer), any(UniverseDefinitionTaskParams.class));
  }

  @Test
  public void testReconcileCreateAutoProvider() {
    UniverseResp uResp = new UniverseResp(defaultUniverse, UUID.randomUUID());
    Mockito.when(
            universeCRUDHandler.createUniverse(
                Mockito.eq(defaultCustomer), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(uResp);
    // Empty response to show the universe isn't yet created
    List<UniverseResp> findResp = new ArrayList<>();
    Mockito.when(universeCRUDHandler.findByName(defaultCustomer, universeName))
        .thenReturn(findResp);
    KubernetesProviderFormData providerData = new KubernetesProviderFormData();
    Mockito.when(cloudProviderHandler.suggestedKubernetesConfigs()).thenReturn(providerData);
    // Create a provider with the name following `YBUniverseReconciler.getProviderName` format
    Mockito.when(cloudProviderHandler.createKubernetes(defaultCustomer, providerData))
        .thenAnswer(
            invocation -> {
              return ModelFactory.kubernetesProvider(defaultCustomer, "prov-" + universeName);
            });

    YBUniverse universe = createYbUniverse();
    // Update universe to have no provider
    YBUniverseSpec spec = universe.getSpec();
    spec.setProviderName("");
    universe.setSpec(spec);
    ybUniverseReconciler.reconcile(universe, OperatorWorkQueue.ResourceAction.CREATE);

    Mockito.verify(universeCRUDHandler, Mockito.times(1)).findByName(defaultCustomer, universeName);
    Mockito.verify(cloudProviderHandler, Mockito.times(1)).suggestedKubernetesConfigs();
    Mockito.verify(cloudProviderHandler, Mockito.times(1))
        .createKubernetes(defaultCustomer, providerData);
    Mockito.verify(universeCRUDHandler, Mockito.times(1))
        .createUniverse(Mockito.eq(defaultCustomer), any(UniverseDefinitionTaskParams.class));
    Mockito.verify(ybUniverseResource, Mockito.atLeast(1)).patch(any(YBUniverse.class));
  }

  private YBUniverse createYbUniverse() {
    YBUniverse universe = new YBUniverse();
    ObjectMeta metadata = new ObjectMeta();
    metadata.setName(universeName);
    metadata.setNamespace(namespace);
    metadata.setGeneration((long) 123);
    YBUniverseStatus status = new YBUniverseStatus();
    YBUniverseSpec spec = new YBUniverseSpec();
    List<String> zones = new ArrayList<>();
    zones.add("one");
    zones.add("two");
    spec.setZoneFilter(zones);
    spec.setReplicationFactor((long) 1);
    spec.setAssignPublicIP(true);
    spec.setUseTimeSync(true);
    spec.setEnableYSQL(true);
    spec.setEnableYEDIS(false);
    spec.setEnableYCQL(false);
    spec.setEnableNodeToNodeEncrypt(false);
    spec.setEnableClientToNodeEncrypt(false);
    spec.setYsqlPassword(null);
    spec.setYcqlPassword(null);
    spec.setProviderName(defaultProvider.getName());
    spec.setMasterK8SNodeResourceSpec(new MasterK8SNodeResourceSpec());
    spec.setTserverK8SNodeResourceSpec(new TserverK8SNodeResourceSpec());
    DeviceInfo deviceInfo = new DeviceInfo();
    spec.setDeviceInfo(deviceInfo);

    universe.setMetadata(metadata);
    universe.setStatus(status);
    universe.setSpec(spec);
    return universe;
  }
}
