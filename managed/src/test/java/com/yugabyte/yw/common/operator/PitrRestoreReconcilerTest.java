// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.operator;

import static org.junit.Assert.assertEquals;
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
import com.yugabyte.yw.common.operator.utils.KubernetesClientFactory;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.common.operator.utils.UniverseImporter;
import com.yugabyte.yw.common.pitr.PitrConfigHelper;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.RestoreSnapshotScheduleParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.NonNamespaceOperation;
import io.fabric8.kubernetes.client.dsl.Resource;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.yugabyte.operator.v1alpha1.PitrRestore;
import io.yugabyte.operator.v1alpha1.PitrRestoreSpec;
import io.yugabyte.operator.v1alpha1.PitrRestoreStatus;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class PitrRestoreReconcilerTest extends FakeDBApplication {

  private PitrConfigHelper mockPitrConfigHelper;
  private OperatorUtils mockOperatorUtils;
  private KubernetesClient mockClient;
  private YBInformerFactory mockInformerFactory;
  private MixedOperation<PitrRestore, KubernetesResourceList<PitrRestore>, Resource<PitrRestore>>
      mockResourceClient;
  private NonNamespaceOperation<
          PitrRestore, KubernetesResourceList<PitrRestore>, Resource<PitrRestore>>
      mockInNamespaceResourceClient;
  private Resource<PitrRestore> mockPitrRestoreResource;
  private Customer testCustomer;
  private Universe testUniverse;
  private PitrRestoreReconciler pitrRestoreReconciler;
  private final String namespace = "test-namespace";

  @Before
  @SuppressWarnings("unchecked")
  public void setup() {
    mockPitrConfigHelper = Mockito.mock(PitrConfigHelper.class);
    mockOperatorUtils =
        spy(
            new OperatorUtils(
                Mockito.mock(RuntimeConfGetter.class),
                mockReleaseManager,
                mockYbcManager,
                Mockito.mock(ValidatingFormFactory.class),
                Mockito.mock(YBClientService.class),
                Mockito.mock(KubernetesClientFactory.class),
                Mockito.mock(UniverseImporter.class),
                Mockito.mock(KubernetesManagerFactory.class)));
    mockClient = Mockito.mock(KubernetesClient.class);
    mockInformerFactory = Mockito.mock(YBInformerFactory.class);
    mockResourceClient = Mockito.mock(MixedOperation.class);
    mockInNamespaceResourceClient = Mockito.mock(NonNamespaceOperation.class);
    mockPitrRestoreResource = Mockito.mock(Resource.class);

    // Mocking the informer
    when(mockInformerFactory.getSharedIndexInformer(eq(PitrRestore.class), any()))
        .thenReturn(Mockito.mock(SharedIndexInformer.class));

    // Fix: Ensure the fluent API chain returns the mock resource instead of null
    when(mockClient.resources(eq(PitrRestore.class))).thenReturn(mockResourceClient);
    when(mockResourceClient.inNamespace(anyString())).thenReturn(mockInNamespaceResourceClient);
    when(mockInNamespaceResourceClient.resource(any(PitrRestore.class)))
        .thenReturn(mockPitrRestoreResource);

    pitrRestoreReconciler =
        spy(
            new PitrRestoreReconciler(
                mockPitrConfigHelper,
                namespace,
                mockOperatorUtils,
                mockClient,
                mockInformerFactory));

    testCustomer = ModelFactory.testCustomer();
    testUniverse = ModelFactory.createUniverse("test-universe", testCustomer.getId());
  }

  private PitrRestore createPitrRestoreCr(String name) {
    PitrRestore pitrRestore = new PitrRestore();
    ObjectMeta metadata = new ObjectMeta();
    metadata.setName(name);
    metadata.setNamespace(namespace);
    pitrRestore.setMetadata(metadata);
    pitrRestore.setSpec(new PitrRestoreSpec());
    return pitrRestore;
  }

  @Test
  public void testCreateTriggersRestoreTask() throws Exception {
    PitrRestore pitrRestore = createPitrRestoreCr("test-restore");
    UUID taskUUID = UUID.randomUUID();
    RestoreSnapshotScheduleParams params = new RestoreSnapshotScheduleParams();
    params.setUniverseUUID(testUniverse.getUniverseUUID());

    doReturn(params).when(mockOperatorUtils).getRestoreSnapshotScheduleParamsFromCr(any());
    when(mockPitrConfigHelper.restorePitrConfig(any(), any(), any())).thenReturn(taskUUID);

    pitrRestoreReconciler.createActionReconcile(pitrRestore, testCustomer);

    verify(mockPitrConfigHelper, times(1))
        .restorePitrConfig(eq(testCustomer.getUuid()), eq(testUniverse.getUniverseUUID()), any());
    // This will now pass as resource() returns the mockPitrRestoreResource
    verify(mockPitrRestoreResource, times(1)).updateStatus();
    assertEquals(taskUUID.toString(), pitrRestore.getStatus().getTaskUUID());
  }

  @Test
  public void testCreateSkipsAlreadyInitialized() throws Exception {
    PitrRestore pitrRestore = createPitrRestoreCr("test-restore");
    PitrRestoreStatus status = new PitrRestoreStatus();
    status.setTaskUUID(UUID.randomUUID().toString());
    pitrRestore.setStatus(status);

    pitrRestoreReconciler.createActionReconcile(pitrRestore, testCustomer);

    verify(mockPitrConfigHelper, never()).restorePitrConfig(any(), any(), any());
  }

  @Test
  public void testCreateHandlesRestoreException() throws Exception {
    PitrRestore pitrRestore = createPitrRestoreCr("test-restore");
    doReturn(new RestoreSnapshotScheduleParams())
        .when(mockOperatorUtils)
        .getRestoreSnapshotScheduleParamsFromCr(any());
    when(mockPitrConfigHelper.restorePitrConfig(any(), any(), any()))
        .thenThrow(new RuntimeException("fail"));

    pitrRestoreReconciler.createActionReconcile(pitrRestore, testCustomer);

    verify(mockPitrRestoreResource, times(1)).updateStatus();
    assertEquals("Failed to process PITR restore: fail", pitrRestore.getStatus().getMessage());
  }
}
