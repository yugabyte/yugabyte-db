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
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.common.operator.utils.KubernetesClientFactory;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.common.operator.utils.UniverseImporter;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.controllers.handlers.UniverseActionsHandler;
import com.yugabyte.yw.forms.EncryptionAtRestConfig;
import com.yugabyte.yw.forms.EncryptionAtRestKeyParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.KmsHistory;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.NonNamespaceOperation;
import io.fabric8.kubernetes.client.dsl.Resource;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.yugabyte.operator.v1alpha1.UniverseKeyRotation;
import io.yugabyte.operator.v1alpha1.UniverseKeyRotationSpec;
import io.yugabyte.operator.v1alpha1.UniverseKeyRotationStatus;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.MockedStatic;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class UniverseKeyRotationReconcilerTest extends FakeDBApplication {

  private UniverseActionsHandler mockUniverseActionsHandler;
  private OperatorUtils mockOperatorUtils;
  private KubernetesClient mockClient;
  private YBInformerFactory mockInformerFactory;
  private MixedOperation<
          UniverseKeyRotation,
          KubernetesResourceList<UniverseKeyRotation>,
          Resource<UniverseKeyRotation>>
      mockResourceClient;
  private NonNamespaceOperation<
          UniverseKeyRotation,
          KubernetesResourceList<UniverseKeyRotation>,
          Resource<UniverseKeyRotation>>
      mockInNamespaceResourceClient;
  private Resource<UniverseKeyRotation> mockResource;
  private Customer testCustomer;
  private Universe testUniverse;
  private UniverseKeyRotationReconciler reconciler;
  private final String namespace = "test-namespace";

  @Before
  @SuppressWarnings("unchecked")
  public void setup() {
    mockUniverseActionsHandler = Mockito.mock(UniverseActionsHandler.class);
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
        .when(mockInformerFactory.getSharedIndexInformer(eq(UniverseKeyRotation.class), any()))
        .thenReturn(Mockito.mock(SharedIndexInformer.class));
    lenient()
        .when(mockClient.resources(eq(UniverseKeyRotation.class)))
        .thenReturn(mockResourceClient);
    lenient()
        .when(mockResourceClient.inNamespace(anyString()))
        .thenReturn(mockInNamespaceResourceClient);
    lenient().when(mockInNamespaceResourceClient.withName(anyString())).thenReturn(mockResource);
    lenient()
        .when(mockInNamespaceResourceClient.resource(any(UniverseKeyRotation.class)))
        .thenReturn(mockResource);

    reconciler =
        spy(
            new UniverseKeyRotationReconciler(
                mockUniverseActionsHandler,
                namespace,
                mockOperatorUtils,
                mockClient,
                mockInformerFactory));

    testCustomer = ModelFactory.testCustomer();
    testUniverse = ModelFactory.createUniverse("test-universe", testCustomer.getId());
  }

  // Universe key rotation requires encryption at rest to already be enabled on the universe.
  private void enableEncryptionAtRest(Universe universe) {
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    details.encryptionAtRestConfig = new EncryptionAtRestConfig();
    details.encryptionAtRestConfig.encryptionAtRestEnabled = true;
    universe.setUniverseDetails(details);
  }

  private UniverseKeyRotation createRotationCr(String name) {
    UniverseKeyRotation rotation = new UniverseKeyRotation();
    ObjectMeta metadata = new ObjectMeta();
    metadata.setName(name);
    metadata.setNamespace(namespace);
    rotation.setMetadata(metadata);
    UniverseKeyRotationSpec spec = new UniverseKeyRotationSpec();
    spec.setUniverse("test-universe");
    rotation.setSpec(spec);
    return rotation;
  }

  @Test
  public void testCreateTriggersRotation() throws Exception {
    UniverseKeyRotation rotation = createRotationCr("test-rotation");
    UUID taskUUID = UUID.randomUUID();
    UUID configUUID = UUID.randomUUID();
    when(mockResource.get()).thenReturn(rotation);
    enableEncryptionAtRest(testUniverse);

    doReturn(testUniverse)
        .when(mockOperatorUtils)
        .getUniverseFromNameAndNamespace(
            eq(testCustomer.getId()), eq("test-universe"), eq(namespace));

    KmsHistory activeKey = Mockito.mock(KmsHistory.class);
    when(activeKey.getConfigUuid()).thenReturn(configUUID);
    when(mockUniverseActionsHandler.setUniverseKey(
            eq(testCustomer), eq(testUniverse), any(EncryptionAtRestKeyParams.class)))
        .thenReturn(taskUUID);

    try (MockedStatic<EncryptionAtRestUtil> earUtil =
            Mockito.mockStatic(EncryptionAtRestUtil.class);
        MockedStatic<KmsConfig> kmsConfig = Mockito.mockStatic(KmsConfig.class)) {
      earUtil
          .when(() -> EncryptionAtRestUtil.getActiveKey(testUniverse.getUniverseUUID()))
          .thenReturn(activeKey);
      kmsConfig.when(() -> KmsConfig.get(configUUID)).thenReturn(Mockito.mock(KmsConfig.class));

      reconciler.createActionReconcile(rotation, testCustomer);
    }

    verify(mockUniverseActionsHandler, times(1))
        .setUniverseKey(eq(testCustomer), eq(testUniverse), any(EncryptionAtRestKeyParams.class));
    assertEquals(taskUUID.toString(), rotation.getStatus().getTaskUUID());
    assertEquals("Running", rotation.getStatus().getState());
  }

  @Test
  public void testCreateFailsWhenUniverseNotFound() throws Exception {
    UniverseKeyRotation rotation = createRotationCr("test-rotation");
    when(mockResource.get()).thenReturn(rotation);
    doReturn(null)
        .when(mockOperatorUtils)
        .getUniverseFromNameAndNamespace(
            eq(testCustomer.getId()), eq("test-universe"), eq(namespace));

    reconciler.createActionReconcile(rotation, testCustomer);

    verify(mockUniverseActionsHandler, never()).setUniverseKey(any(), any(), any());
    assertEquals("Failed", rotation.getStatus().getState());
  }

  @Test
  public void testCreateFailsWhenEarNotEnabled() throws Exception {
    UniverseKeyRotation rotation = createRotationCr("test-rotation");
    when(mockResource.get()).thenReturn(rotation);
    // Universe is left with encryption at rest disabled.
    doReturn(testUniverse)
        .when(mockOperatorUtils)
        .getUniverseFromNameAndNamespace(
            eq(testCustomer.getId()), eq("test-universe"), eq(namespace));

    reconciler.createActionReconcile(rotation, testCustomer);

    verify(mockUniverseActionsHandler, never()).setUniverseKey(any(), any(), any());
    assertEquals("Failed", rotation.getStatus().getState());
    org.junit.Assert.assertTrue(
        rotation.getStatus().getMessage().contains("does not have encryption at rest enabled"));
  }

  @Test
  public void testCreateFailsWhenNoActiveKey() throws Exception {
    UniverseKeyRotation rotation = createRotationCr("test-rotation");
    when(mockResource.get()).thenReturn(rotation);
    // EAR is enabled, but the universe has no active universe key to rotate.
    enableEncryptionAtRest(testUniverse);
    doReturn(testUniverse)
        .when(mockOperatorUtils)
        .getUniverseFromNameAndNamespace(
            eq(testCustomer.getId()), eq("test-universe"), eq(namespace));

    try (MockedStatic<EncryptionAtRestUtil> earUtil =
        Mockito.mockStatic(EncryptionAtRestUtil.class)) {
      earUtil
          .when(() -> EncryptionAtRestUtil.getActiveKey(testUniverse.getUniverseUUID()))
          .thenReturn(null);

      reconciler.createActionReconcile(rotation, testCustomer);
    }

    verify(mockUniverseActionsHandler, never()).setUniverseKey(any(), any(), any());
    assertEquals("Failed", rotation.getStatus().getState());
    org.junit.Assert.assertTrue(
        rotation.getStatus().getMessage().contains("does not have an active universe key"));
  }

  @Test
  public void testCreateSkipsWhenTaskAlreadyInProgress() throws Exception {
    UniverseKeyRotation rotation = createRotationCr("test-rotation");
    UUID taskUUID = UUID.randomUUID();
    UniverseKeyRotationStatus status = new UniverseKeyRotationStatus();
    status.setState("Running");
    status.setTaskUUID(taskUUID.toString());
    rotation.setStatus(status);

    TaskInfo runningTask = Mockito.mock(TaskInfo.class);
    when(runningTask.getTaskState()).thenReturn(TaskInfo.State.Running);

    try (MockedStatic<TaskInfo> taskInfoStatic = Mockito.mockStatic(TaskInfo.class)) {
      taskInfoStatic
          .when(() -> TaskInfo.maybeGet(taskUUID))
          .thenReturn(java.util.Optional.of(runningTask));

      reconciler.createActionReconcile(rotation, testCustomer);
    }

    // Idempotency: a repeated create for a rotation whose task is still running must not submit a
    // second SetUniverseKey task, and must leave the existing status untouched.
    verify(mockUniverseActionsHandler, never()).setUniverseKey(any(), any(), any());
    assertEquals("Running", rotation.getStatus().getState());
    assertEquals(taskUUID.toString(), rotation.getStatus().getTaskUUID());
  }

  @Test
  public void testSucceededTaskMarksSucceeded() throws Exception {
    UniverseKeyRotation rotation = createRotationCr("test-rotation");
    UUID taskUUID = UUID.randomUUID();
    UniverseKeyRotationStatus status = new UniverseKeyRotationStatus();
    status.setState("Running");
    status.setTaskUUID(taskUUID.toString());
    rotation.setStatus(status);
    when(mockResource.get()).thenReturn(rotation);

    TaskInfo succeededTask = Mockito.mock(TaskInfo.class);
    when(succeededTask.getTaskState()).thenReturn(TaskInfo.State.Success);
    lenient().when(succeededTask.getUuid()).thenReturn(taskUUID);

    try (MockedStatic<TaskInfo> taskInfoStatic = Mockito.mockStatic(TaskInfo.class)) {
      taskInfoStatic
          .when(() -> TaskInfo.maybeGet(taskUUID))
          .thenReturn(java.util.Optional.of(succeededTask));

      reconciler.noOpActionReconcile(rotation, testCustomer);
    }

    // Terminal success: state flips to Succeeded and completedAt is stamped.
    assertEquals("Succeeded", rotation.getStatus().getState());
    org.junit.Assert.assertNotNull(rotation.getStatus().getCompletedAt());
  }

  @Test
  public void testFailedTaskSchedulesRetryWhenRetriesRemain() throws Exception {
    UniverseKeyRotation rotation = createRotationCr("test-rotation");
    UUID taskUUID = UUID.randomUUID();
    UniverseKeyRotationStatus status = new UniverseKeyRotationStatus();
    status.setState("Running");
    status.setTaskUUID(taskUUID.toString());
    status.setRetryCount(1L);
    rotation.setStatus(status);
    when(mockResource.get()).thenReturn(rotation);

    TaskInfo failedTask = Mockito.mock(TaskInfo.class);
    when(failedTask.getTaskState()).thenReturn(TaskInfo.State.Failure);
    lenient().when(failedTask.getUuid()).thenReturn(taskUUID);
    lenient().when(failedTask.getErrorMessage()).thenReturn("boom");

    try (MockedStatic<TaskInfo> taskInfoStatic = Mockito.mockStatic(TaskInfo.class)) {
      taskInfoStatic
          .when(() -> TaskInfo.maybeGet(taskUUID))
          .thenReturn(java.util.Optional.of(failedTask));

      reconciler.noOpActionReconcile(rotation, testCustomer);
    }

    // Non-terminal: state becomes Retrying and the durable retry count is bumped.
    assertEquals("Retrying", rotation.getStatus().getState());
    assertEquals(Long.valueOf(2L), rotation.getStatus().getRetryCount());
    org.junit.Assert.assertNull(rotation.getStatus().getCompletedAt());
  }

  @Test
  public void testFailedTaskMarksFailedWhenRetriesExhausted() throws Exception {
    UniverseKeyRotation rotation = createRotationCr("test-rotation");
    UUID taskUUID = UUID.randomUUID();
    UniverseKeyRotationStatus status = new UniverseKeyRotationStatus();
    status.setState("Retrying");
    status.setTaskUUID(taskUUID.toString());
    // At the retry limit (MAX_ROTATION_RETRIES = 5).
    status.setRetryCount(5L);
    rotation.setStatus(status);
    when(mockResource.get()).thenReturn(rotation);

    TaskInfo failedTask = Mockito.mock(TaskInfo.class);
    when(failedTask.getTaskState()).thenReturn(TaskInfo.State.Failure);
    lenient().when(failedTask.getUuid()).thenReturn(taskUUID);
    lenient().when(failedTask.getErrorMessage()).thenReturn("boom");

    try (MockedStatic<TaskInfo> taskInfoStatic = Mockito.mockStatic(TaskInfo.class)) {
      taskInfoStatic
          .when(() -> TaskInfo.maybeGet(taskUUID))
          .thenReturn(java.util.Optional.of(failedTask));

      reconciler.noOpActionReconcile(rotation, testCustomer);
    }

    // Terminal: state becomes Failed once retries are exhausted.
    assertEquals("Failed", rotation.getStatus().getState());
    org.junit.Assert.assertNotNull(rotation.getStatus().getCompletedAt());
    org.junit.Assert.assertTrue(rotation.getStatus().getMessage().contains("after 5 retries"));
  }
}
