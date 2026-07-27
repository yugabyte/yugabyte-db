// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.operator;

import static org.junit.Assert.assertEquals;
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
import com.yugabyte.yw.common.ValidatingFormFactory;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.kms.KMSConfigHelper;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.common.operator.utils.KubernetesClientFactory;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.common.operator.utils.OperatorWorkQueue;
import com.yugabyte.yw.common.operator.utils.UniverseImporter;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.KmsConfig;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.client.KubernetesClient;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.NonNamespaceOperation;
import io.fabric8.kubernetes.client.dsl.Resource;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.yugabyte.operator.v1alpha1.KMSConfig;
import io.yugabyte.operator.v1alpha1.KMSConfigSpec;
import io.yugabyte.operator.v1alpha1.kmsconfigspec.Vault;
import io.yugabyte.operator.v1alpha1.kmsconfigspec.vault.AppRole;
import io.yugabyte.operator.v1alpha1.kmsconfigspec.vault.TokenSecret;
import io.yugabyte.operator.v1alpha1.kmsconfigspec.vault.approle.SecretIdSecret;
import java.util.Collections;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.MockedStatic;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class KMSConfigReconcilerTest extends FakeDBApplication {

  private KMSConfigHelper mockKmsConfigHelper;
  private ValidatingFormFactory mockFormFactory;
  private OperatorUtils mockOperatorUtils;
  private KubernetesClient mockClient;
  private YBInformerFactory mockInformerFactory;
  private MixedOperation<KMSConfig, KubernetesResourceList<KMSConfig>, Resource<KMSConfig>>
      mockResourceClient;
  private NonNamespaceOperation<KMSConfig, KubernetesResourceList<KMSConfig>, Resource<KMSConfig>>
      mockInNamespaceResourceClient;
  private Resource<KMSConfig> mockKmsConfigResource;
  private Customer testCustomer;
  private KMSConfigReconciler kmsConfigReconciler;
  private final String namespace = "test-namespace";

  @Before
  @SuppressWarnings("unchecked")
  public void setup() {
    mockKmsConfigHelper = Mockito.mock(KMSConfigHelper.class);
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
    mockKmsConfigResource = Mockito.mock(Resource.class);

    // Using lenient() for shared setup mocks to avoid UnnecessaryStubbingException.
    lenient()
        .when(mockInformerFactory.getSharedIndexInformer(eq(KMSConfig.class), any()))
        .thenReturn(Mockito.mock(SharedIndexInformer.class));

    lenient().when(mockClient.resources(eq(KMSConfig.class))).thenReturn(mockResourceClient);
    lenient()
        .when(mockResourceClient.inNamespace(anyString()))
        .thenReturn(mockInNamespaceResourceClient);
    lenient()
        .when(mockInNamespaceResourceClient.withName(anyString()))
        .thenReturn(mockKmsConfigResource);
    lenient()
        .when(mockInNamespaceResourceClient.resource(any(KMSConfig.class)))
        .thenReturn(mockKmsConfigResource);

    kmsConfigReconciler =
        spy(
            new KMSConfigReconciler(
                mockKmsConfigHelper,
                namespace,
                mockOperatorUtils,
                mockClient,
                mockInformerFactory));

    testCustomer = ModelFactory.testCustomer();
  }

  private KMSConfig createHashicorpTokenCr(String name) {
    KMSConfig kmsConfig = baseCr(name, KMSConfigSpec.Provider.HASHICORP);
    Vault vault = new Vault();
    vault.setAddress("http://vault:8200");
    vault.setAuthType(Vault.AuthType.TOKEN);
    TokenSecret tokenSecret = new TokenSecret();
    tokenSecret.setName("vault-token");
    tokenSecret.setKey("token");
    vault.setTokenSecret(tokenSecret);
    kmsConfig.getSpec().setVault(vault);
    return kmsConfig;
  }

  private KMSConfig createHashicorpAppRoleCr(String name) {
    KMSConfig kmsConfig = baseCr(name, KMSConfigSpec.Provider.HASHICORP);
    Vault vault = new Vault();
    vault.setAddress("http://vault:8200");
    vault.setAuthType(Vault.AuthType.APPROLE);
    vault.setAuthNamespace("admin");
    AppRole appRole = new AppRole();
    appRole.setRoleID("role-id");
    SecretIdSecret secretIdSecret = new SecretIdSecret();
    secretIdSecret.setName("vault-approle-secret-id");
    secretIdSecret.setKey("secret-id");
    appRole.setSecretIdSecret(secretIdSecret);
    vault.setAppRole(appRole);
    kmsConfig.getSpec().setVault(vault);
    return kmsConfig;
  }

  // AWS is not implemented by the operator; used to exercise the unsupported-provider path.
  private KMSConfig createAwsCr(String name) {
    return baseCr(name, KMSConfigSpec.Provider.AWS);
  }

  private KMSConfig baseCr(String name, KMSConfigSpec.Provider provider) {
    KMSConfig kmsConfig = new KMSConfig();
    ObjectMeta metadata = new ObjectMeta();
    metadata.setName(name);
    metadata.setNamespace(namespace);
    metadata.setUid(UUID.randomUUID().toString());
    metadata.setGeneration(1L);
    kmsConfig.setMetadata(metadata);
    KMSConfigSpec spec = new KMSConfigSpec();
    spec.setName(name + "-config");
    spec.setProvider(provider);
    kmsConfig.setSpec(spec);
    return kmsConfig;
  }

  private String workQueueKey(KMSConfig kmsConfig) {
    return OperatorWorkQueue.getWorkQueueKey(kmsConfig.getMetadata());
  }

  // --- CREATE tests ---

  @Test
  public void testCreateNewKMSConfigToken() throws Exception {
    KMSConfig kmsConfig = createHashicorpTokenCr("test-kms");
    UUID taskUUID = UUID.randomUUID();

    doReturn(Json.newObject()).when(mockOperatorUtils).getKMSConfigFormDataFromCr(any());
    when(mockKmsConfigHelper.createKMSConfig(
            eq(testCustomer.getUuid()), eq(KeyProvider.HASHICORP), any(ObjectNode.class)))
        .thenReturn(taskUUID);

    kmsConfigReconciler.createActionReconcile(kmsConfig, testCustomer);

    verify(mockKmsConfigHelper, times(1))
        .createKMSConfig(
            eq(testCustomer.getUuid()), eq(KeyProvider.HASHICORP), any(ObjectNode.class));
    assertEquals(taskUUID, kmsConfigReconciler.getKMSConfigTaskMapValue(workQueueKey(kmsConfig)));
  }

  @Test
  public void testCreateNewKMSConfigAppRole() throws Exception {
    KMSConfig kmsConfig = createHashicorpAppRoleCr("test-kms-approle");
    UUID taskUUID = UUID.randomUUID();

    doReturn(Json.newObject()).when(mockOperatorUtils).getKMSConfigFormDataFromCr(any());
    when(mockKmsConfigHelper.createKMSConfig(
            eq(testCustomer.getUuid()), eq(KeyProvider.HASHICORP), any(ObjectNode.class)))
        .thenReturn(taskUUID);

    kmsConfigReconciler.createActionReconcile(kmsConfig, testCustomer);

    verify(mockKmsConfigHelper, times(1))
        .createKMSConfig(
            eq(testCustomer.getUuid()), eq(KeyProvider.HASHICORP), any(ObjectNode.class));
    assertEquals(taskUUID, kmsConfigReconciler.getKMSConfigTaskMapValue(workQueueKey(kmsConfig)));
  }

  @Test
  public void testCreateSetsFinalizer() throws Exception {
    KMSConfig kmsConfig = createHashicorpTokenCr("test-kms");
    kmsConfig.getMetadata().setFinalizers(Collections.emptyList());

    doReturn(Json.newObject()).when(mockOperatorUtils).getKMSConfigFormDataFromCr(any());
    when(mockKmsConfigHelper.createKMSConfig(any(), any(), any(ObjectNode.class)))
        .thenReturn(UUID.randomUUID());

    kmsConfigReconciler.createActionReconcile(kmsConfig, testCustomer);

    verify(mockKmsConfigResource, times(1)).patch(any(KMSConfig.class));
    assertEquals(OperatorUtils.YB_FINALIZER, kmsConfig.getMetadata().getFinalizers().get(0));
  }

  @Test
  public void testCreateAdoptsExistingConfigByName() throws Exception {
    KMSConfig kmsConfig = createHashicorpTokenCr("test-kms");
    UUID existingUuid = UUID.randomUUID();

    KmsConfig existing = Mockito.mock(KmsConfig.class);
    when(existing.getName()).thenReturn(kmsConfig.getSpec().getName());
    lenient().when(existing.getConfigUUID()).thenReturn(existingUuid);
    // updateStatus re-reads the latest CR from the API server.
    when(mockKmsConfigResource.get()).thenReturn(kmsConfig);

    try (MockedStatic<KmsConfig> mocked = Mockito.mockStatic(KmsConfig.class)) {
      mocked
          .when(() -> KmsConfig.listKMSConfigs(testCustomer.getUuid()))
          .thenReturn(List.of(existing));

      kmsConfigReconciler.createActionReconcile(kmsConfig, testCustomer);
    }

    verify(mockKmsConfigHelper, never()).createKMSConfig(any(), any(), any(ObjectNode.class));
    assertEquals("Ready", kmsConfig.getStatus().getState());
  }

  @Test
  public void testCreateUnsupportedProviderSetsError() throws Exception {
    KMSConfig kmsConfig = createAwsCr("test-kms-aws");
    // Let the real form-data builder run so the unsupported-provider path throws.
    when(mockKmsConfigResource.get()).thenReturn(kmsConfig);

    kmsConfigReconciler.createActionReconcile(kmsConfig, testCustomer);

    verify(mockKmsConfigHelper, never()).createKMSConfig(any(), any(), any(ObjectNode.class));
    assertNull(kmsConfigReconciler.getKMSConfigTaskMapValue(workQueueKey(kmsConfig)));
    assertEquals("Error", kmsConfig.getStatus().getState());
    String msg = kmsConfig.getStatus().getMessage();
    assertTrue(msg, msg.contains("not yet supported"));
  }

  // --- UPDATE tests ---

  @Test
  public void testUpdateIgnoredWhenNoStatus() throws Exception {
    KMSConfig kmsConfig = createHashicorpTokenCr("test-kms");
    // No status set -> resource does not exist yet.

    kmsConfigReconciler.updateActionReconcile(kmsConfig, testCustomer);

    verify(mockKmsConfigHelper, never())
        .editKMSConfig(any(), any(), any(), anyString(), any(ObjectNode.class));
  }

  // --- NO_OP tests ---

  @Test
  public void testNoOpRequeuesCreateWhenNotFound() throws Exception {
    KMSConfig kmsConfig = createHashicorpTokenCr("test-kms");
    // No status, no tracked task, and no existing config -> a CREATE is requeued and no task map
    // entry is created.

    kmsConfigReconciler.noOpActionReconcile(kmsConfig, testCustomer);

    assertNull(kmsConfigReconciler.getKMSConfigTaskMapValue(workQueueKey(kmsConfig)));
    verify(mockKmsConfigHelper, never()).createKMSConfig(any(), any(), any(ObjectNode.class));
  }

  // --- DELETE tests ---

  @Test
  public void testDeleteNoStatusRemovesFinalizer() throws Exception {
    KMSConfig kmsConfig = createHashicorpTokenCr("test-kms");
    kmsConfig.getMetadata().setFinalizers(Collections.singletonList(OperatorUtils.YB_FINALIZER));
    // No status set -> nothing to delete in YBA, just drop the finalizer.

    kmsConfigReconciler.handleResourceDeletion(
        kmsConfig, testCustomer, OperatorWorkQueue.ResourceAction.DELETE);

    verify(mockOperatorUtils, times(1)).removeFinalizer(eq(kmsConfig), any());
  }
}
