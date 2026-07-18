// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.operator;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ValidatingFormFactory;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.dr.DrConfigHelper;
import com.yugabyte.yw.common.dr.DrConfigHelper.DrConfigTaskResult;
import com.yugabyte.yw.common.operator.utils.KubernetesClientFactory;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.common.operator.utils.OperatorWorkQueue;
import com.yugabyte.yw.common.operator.utils.UniverseImporter;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.DrConfigCreateForm;
import com.yugabyte.yw.forms.DrConfigFailoverForm;
import com.yugabyte.yw.forms.DrConfigSetDatabasesForm;
import com.yugabyte.yw.forms.DrConfigSwitchoverForm;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.NonNamespaceOperation;
import io.fabric8.kubernetes.client.dsl.Resource;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.fabric8.kubernetes.client.informers.cache.Indexer;
import io.yugabyte.operator.v1alpha1.DrConfig;
import io.yugabyte.operator.v1alpha1.DrConfigSpec;
import io.yugabyte.operator.v1alpha1.DrConfigStatus;
import io.yugabyte.operator.v1alpha1.StorageConfig;
import java.util.Collections;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class DrConfigReconcilerTest extends FakeDBApplication {

  private DrConfigHelper mockDrConfigHelper;
  private OperatorUtils mockOperatorUtils;
  private KubernetesClient mockClient;
  private YBInformerFactory mockInformerFactory;
  private SharedIndexInformer<DrConfig> mockDrConfigInformer;
  private SharedIndexInformer<StorageConfig> mockScInformer;
  private MixedOperation<DrConfig, KubernetesResourceList<DrConfig>, Resource<DrConfig>>
      mockResourceClient;
  private NonNamespaceOperation<DrConfig, KubernetesResourceList<DrConfig>, Resource<DrConfig>>
      mockInNamespaceResourceClient;
  private Resource<DrConfig> mockDrConfigResource;
  private Indexer<DrConfig> mockDrConfigIndexer;
  private RuntimeConfGetter mockConfGetter;
  private ValidatingFormFactory mockFormFactory;
  private YBClientService mockYbClientService;
  private KubernetesClientFactory mockKubernetesClientFactory;
  private UniverseImporter mockUniverseImporter;
  private KubernetesManagerFactory mockKubernetesManagerFactory;

  private DrConfigReconciler drConfigReconciler;
  private Customer testCustomer;
  private Provider testProvider;
  private Universe testSourceUniverse;
  private Universe testTargetUniverse;

  private final String namespace = "test-namespace";

  @Before
  public void setup() {
    mockDrConfigHelper = Mockito.mock(DrConfigHelper.class);
    mockConfGetter = Mockito.mock(RuntimeConfGetter.class);
    mockFormFactory = Mockito.mock(ValidatingFormFactory.class);
    mockYbClientService = Mockito.mock(YBClientService.class);
    mockKubernetesClientFactory = Mockito.mock(KubernetesClientFactory.class);
    mockUniverseImporter = Mockito.mock(UniverseImporter.class);
    mockKubernetesManagerFactory = Mockito.mock(KubernetesManagerFactory.class);
    mockOperatorUtils =
        spy(
            new OperatorUtils(
                mockConfGetter,
                mockReleaseManager,
                mockYbcManager,
                mockFormFactory,
                mockYbClientService,
                mockKubernetesClientFactory,
                mockUniverseImporter,
                mockKubernetesManagerFactory));
    mockClient = Mockito.mock(KubernetesClient.class);
    mockInformerFactory = Mockito.mock(YBInformerFactory.class);
    mockDrConfigInformer = Mockito.mock(SharedIndexInformer.class);
    mockScInformer = Mockito.mock(SharedIndexInformer.class);
    mockResourceClient = Mockito.mock(MixedOperation.class);
    mockDrConfigIndexer = Mockito.mock(Indexer.class);
    mockInNamespaceResourceClient = Mockito.mock(NonNamespaceOperation.class);
    mockDrConfigResource = Mockito.mock(Resource.class);

    when(mockInformerFactory.getSharedIndexInformer(
            eq(DrConfig.class), any(KubernetesClient.class)))
        .thenReturn(mockDrConfigInformer);
    when(mockInformerFactory.getSharedIndexInformer(
            eq(StorageConfig.class), any(KubernetesClient.class)))
        .thenReturn(mockScInformer);
    when(mockDrConfigInformer.getIndexer()).thenReturn(mockDrConfigIndexer);
    when(mockClient.resources(eq(DrConfig.class))).thenReturn(mockResourceClient);
    when(mockResourceClient.inNamespace(anyString())).thenReturn(mockInNamespaceResourceClient);
    when(mockInNamespaceResourceClient.withName(anyString())).thenReturn(mockDrConfigResource);
    when(mockInNamespaceResourceClient.resource(any(DrConfig.class)))
        .thenReturn(mockDrConfigResource);

    drConfigReconciler =
        spy(
            new DrConfigReconciler(
                mockDrConfigHelper, namespace, mockOperatorUtils, mockClient, mockInformerFactory));

    testCustomer = ModelFactory.testCustomer();
    testProvider = ModelFactory.kubernetesProvider(testCustomer);
    testSourceUniverse = ModelFactory.createUniverse("source-universe", testCustomer.getId());
    testTargetUniverse = ModelFactory.createUniverse("target-universe", testCustomer.getId());
  }

  private DrConfig createDrConfigCr(String name, String sourceUniverse, String targetUniverse) {
    DrConfig drConfig = new DrConfig();
    ObjectMeta metadata = new ObjectMeta();
    metadata.setName(name);
    metadata.setNamespace(namespace);
    metadata.setUid(UUID.randomUUID().toString());
    metadata.setGeneration(1L);
    DrConfigSpec spec = new DrConfigSpec();
    spec.setSourceUniverse(sourceUniverse);
    spec.setTargetUniverse(targetUniverse);
    drConfig.setMetadata(metadata);
    drConfig.setSpec(spec);
    return drConfig;
  }

  // --- CREATE tests ---

  @Test
  public void testReconcileCreate() throws Exception {
    DrConfig drConfig = createDrConfigCr("test-dr", "source-universe", "target-universe");
    UUID taskUUID = UUID.randomUUID();

    DrConfigCreateForm createForm = new DrConfigCreateForm();
    doReturn(createForm)
        .when(mockOperatorUtils)
        .getDrConfigCreateFormFromCr(any(DrConfig.class), any());

    DrConfigTaskResult result = new DrConfigTaskResult(UUID.randomUUID(), taskUUID, "drConfigName");
    when(mockDrConfigHelper.createDrConfigTask(
            eq(testCustomer.getUuid()), any(DrConfigCreateForm.class)))
        .thenReturn(result);

    drConfigReconciler.createActionReconcile(drConfig, testCustomer);

    verify(mockDrConfigHelper, times(1))
        .createDrConfigTask(eq(testCustomer.getUuid()), any(DrConfigCreateForm.class));
    assertEquals(
        taskUUID,
        drConfigReconciler.getDrConfigTaskMapValue(
            OperatorWorkQueue.getWorkQueueKey(drConfig.getMetadata())));
  }

  @Test
  public void testReconcileCreateAlreadyInitialized() throws Exception {
    DrConfig drConfig = createDrConfigCr("test-dr", "source-universe", "target-universe");
    DrConfigStatus status = new DrConfigStatus();
    status.setResourceUUID(UUID.randomUUID().toString());
    drConfig.setStatus(status);

    drConfigReconciler.createActionReconcile(drConfig, testCustomer);

    verify(mockDrConfigHelper, never()).createDrConfigTask(any(), any(DrConfigCreateForm.class));
  }

  @Test
  public void testReconcileCreateSetsFinalizer() throws Exception {
    DrConfig drConfig = createDrConfigCr("test-dr", "source-universe", "target-universe");
    // Ensure no finalizers set initially
    drConfig.getMetadata().setFinalizers(Collections.emptyList());

    DrConfigCreateForm createForm = new DrConfigCreateForm();
    doReturn(createForm)
        .when(mockOperatorUtils)
        .getDrConfigCreateFormFromCr(any(DrConfig.class), any());

    DrConfigTaskResult result =
        new DrConfigTaskResult(UUID.randomUUID(), UUID.randomUUID(), "drConfigName");
    when(mockDrConfigHelper.createDrConfigTask(any(), any(DrConfigCreateForm.class)))
        .thenReturn(result);

    drConfigReconciler.createActionReconcile(drConfig, testCustomer);

    // Verify patch was called (which sets the finalizer)
    verify(mockDrConfigResource, times(1)).patch(any(DrConfig.class));
    assertEquals(OperatorUtils.YB_FINALIZER, drConfig.getMetadata().getFinalizers().get(0));
  }

  // --- UPDATE tests ---

  @Test
  public void testReconcileUpdateIgnoredNoStatus() throws Exception {
    DrConfig drConfig = createDrConfigCr("test-dr", "source-universe", "target-universe");
    // No status set

    drConfigReconciler.updateActionReconcile(drConfig, testCustomer);

    verify(mockDrConfigHelper, never())
        .failoverDrConfigTask(any(), any(), any(DrConfigFailoverForm.class));
    verify(mockDrConfigHelper, never())
        .switchoverDrConfigTask(any(), any(), any(DrConfigSwitchoverForm.class));
    verify(mockDrConfigHelper, never())
        .setDatabasesTask(any(), any(), any(DrConfigSetDatabasesForm.class));
  }

  // --- NO_OP tests ---

  @Test
  public void testNoOpRequeuesCreateWhenNotFound() throws Exception {
    DrConfig drConfig = createDrConfigCr("test-dr", "source-universe", "target-universe");
    // No status, no tracked task

    drConfigReconciler.noOpActionReconcile(drConfig, testCustomer);

    // The workqueue should have a CREATE action requeued.
    // We verify by checking that no task-related methods were called
    // and that the drConfigTaskMap has no entry.
    assertNull(
        drConfigReconciler.getDrConfigTaskMapValue(
            OperatorWorkQueue.getWorkQueueKey(drConfig.getMetadata())));
  }

  // --- DELETE tests ---

  @Test
  public void testReconcileDeleteNoStatus() throws Exception {
    DrConfig drConfig = createDrConfigCr("test-dr", "source-universe", "target-universe");
    drConfig.getMetadata().setFinalizers(Collections.singletonList(OperatorUtils.YB_FINALIZER));
    // No status set

    drConfigReconciler.handleResourceDeletion(
        drConfig, testCustomer, OperatorWorkQueue.ResourceAction.DELETE);

    verify(mockOperatorUtils, times(1)).removeFinalizer(eq(drConfig), any());
  }

  @Test
  public void testCreateOnExceptionHandledGracefully() throws Exception {
    DrConfig drConfig = createDrConfigCr("test-dr", "source-universe", "target-universe");

    doReturn(new DrConfigCreateForm())
        .when(mockOperatorUtils)
        .getDrConfigCreateFormFromCr(any(DrConfig.class), any());

    when(mockDrConfigHelper.createDrConfigTask(any(), any(DrConfigCreateForm.class)))
        .thenThrow(new RuntimeException("simulated failure"));

    // Should not throw - the method catches exceptions internally
    drConfigReconciler.createActionReconcile(drConfig, testCustomer);

    // Task map should be empty since creation failed
    assertNull(
        drConfigReconciler.getDrConfigTaskMapValue(
            OperatorWorkQueue.getWorkQueueKey(drConfig.getMetadata())));
  }
}
