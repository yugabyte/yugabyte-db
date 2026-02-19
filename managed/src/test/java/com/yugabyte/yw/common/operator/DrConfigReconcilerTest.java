// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.operator;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.dr.DrConfigHelper;
import com.yugabyte.yw.common.operator.utils.OperatorUtils;
import com.yugabyte.yw.models.OperatorResource;
import io.fabric8.kubernetes.api.model.KubernetesResourceList;
import io.fabric8.kubernetes.api.model.ObjectMeta;
import io.fabric8.kubernetes.client.dsl.MixedOperation;
import io.fabric8.kubernetes.client.dsl.Resource;
import io.fabric8.kubernetes.client.informers.SharedIndexInformer;
import io.fabric8.kubernetes.client.utils.Serialization;
import io.yugabyte.operator.v1alpha1.DrConfig;
import io.yugabyte.operator.v1alpha1.DrConfigSpec;
import io.yugabyte.operator.v1alpha1.StorageConfig;
import java.util.Arrays;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class DrConfigReconcilerTest extends FakeDBApplication {

  @Mock SharedIndexInformer<DrConfig> drConfigInformer;

  @Mock
  MixedOperation<DrConfig, KubernetesResourceList<DrConfig>, Resource<DrConfig>> resourceClient;

  @Mock DrConfigHelper drConfigHelper;
  @Mock SharedIndexInformer<StorageConfig> scInformer;
  @Mock OperatorUtils operatorUtils;

  private DrConfigReconciler drConfigReconciler;
  private static final String NAMESPACE = "test-namespace";

  @Before
  public void setup() {
    drConfigReconciler =
        new DrConfigReconciler(
            drConfigInformer, resourceClient, drConfigHelper, NAMESPACE, scInformer, operatorUtils);
  }

  private DrConfig createDrConfigCr(String name) {
    DrConfig drConfig = new DrConfig();
    ObjectMeta metadata = new ObjectMeta();
    metadata.setName(name);
    metadata.setNamespace(NAMESPACE);
    metadata.setUid(UUID.randomUUID().toString());
    drConfig.setMetadata(metadata);
    drConfig.setStatus(null);
    DrConfigSpec spec = new DrConfigSpec();
    spec.setName(name);
    spec.setSourceUniverse("source-universe");
    spec.setTargetUniverse("target-universe");
    spec.setStorageConfig("my-storage-config");
    spec.setDatabases(Arrays.asList("db1", "db2"));
    drConfig.setSpec(spec);
    return drConfig;
  }

  @Test
  public void testOnAddAddsResourceToTrackedResources() throws Exception {
    String drConfigName = "test-drconfig";
    DrConfig drConfig = createDrConfigCr(drConfigName);
    when(operatorUtils.getDrConfigCreateFormFromCr(any(DrConfig.class), eq(scInformer)))
        .thenThrow(new RuntimeException("mock - skip full processing"));

    try {
      drConfigReconciler.onAdd(drConfig);
    } catch (Exception e) {
      // expected as we have not setup any k8s connection
    }

    assertEquals(1, drConfigReconciler.getTrackedResources().size());
    KubernetesResourceDetails details = drConfigReconciler.getTrackedResources().iterator().next();
    assertEquals(drConfigName, details.name);
    assertEquals(NAMESPACE, details.namespace);

    // Verify OperatorResource entries were persisted in the database
    List<OperatorResource> allResources = OperatorResource.getAll();
    assertEquals(1, allResources.size());
    assertTrue(
        "OperatorResource name should contain the drconfig name",
        allResources.get(0).getName().contains(drConfigName));
    DrConfig rDrConfig = Serialization.unmarshal(allResources.get(0).getData(), DrConfig.class);
    assertEquals(drConfigName, rDrConfig.getMetadata().getName());
    assertEquals(NAMESPACE, rDrConfig.getMetadata().getNamespace());
    assertEquals("source-universe", rDrConfig.getSpec().getSourceUniverse());
    assertEquals("target-universe", rDrConfig.getSpec().getTargetUniverse());
    assertEquals("my-storage-config", rDrConfig.getSpec().getStorageConfig());
    assertEquals(Arrays.asList("db1", "db2"), rDrConfig.getSpec().getDatabases());
  }

  @Test
  public void testOnDeleteRemovesOperatorResource() throws Exception {
    String drConfigName = "test-drconfig-delete";
    DrConfig drConfig = createDrConfigCr(drConfigName);
    when(operatorUtils.getDrConfigCreateFormFromCr(any(DrConfig.class), eq(scInformer)))
        .thenThrow(new RuntimeException("mock - skip full processing"));

    try {
      drConfigReconciler.onAdd(drConfig);
    } catch (Exception e) {
      // expected
    }
    assertEquals(1, OperatorResource.getAll().size());

    // Delete - drConfig has no status, so handleDelete takes the simple path
    drConfigReconciler.onDelete(drConfig, false);

    assertTrue(
        "Tracked resources should be empty after delete",
        drConfigReconciler.getTrackedResources().isEmpty());
    assertTrue(
        "OperatorResource entries should be removed after delete",
        OperatorResource.getAll().isEmpty());
  }

  @Test
  public void testOnAddWithExistingResourceUuidDoesNotAddToTrackedResources() {
    DrConfig drConfig = createDrConfigCr("existing-drconfig");
    io.yugabyte.operator.v1alpha1.DrConfigStatus status =
        new io.yugabyte.operator.v1alpha1.DrConfigStatus();
    status.setResourceUUID(UUID.randomUUID().toString());
    drConfig.setStatus(status);

    drConfigReconciler.onAdd(drConfig);

    assertTrue(
        "Tracked resources should be empty when already initialized (early return)",
        drConfigReconciler.getTrackedResources().isEmpty());

    // Verify no OperatorResource entries were created in the database
    List<OperatorResource> allResources = OperatorResource.getAll();
    assertTrue(
        "No OperatorResource entries should exist when early return occurs",
        allResources.isEmpty());
  }
}
