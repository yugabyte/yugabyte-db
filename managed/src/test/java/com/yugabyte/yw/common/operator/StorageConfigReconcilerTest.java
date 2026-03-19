// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.operator;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.OperatorResource;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.api.model.Secret;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.NonNamespaceOperation;
import io.fabric8.kubernetes.client.dsl.Resource;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.fabric8.kubernetes.client.informers.cache.Indexer;
import io.fabric8.kubernetes.client.utils.Serialization;
import io.yugabyte.operator.v1alpha1.StorageConfig;
import io.yugabyte.operator.v1alpha1.StorageConfigSpec;
import io.yugabyte.operator.v1alpha1.StorageConfigStatus;
import io.yugabyte.operator.v1alpha1.storageconfigspec.AzureStorageSasTokenSecret;
import io.yugabyte.operator.v1alpha1.storageconfigspec.Data;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class StorageConfigReconcilerTest extends FakeDBApplication {

  @Mock SharedIndexInformer<StorageConfig> scInformer;

  @Mock
  MixedOperation<StorageConfig, KubernetesResourceList<StorageConfig>, Resource<StorageConfig>>
      resourceClient;

  @Mock CustomerConfigService ccs;
  @Mock OperatorUtils operatorUtils;
  @Mock Indexer<StorageConfig> indexer;

  @Mock
  NonNamespaceOperation<
          StorageConfig, KubernetesResourceList<StorageConfig>, Resource<StorageConfig>>
      inNamespaceResource;

  @Mock Resource<StorageConfig> storageConfigResource;

  private StorageConfigReconciler storageConfigReconciler;
  private static final String NAMESPACE = "test-namespace";

  @Before
  public void setup() throws Exception {
    when(scInformer.getIndexer()).thenReturn(indexer);
    when(resourceClient.inNamespace(anyString())).thenReturn(inNamespaceResource);
    when(inNamespaceResource.resource(any(StorageConfig.class))).thenReturn(storageConfigResource);
    when(operatorUtils.getCustomerUUID()).thenThrow(new RuntimeException("mock - skip processing"));
    storageConfigReconciler =
        new StorageConfigReconciler(scInformer, resourceClient, ccs, NAMESPACE, operatorUtils);
  }

  private StorageConfig createStorageConfigCr(String name) {
    StorageConfig sc = new StorageConfig();
    ObjectMeta metadata = new ObjectMeta();
    metadata.setName(name);
    metadata.setNamespace(NAMESPACE);
    metadata.setUid(UUID.randomUUID().toString());
    sc.setMetadata(metadata);
    sc.setStatus(null);
    StorageConfigSpec spec = new StorageConfigSpec();
    spec.setConfig_type(StorageConfigSpec.Config_type.STORAGE_S3);
    sc.setSpec(spec);
    return sc;
  }

  /** Creates a StorageConfig CR with Azure type and an Azure SAS token secret reference. */
  private StorageConfig createStorageConfigCrWithAzureSecret(
      String name, String secretName, String secretNamespace) {
    StorageConfig sc = new StorageConfig();
    ObjectMeta metadata = new ObjectMeta();
    metadata.setName(name);
    metadata.setNamespace(NAMESPACE);
    metadata.setUid(UUID.randomUUID().toString());
    sc.setMetadata(metadata);
    sc.setStatus(null);
    StorageConfigSpec spec = new StorageConfigSpec();
    spec.setConfig_type(StorageConfigSpec.Config_type.STORAGE_AZ);
    Data data = new Data();
    data.setBACKUP_LOCATION("https://account.blob.core.windows.net/container");
    spec.setData(data);
    AzureStorageSasTokenSecret secretRef = new AzureStorageSasTokenSecret();
    secretRef.setName(secretName);
    secretRef.setNamespace(secretNamespace);
    spec.setAzureStorageSasTokenSecret(secretRef);
    sc.setSpec(spec);
    return sc;
  }

  @Test
  public void testOnAddAddsResourceToTrackedResources() {
    String configName = "test-storage-config";
    StorageConfig sc = createStorageConfigCr(configName);

    storageConfigReconciler.onAdd(sc);

    assertEquals(1, storageConfigReconciler.getTrackedResources().size());
    KubernetesResourceDetails details =
        storageConfigReconciler.getTrackedResources().iterator().next();
    assertEquals(configName, details.name);
    assertEquals(NAMESPACE, details.namespace);

    // Verify OperatorResource entries were persisted in the database
    List<OperatorResource> allResources = OperatorResource.getAll();
    assertEquals(1, allResources.size());
    assertTrue(
        "OperatorResource name should contain the storage config name",
        allResources.get(0).getName().contains(configName));
    StorageConfig rSc = Serialization.unmarshal(allResources.get(0).getData(), StorageConfig.class);
    assertEquals(configName, rSc.getMetadata().getName());
    assertEquals(NAMESPACE, rSc.getMetadata().getNamespace());
    assertEquals(StorageConfigSpec.Config_type.STORAGE_S3, rSc.getSpec().getConfig_type());
  }

  @Test
  public void testOnAddWithExistingResourceUuidDoesNotAddToTrackedResources() {
    StorageConfig sc = createStorageConfigCr("existing-config");
    StorageConfigStatus status = new StorageConfigStatus();
    status.setResourceUUID(UUID.randomUUID().toString());
    sc.setStatus(status);

    storageConfigReconciler.onAdd(sc);

    assertTrue(
        "Tracked resources should be empty when already initialized (early return)",
        storageConfigReconciler.getTrackedResources().isEmpty());

    // Verify no OperatorResource entries were created in the database
    List<OperatorResource> allResources = OperatorResource.getAll();
    assertTrue(
        "No OperatorResource entries should exist when early return occurs",
        allResources.isEmpty());
  }

  @Test
  public void testOnAddWithAzureSecretTracksStorageConfigAndSecret() throws Exception {
    String configName = "azure-storage-config";
    String secretName = "azure-sas-token-secret";
    String secretNamespace = NAMESPACE;
    Customer customer = ModelFactory.testCustomer();
    String customerUUID = customer.getUuid().toString();

    doReturn(customerUUID).when(operatorUtils).getCustomerUUID();
    Secret secret = new Secret();
    ObjectMeta secretMeta = new ObjectMeta();
    secretMeta.setName(secretName);
    secretMeta.setNamespace(secretNamespace);
    secret.setMetadata(secretMeta);
    when(operatorUtils.getSecret(eq(secretName), eq(secretNamespace))).thenReturn(secret);
    when(operatorUtils.parseSecretForKey(
            eq(secret), eq(StorageConfigReconciler.AZURE_STORAGE_SAS_TOKEN_SECRET_KEY)))
        .thenReturn("mock-sas-token");
    doThrow(new RuntimeException("mock - skip create")).when(ccs).create(any());

    StorageConfig sc =
        createStorageConfigCrWithAzureSecret(configName, secretName, secretNamespace);

    storageConfigReconciler.onAdd(sc);

    assertEquals(2, storageConfigReconciler.getTrackedResources().size());
    assertTrue(
        storageConfigReconciler.getTrackedResources().stream()
            .anyMatch(d -> configName.equals(d.name) && NAMESPACE.equals(d.namespace)));
    assertTrue(
        storageConfigReconciler.getTrackedResources().stream()
            .anyMatch(d -> secretName.equals(d.name) && secretNamespace.equals(d.namespace)));

    // Verify the secret is tracked as a dependency of the storage config
    ResourceTracker tracker = storageConfigReconciler.getResourceTracker();
    KubernetesResourceDetails scDetails =
        tracker.getResourceDependencies().keySet().stream()
            .filter(d -> configName.equals(d.name))
            .findFirst()
            .orElse(null);
    assertTrue(scDetails != null);
    assertEquals(1, tracker.getDependencies(scDetails).size());

    // Verify OperatorResource entries were persisted in the database (storage config + secret)
    List<OperatorResource> allResources = OperatorResource.getAll();
    assertEquals(2, allResources.size());
    assertTrue(
        "OperatorResource entries should include the storage config",
        allResources.stream().anyMatch(r -> r.getName().contains(configName)));
    assertTrue(
        "OperatorResource entries should include the Azure secret",
        allResources.stream().anyMatch(r -> r.getName().contains(secretName)));
    OperatorResource scResource =
        allResources.stream()
            .filter(r -> r.getName().contains(configName))
            .findFirst()
            .orElse(null);
    StorageConfig rSc = Serialization.unmarshal(scResource.getData(), StorageConfig.class);
    assertEquals(configName, rSc.getMetadata().getName());
    assertEquals(NAMESPACE, rSc.getMetadata().getNamespace());
    assertEquals(StorageConfigSpec.Config_type.STORAGE_AZ, rSc.getSpec().getConfig_type());
    assertEquals(
        "https://account.blob.core.windows.net/container",
        rSc.getSpec().getData().getBACKUP_LOCATION());
  }

  @Test
  public void testOnDeleteRemovesOperatorResource() {
    String configName = "test-storage-config-delete";
    StorageConfig sc = createStorageConfigCr(configName);

    storageConfigReconciler.onAdd(sc);
    assertEquals(1, OperatorResource.getAll().size());

    // Delete - getCustomerUUID throws so cleanup skips, but untrackResource runs first
    storageConfigReconciler.onDelete(sc, false);

    assertTrue(
        "Tracked resources should be empty after delete",
        storageConfigReconciler.getTrackedResources().isEmpty());
    assertTrue(
        "OperatorResource entries should be removed after delete",
        OperatorResource.getAll().isEmpty());
  }

  @Test
  public void testOnDeleteWithAzureSecretRemovesAllOperatorResources() throws Exception {
    String configName = "azure-storage-config-delete";
    String secretName = "azure-sas-token-secret-delete";
    String secretNamespace = NAMESPACE;
    Customer customer = ModelFactory.testCustomer();
    String customerUUID = customer.getUuid().toString();

    doReturn(customerUUID).when(operatorUtils).getCustomerUUID();
    Secret secret = new Secret();
    ObjectMeta secretMeta = new ObjectMeta();
    secretMeta.setName(secretName);
    secretMeta.setNamespace(secretNamespace);
    secret.setMetadata(secretMeta);
    when(operatorUtils.getSecret(eq(secretName), eq(secretNamespace))).thenReturn(secret);
    when(operatorUtils.parseSecretForKey(
            eq(secret), eq(StorageConfigReconciler.AZURE_STORAGE_SAS_TOKEN_SECRET_KEY)))
        .thenReturn("mock-sas-token");
    doThrow(new RuntimeException("mock - skip create")).when(ccs).create(any());

    StorageConfig sc =
        createStorageConfigCrWithAzureSecret(configName, secretName, secretNamespace);

    storageConfigReconciler.onAdd(sc);
    assertEquals(2, OperatorResource.getAll().size());

    // For delete, need getCustomerUUID to throw so it exits early after untrackResource
    doThrow(new RuntimeException("mock - skip delete processing"))
        .when(operatorUtils)
        .getCustomerUUID();

    storageConfigReconciler.onDelete(sc, false);

    assertTrue(
        "Tracked resources should be empty after delete",
        storageConfigReconciler.getTrackedResources().isEmpty());
    assertTrue(
        "All OperatorResource entries (storage config + orphaned secret) should be removed",
        OperatorResource.getAll().isEmpty());
  }
}
