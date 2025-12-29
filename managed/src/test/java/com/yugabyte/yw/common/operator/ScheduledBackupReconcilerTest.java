// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.operator;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doCallRealMethod;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ValidatingFormFactory;
import com.yugabyte.yw.common.backuprestore.BackupHelper;
import com.yugabyte.yw.common.backuprestore.ScheduleTaskHelper;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.operator.utils.KubernetesClientFactory;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.common.operator.utils.OperatorWorkQueue;
import com.yugabyte.yw.common.operator.utils.UniverseImporter;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.backuprestore.BackupScheduleTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.NonNamespaceOperation;
import io.fabric8.kubernetes.client.dsl.Resource;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.fabric8.kubernetes.client.informers.cache.Indexer;
import io.yugabyte.operator.v1alpha1.BackupSchedule;
import io.yugabyte.operator.v1alpha1.BackupScheduleSpec;
import io.yugabyte.operator.v1alpha1.StorageConfig;
import io.yugabyte.operator.v1alpha1.YBUniverse;
import java.util.UUID;
import javax.annotation.Nullable;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class ScheduledBackupReconcilerTest extends FakeDBApplication {

  private BackupHelper mockBackupHelper;
  private ValidatingFormFactory mockFormFactory;
  private RuntimeConfGetter mockConfGetter;
  private OperatorUtils mockOperatorUtils;
  private YBClientService mockYbClientService;
  private KubernetesClient mockClient;
  private YBInformerFactory mockInformerFactory;
  private SharedIndexInformer<BackupSchedule> mockScheduleInformer;
  private SharedIndexInformer<StorageConfig> mockScInformer;
  private KubernetesClientFactory mockKubernetesClientFactory;
  private UniverseImporter mockUniverseImporter;
  private MixedOperation<
          BackupSchedule, KubernetesResourceList<BackupSchedule>, Resource<BackupSchedule>>
      mockResourceClient;
  NonNamespaceOperation<
          BackupSchedule,
          KubernetesResourceList<BackupSchedule>,
          io.fabric8.kubernetes.client.dsl.Resource<BackupSchedule>>
      mockInNamespaceResourceClient;
  private Resource<BackupSchedule> mockBackupScheduleResource;
  private Indexer<BackupSchedule> mockScheduleIndexer;
  private ScheduleTaskHelper mockScheduleTaskHelper;
  private ScheduledBackupReconciler scheduledBackupReconciler;
  private Customer testCustomer;
  private Provider testProvider;
  private Universe testUniverse;
  private KubernetesResourceDetails universeResource;
  private KubernetesManagerFactory mockKubernetesManagerFactory;

  private final String namespace = "test-namespace";

  @Before
  public void setup() {
    mockBackupHelper = Mockito.mock(BackupHelper.class);
    mockFormFactory = Mockito.mock(ValidatingFormFactory.class);
    mockConfGetter = Mockito.mock(RuntimeConfGetter.class);
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
    when(mockKubernetesClientFactory.getKubernetesClientWithConfig(any())).thenReturn(mockClient);
    mockInformerFactory = Mockito.mock(YBInformerFactory.class);
    mockScheduleTaskHelper = Mockito.mock(ScheduleTaskHelper.class);
    mockScheduleInformer = Mockito.mock(SharedIndexInformer.class);
    mockScInformer = Mockito.mock(SharedIndexInformer.class);
    mockResourceClient = Mockito.mock(MixedOperation.class);
    mockScheduleIndexer = Mockito.mock(Indexer.class);
    mockInNamespaceResourceClient = Mockito.mock(NonNamespaceOperation.class);
    mockBackupScheduleResource = Mockito.mock(Resource.class);

    when(mockInformerFactory.getSharedIndexInformer(
            eq(BackupSchedule.class), any(KubernetesClient.class)))
        .thenReturn(mockScheduleInformer);
    when(mockInformerFactory.getSharedIndexInformer(
            eq(StorageConfig.class), any(KubernetesClient.class)))
        .thenReturn(mockScInformer);
    when(mockScheduleInformer.getIndexer()).thenReturn(mockScheduleIndexer);
    when(mockClient.resources(eq(BackupSchedule.class))).thenReturn(mockResourceClient);

    scheduledBackupReconciler =
        spy(
            new ScheduledBackupReconciler(
                mockBackupHelper,
                mockFormFactory,
                namespace,
                mockOperatorUtils,
                mockClient,
                mockInformerFactory,
                mockScheduleTaskHelper));

    testCustomer = ModelFactory.testCustomer();
    testProvider = ModelFactory.kubernetesProvider(testCustomer);
    testUniverse = ModelFactory.createUniverse(testCustomer.getId());
    universeResource = new KubernetesResourceDetails("test-universe", "test-namespace");
    UniverseUpdater updater =
        new UniverseUpdater() {
          @Override
          public void run(Universe universe) {
            UniverseDefinitionTaskParams params = universe.getUniverseDetails();
            params.setKubernetesResourceDetails(universeResource);
            universe.setUniverseDetails(params);
          }
        };
    testUniverse = Universe.saveDetails(testUniverse.getUniverseUUID(), updater);
  }

  private BackupSchedule getBackupScheduleCr(@Nullable String scheduleName) {
    BackupSchedule backupSchedule = new BackupSchedule();
    ObjectMeta metadata = new ObjectMeta();
    metadata.setName(scheduleName == null ? "test-schedule" : scheduleName);
    metadata.setNamespace(namespace);
    metadata.setUid(UUID.randomUUID().toString());
    metadata.setGeneration((long) 123);
    BackupScheduleSpec spec = new BackupScheduleSpec();
    spec.setBackupType(BackupScheduleSpec.BackupType.PGSQL_TABLE_TYPE);
    spec.setIncrementalBackupFrequency(900000L);
    spec.setKeyspace("testdb");
    spec.setSchedulingFrequency(3600000L);
    spec.setTimeBeforeDelete(84600000L);
    spec.setUniverse("test-universe");
    spec.setStorageConfig("test-storageconfig");
    backupSchedule.setMetadata(metadata);
    backupSchedule.setSpec(spec);
    return backupSchedule;
  }

  @Test
  public void testCreateScheduleTask() throws Exception {
    BackupSchedule backupSchedule = getBackupScheduleCr(null);
    YBUniverse ybUniverse = ModelFactory.createYbUniverse("test-universe", testProvider);
    when(mockResourceClient.inNamespace(anyString())).thenReturn(mockInNamespaceResourceClient);
    when(mockInNamespaceResourceClient.withName(anyString()))
        .thenReturn(mockBackupScheduleResource);
    doReturn(ybUniverse).when(mockOperatorUtils).getResource(any(), any(), eq(YBUniverse.class));
    doCallRealMethod()
        .when(mockOperatorUtils)
        .getResourceOwnerReference(any(), eq(YBUniverse.class));
    UUID taskUUID = UUID.randomUUID();
    when(mockScheduleTaskHelper.createCreateScheduledBackupTask(
            any(BackupScheduleTaskParams.class), any(Customer.class), any(Universe.class)))
        .thenReturn(taskUUID);

    scheduledBackupReconciler.createScheduleTask(
        new BackupScheduleTaskParams(), backupSchedule, testCustomer, testUniverse);

    Mockito.verify(mockScheduleTaskHelper, times(1))
        .createCreateScheduledBackupTask(
            any(BackupScheduleTaskParams.class), any(Customer.class), any(Universe.class));
    assertEquals(OperatorUtils.YB_FINALIZER, backupSchedule.getMetadata().getFinalizers().get(0));
    assertEquals(
        ybUniverse.getMetadata().getName(),
        backupSchedule.getMetadata().getOwnerReferences().get(0).getName());
    assertEquals(
        ybUniverse.getMetadata().getUid(),
        backupSchedule.getMetadata().getOwnerReferences().get(0).getUid());
    assertTrue(backupSchedule.getMetadata().getAnnotations().containsKey("universeUUID"));
    assertEquals(
        testUniverse.getUniverseUUID().toString(),
        backupSchedule.getMetadata().getAnnotations().get("universeUUID"));
    assertEquals(
        taskUUID,
        scheduledBackupReconciler.getScheduleTaskMapValue(
            OperatorWorkQueue.getWorkQueueKey(backupSchedule.getMetadata())));
  }
}
