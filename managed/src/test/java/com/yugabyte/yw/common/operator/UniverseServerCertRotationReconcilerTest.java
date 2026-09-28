// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.operator;

import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
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

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.common.ValidatingFormFactory;
import com.yugabyte.yw.common.certmgmt.CertConfigType;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.operator.utils.KubernetesClientFactory;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.common.operator.utils.ResourceAnnotationKeys;
import com.yugabyte.yw.common.operator.utils.UniverseImporter;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.controllers.handlers.UpgradeUniverseHandler;
import com.yugabyte.yw.forms.CertsRotateParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.NonNamespaceOperation;
import io.fabric8.kubernetes.client.dsl.Resource;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.yugabyte.operator.v1alpha1.UniverseServerCertRotation;
import io.yugabyte.operator.v1alpha1.UniverseServerCertRotationSpec;
import io.yugabyte.operator.v1alpha1.UniverseServerCertRotationStatus;
import io.yugabyte.operator.v1alpha1.YBUniverse;
import io.yugabyte.operator.v1alpha1.YBUniverseSpec;
import java.util.Collections;
import java.util.Date;
import java.util.UUID;
import org.apache.commons.lang3.RandomStringUtils;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class UniverseServerCertRotationReconcilerTest extends FakeDBApplication {

  private UpgradeUniverseHandler mockUpgradeUniverseHandler;
  private OperatorUtils mockOperatorUtils;
  private KubernetesClient mockClient;
  private YBInformerFactory mockInformerFactory;
  private MixedOperation<
          UniverseServerCertRotation,
          KubernetesResourceList<UniverseServerCertRotation>,
          Resource<UniverseServerCertRotation>>
      mockResourceClient;
  private NonNamespaceOperation<
          UniverseServerCertRotation,
          KubernetesResourceList<UniverseServerCertRotation>,
          Resource<UniverseServerCertRotation>>
      mockInNamespaceResourceClient;
  private Resource<UniverseServerCertRotation> mockResource;
  private Customer testCustomer;
  private Universe testUniverse;
  private YBUniverse ybUniverse;
  private UniverseServerCertRotationReconciler reconciler;
  private final String namespace = "test-namespace";

  @Before
  @SuppressWarnings("unchecked")
  public void setup() throws Exception {
    mockUpgradeUniverseHandler = Mockito.mock(UpgradeUniverseHandler.class);
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
    mockResource = Mockito.mock(Resource.class);

    lenient()
        .when(
            mockInformerFactory.getSharedIndexInformer(eq(UniverseServerCertRotation.class), any()))
        .thenReturn(Mockito.mock(SharedIndexInformer.class));
    lenient()
        .when(mockClient.resources(eq(UniverseServerCertRotation.class)))
        .thenReturn(mockResourceClient);
    lenient()
        .when(mockResourceClient.inNamespace(anyString()))
        .thenReturn(mockInNamespaceResourceClient);
    lenient().when(mockInNamespaceResourceClient.withName(anyString())).thenReturn(mockResource);
    lenient()
        .when(mockInNamespaceResourceClient.resource(any(UniverseServerCertRotation.class)))
        .thenReturn(mockResource);

    reconciler =
        spy(
            new UniverseServerCertRotationReconciler(
                mockUpgradeUniverseHandler,
                namespace,
                mockOperatorUtils,
                mockClient,
                mockInformerFactory));

    testCustomer = ModelFactory.testCustomer();
    testUniverse = ModelFactory.createUniverse("test-universe", testCustomer.getId());
    ybUniverse = createYbUniverseCr(testUniverse);
    lenient().doReturn(ybUniverse).when(mockOperatorUtils).getYBUniverse(any());
  }

  // The YBUniverse CR resolves to testUniverse through the yba-resource-id annotation.
  private YBUniverse createYbUniverseCr(Universe universe) {
    YBUniverse cr = new YBUniverse();
    ObjectMeta metadata = new ObjectMeta();
    metadata.setName("test-universe");
    metadata.setNamespace(namespace);
    metadata.setAnnotations(
        Collections.singletonMap(
            ResourceAnnotationKeys.YBA_RESOURCE_ID, universe.getUniverseUUID().toString()));
    cr.setMetadata(metadata);
    cr.setSpec(new YBUniverseSpec());
    return cr;
  }

  // Turns on both TLS modes on testUniverse with a fresh rootCA of the given type.
  private UUID enableTls(CertConfigType certType) throws Exception {
    UUID rootCA = UUID.randomUUID();
    createTempFile("universe_cert_rotation_test_ca.crt", "test data");
    CertificateInfo.create(
        rootCA,
        testCustomer.getUuid(),
        "test-root-ca-" + RandomStringUtils.randomAlphanumeric(8),
        new Date(),
        new Date(),
        "privateKey",
        TestHelper.TMP_PATH + "/universe_cert_rotation_test_ca.crt",
        certType);
    testUniverse =
        Universe.saveDetails(
            testUniverse.getUniverseUUID(),
            u -> {
              UserIntent userIntent = u.getUniverseDetails().getPrimaryCluster().userIntent;
              userIntent.enableNodeToNodeEncrypt = true;
              userIntent.enableClientToNodeEncrypt = true;
              u.getUniverseDetails().rootCA = rootCA;
              u.getUniverseDetails().rootAndClientRootCASame = true;
            });
    return rootCA;
  }

  private UniverseServerCertRotation createRotationCr(String name) {
    UniverseServerCertRotation rotation = new UniverseServerCertRotation();
    ObjectMeta metadata = new ObjectMeta();
    metadata.setName(name);
    metadata.setNamespace(namespace);
    rotation.setMetadata(metadata);
    UniverseServerCertRotationSpec spec = new UniverseServerCertRotationSpec();
    spec.setUniverse("test-universe");
    rotation.setSpec(spec);
    return rotation;
  }

  private UniverseServerCertRotation trackingTask(UUID taskUUID) {
    UniverseServerCertRotation rotation = createRotationCr("test-rotation");
    UniverseServerCertRotationStatus status = new UniverseServerCertRotationStatus();
    status.setState("Running");
    status.setTaskUUID(taskUUID.toString());
    rotation.setStatus(status);
    when(mockResource.get()).thenReturn(rotation);
    return rotation;
  }

  // Persists a CertsRotateKubernetesUpgrade task on testUniverse, as a retry of originalTaskUUID
  // when that is non-null.
  private TaskInfo createCertsRotateTask(TaskInfo.State state, UUID originalTaskUUID) {
    CertsRotateParams params = new CertsRotateParams();
    params.setUniverseUUID(testUniverse.getUniverseUUID());
    ObjectNode paramsJson = (ObjectNode) Json.toJson(params);
    if (originalTaskUUID != null) {
      paramsJson.put("originalTaskUUID", originalTaskUUID.toString());
    }
    TaskInfo taskInfo = new TaskInfo(TaskType.CertsRotateKubernetesUpgrade, null);
    taskInfo.setTaskParams(paramsJson);
    taskInfo.setTaskState(state);
    taskInfo.setOwner("localhost");
    taskInfo.save();
    taskInfo.refresh();
    CustomerTask.create(
        testCustomer,
        testUniverse.getUniverseUUID(),
        taskInfo.getUuid(),
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.CertsRotate,
        testUniverse.getName());
    return taskInfo;
  }

  private void setPlacementModificationTask(UUID taskUUID) {
    testUniverse =
        Universe.saveDetails(
            testUniverse.getUniverseUUID(),
            u -> u.getUniverseDetails().placementModificationTaskUuid = taskUUID);
  }

  @Test
  public void testCreateTriggersServerCertRotation() throws Exception {
    UUID rootCA = enableTls(CertConfigType.SelfSigned);
    UniverseServerCertRotation rotation = createRotationCr("test-rotation");
    rotation.getSpec().setUpgradeOption(UniverseServerCertRotationSpec.UpgradeOption.NONROLLING);
    when(mockResource.get()).thenReturn(rotation);
    UUID taskUUID = UUID.randomUUID();
    when(mockUpgradeUniverseHandler.rotateCerts(any(), eq(testCustomer), any()))
        .thenReturn(taskUUID);

    reconciler.createActionReconcile(rotation, testCustomer);

    ArgumentCaptor<CertsRotateParams> paramsCaptor =
        ArgumentCaptor.forClass(CertsRotateParams.class);
    verify(mockUpgradeUniverseHandler, times(1))
        .rotateCerts(paramsCaptor.capture(), eq(testCustomer), any());
    CertsRotateParams params = paramsCaptor.getValue();
    // rootCA is unchanged, so the task treats this as a server (leaf) cert rotation.
    assertEquals(rootCA, params.rootCA);
    assertTrue(params.rootAndClientRootCASame);
    assertTrue(params.selfSignedServerCertRotate);
    assertTrue(params.selfSignedClientCertRotate);
    assertEquals(UpgradeOption.NON_ROLLING_UPGRADE, params.upgradeOption);
    assertEquals(testUniverse.getUniverseUUID(), params.getUniverseUUID());
    // Task progress is reported on the ybuniverse resource.
    assertEquals("test-universe", params.getKubernetesResourceDetails().name);
    assertEquals(taskUUID.toString(), rotation.getStatus().getTaskUUID());
    assertEquals("Running", rotation.getStatus().getState());
  }

  @Test
  public void testCreateFailsWhenTlsDisabled() throws Exception {
    UniverseServerCertRotation rotation = createRotationCr("test-rotation");
    when(mockResource.get()).thenReturn(rotation);

    reconciler.createActionReconcile(rotation, testCustomer);

    verify(mockUpgradeUniverseHandler, never()).rotateCerts(any(), any(), any());
    assertEquals("Failed", rotation.getStatus().getState());
    assertTrue(
        rotation.getStatus().getMessage().contains("does not have encryption in transit enabled"));
  }

  @Test
  public void testCreateTriggersRotationForCertManagerCerts() throws Exception {
    UUID rootCA = enableTls(CertConfigType.K8SCertManager);
    UniverseServerCertRotation rotation = createRotationCr("test-rotation");
    when(mockResource.get()).thenReturn(rotation);
    when(mockUpgradeUniverseHandler.rotateCerts(any(), eq(testCustomer), any()))
        .thenReturn(UUID.randomUUID());

    reconciler.createActionReconcile(rotation, testCustomer);

    ArgumentCaptor<CertsRotateParams> paramsCaptor =
        ArgumentCaptor.forClass(CertsRotateParams.class);
    verify(mockUpgradeUniverseHandler, times(1))
        .rotateCerts(paramsCaptor.capture(), eq(testCustomer), any());
    assertEquals(rootCA, paramsCaptor.getValue().rootCA);
    assertTrue(paramsCaptor.getValue().selfSignedServerCertRotate);
    assertEquals("Running", rotation.getStatus().getState());
  }

  @Test
  public void testCreateFailsWhenUniverseNotFound() throws Exception {
    UniverseServerCertRotation rotation = createRotationCr("test-rotation");
    when(mockResource.get()).thenReturn(rotation);
    doReturn(null).when(mockOperatorUtils).getYBUniverse(any());

    reconciler.createActionReconcile(rotation, testCustomer);

    verify(mockUpgradeUniverseHandler, never()).rotateCerts(any(), any(), any());
    assertEquals("Failed", rotation.getStatus().getState());
  }

  @Test
  public void testCreateWaitsWhileFailedTaskPendingRetry() throws Exception {
    enableTls(CertConfigType.SelfSigned);
    setPlacementModificationTask(UUID.randomUUID());
    UniverseServerCertRotation rotation = createRotationCr("test-rotation");

    reconciler.createActionReconcile(rotation, testCustomer);

    // Requeued without submitting or writing a status.
    verify(mockUpgradeUniverseHandler, never()).rotateCerts(any(), any(), any());
    assertNull(rotation.getStatus());
  }

  @Test
  public void testNoOpFollowsRetryTaskToSuccess() throws Exception {
    TaskInfo failed = createCertsRotateTask(TaskInfo.State.Failure, null);
    TaskInfo retry = createCertsRotateTask(TaskInfo.State.Success, failed.getUuid());
    UniverseServerCertRotation rotation = trackingTask(failed.getUuid());

    reconciler.noOpActionReconcile(rotation, testCustomer);

    assertEquals(retry.getUuid().toString(), rotation.getStatus().getTaskUUID());
    assertEquals(Long.valueOf(1L), rotation.getStatus().getRetryCount());
    assertEquals("Succeeded", rotation.getStatus().getState());
    assertNotNull(rotation.getStatus().getCompletedAt());
  }

  @Test
  public void testNoOpWaitsForRetryOfPendingPlacementTask() throws Exception {
    TaskInfo failed = createCertsRotateTask(TaskInfo.State.Failure, null);
    setPlacementModificationTask(failed.getUuid());
    UniverseServerCertRotation rotation = trackingTask(failed.getUuid());

    reconciler.noOpActionReconcile(rotation, testCustomer);
    reconciler.noOpActionReconcile(rotation, testCustomer);

    // The ybuniverse reconciler owns the retry; nothing is resubmitted from here.
    verify(mockUpgradeUniverseHandler, never()).rotateCerts(any(), any(), any());
    assertEquals("Retrying", rotation.getStatus().getState());
    assertNull(rotation.getStatus().getCompletedAt());
    // The second pass must not rewrite the status, since each write triggers another reconcile.
    verify(mockResource, times(1)).updateStatus();
  }

  @Test
  public void testNoOpMarksFailedWhenTaskWillNotBeRetried() throws Exception {
    TaskInfo failed = createCertsRotateTask(TaskInfo.State.Failure, null);
    UniverseServerCertRotation rotation = trackingTask(failed.getUuid());

    reconciler.noOpActionReconcile(rotation, testCustomer);

    assertEquals("Failed", rotation.getStatus().getState());
    assertNotNull(rotation.getStatus().getCompletedAt());
  }

  @Test
  public void testCreateSkipsWhenTaskAlreadyTracked() throws Exception {
    TaskInfo running = createCertsRotateTask(TaskInfo.State.Running, null);
    UniverseServerCertRotation rotation = trackingTask(running.getUuid());

    reconciler.createActionReconcile(rotation, testCustomer);

    verify(mockUpgradeUniverseHandler, never()).rotateCerts(any(), any(), any());
    assertEquals(running.getUuid().toString(), rotation.getStatus().getTaskUUID());
    assertFalse("Failed".equals(rotation.getStatus().getState()));
  }
}
