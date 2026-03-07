// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.operator;

import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.lenient;
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
import com.yugabyte.yw.common.operator.utils.OperatorWorkQueue;
import com.yugabyte.yw.common.operator.utils.UniverseImporter;
import com.yugabyte.yw.common.pitr.PitrConfigHelper;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.CreatePitrConfigParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.api.model.OwnerReference;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.NonNamespaceOperation;
import io.fabric8.kubernetes.client.dsl.Resource;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.yugabyte.operator.v1alpha1.PitrConfig;
import io.yugabyte.operator.v1alpha1.PitrConfigSpec;
import java.util.Collections;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class PitrConfigReconcilerTest extends FakeDBApplication {

  private PitrConfigHelper mockPitrConfigHelper;
  private ValidatingFormFactory mockFormFactory;
  private OperatorUtils mockOperatorUtils;
  private KubernetesClient mockClient;
  private YBInformerFactory mockInformerFactory;
  private MixedOperation<PitrConfig, KubernetesResourceList<PitrConfig>, Resource<PitrConfig>>
      mockResourceClient;
  private NonNamespaceOperation<
          PitrConfig, KubernetesResourceList<PitrConfig>, Resource<PitrConfig>>
      mockInNamespaceResourceClient;
  private Resource<PitrConfig> mockPitrConfigResource;
  private Customer testCustomer;
  private Universe testUniverse;
  private PitrConfigReconciler pitrConfigReconciler;
  private final String namespace = "test-namespace";

  @Before
  @SuppressWarnings("unchecked")
  public void setup() {
    mockPitrConfigHelper = Mockito.mock(PitrConfigHelper.class);
    mockFormFactory = Mockito.mock(ValidatingFormFactory.class);
    mockClient = Mockito.mock(KubernetesClient.class);

    mockOperatorUtils =
        spy(
            new OperatorUtils(
                Mockito.mock(RuntimeConfGetter.class),
                mockReleaseManager,
                mockYbcManager,
                mockFormFactory,
                Mockito.mock(YBClientService.class),
                Mockito.mock(KubernetesClientFactory.class),
                Mockito.mock(UniverseImporter.class),
                Mockito.mock(KubernetesManagerFactory.class)));

    mockInformerFactory = Mockito.mock(YBInformerFactory.class);
    mockResourceClient = Mockito.mock(MixedOperation.class);
    mockInNamespaceResourceClient = Mockito.mock(NonNamespaceOperation.class);
    mockPitrConfigResource = Mockito.mock(Resource.class);

    // Using lenient() for shared setup mocks to avoid UnnecessaryStubbingException
    lenient()
        .when(mockInformerFactory.getSharedIndexInformer(eq(PitrConfig.class), any()))
        .thenReturn(Mockito.mock(SharedIndexInformer.class));

    lenient().when(mockClient.resources(eq(PitrConfig.class))).thenReturn(mockResourceClient);
    lenient()
        .when(mockResourceClient.inNamespace(anyString()))
        .thenReturn(mockInNamespaceResourceClient);
    lenient()
        .when(mockInNamespaceResourceClient.withName(anyString()))
        .thenReturn(mockPitrConfigResource);
    lenient()
        .when(mockInNamespaceResourceClient.resource(any(PitrConfig.class)))
        .thenReturn(mockPitrConfigResource);

    try {
      // Stubbing the owner reference call as lenient because the LockedUniverse test skips this
      // path
      lenient()
          .doReturn(new OwnerReference())
          .when(mockOperatorUtils)
          .getResourceOwnerReference(any(), any());
    } catch (Exception e) {
    }

    pitrConfigReconciler =
        spy(
            new PitrConfigReconciler(
                mockPitrConfigHelper,
                mockFormFactory,
                namespace,
                mockOperatorUtils,
                mockClient,
                mockInformerFactory));

    testCustomer = ModelFactory.testCustomer();
    testUniverse = ModelFactory.createUniverse("test-universe", testCustomer.getId());
  }

  private PitrConfig createPitrConfigCr(String name) {
    PitrConfig pitrConfig = new PitrConfig();
    ObjectMeta metadata = new ObjectMeta();
    metadata.setName(name);
    metadata.setNamespace(namespace);
    metadata.setUid(UUID.randomUUID().toString());
    pitrConfig.setMetadata(metadata);
    PitrConfigSpec spec = new PitrConfigSpec();
    spec.setUniverse("test-universe");
    spec.setTableType(PitrConfigSpec.TableType.YSQL);
    pitrConfig.setSpec(spec);
    return pitrConfig;
  }

  @Test
  public void testCreateNewPitrConfig() throws Exception {
    PitrConfig pitrConfig = createPitrConfigCr("test-pitr");
    UUID taskUUID = UUID.randomUUID();

    doReturn(new CreatePitrConfigParams())
        .when(mockOperatorUtils)
        .getCreatePitrConfigParamsFromCr(any());
    doReturn(testUniverse)
        .when(mockOperatorUtils)
        .getUniverseFromNameAndNamespace(eq(testCustomer.getId()), anyString(), anyString());

    when(mockPitrConfigHelper.createPitrConfig(any(), any(), any(), any(), any()))
        .thenReturn(taskUUID);

    pitrConfigReconciler.createActionReconcile(pitrConfig, testCustomer);

    verify(mockPitrConfigHelper, times(1)).createPitrConfig(any(), any(), any(), any(), any());
    assertEquals(
        taskUUID,
        pitrConfigReconciler.getPitrConfigTaskMapValue(
            OperatorWorkQueue.getWorkQueueKey(pitrConfig.getMetadata())));
  }

  @Test
  public void testCreateSetsFinalizer() throws Exception {
    PitrConfig pitrConfig = createPitrConfigCr("test-pitr");
    pitrConfig.getMetadata().setFinalizers(Collections.emptyList());

    doReturn(new CreatePitrConfigParams())
        .when(mockOperatorUtils)
        .getCreatePitrConfigParamsFromCr(any());
    doReturn(testUniverse)
        .when(mockOperatorUtils)
        .getUniverseFromNameAndNamespace(eq(testCustomer.getId()), anyString(), anyString());

    pitrConfigReconciler.createActionReconcile(pitrConfig, testCustomer);

    verify(mockPitrConfigResource, times(1)).patch(any(PitrConfig.class));
    assertEquals(OperatorUtils.YB_FINALIZER, pitrConfig.getMetadata().getFinalizers().get(0));
  }

  @Test
  public void testCreateRequeuesWhenUniverseLocked() throws Exception {
    PitrConfig pitrConfig = createPitrConfigCr("test-pitr");

    Universe lockedUniverse = ModelFactory.createUniverse("locked-universe", testCustomer.getId());
    Universe.saveDetails(
        lockedUniverse.getUniverseUUID(),
        u -> {
          u.getUniverseDetails().updateInProgress = true;
        });
    lockedUniverse = Universe.getOrBadRequest(lockedUniverse.getUniverseUUID());

    doReturn(new CreatePitrConfigParams())
        .when(mockOperatorUtils)
        .getCreatePitrConfigParamsFromCr(any());
    doReturn(lockedUniverse)
        .when(mockOperatorUtils)
        .getUniverseFromNameAndNamespace(eq(testCustomer.getId()), anyString(), anyString());

    pitrConfigReconciler.createActionReconcile(pitrConfig, testCustomer);

    verify(mockPitrConfigHelper, never()).createPitrConfig(any(), any(), any(), any(), any());
  }
}
