// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.controllers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertSame;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.mockStatic;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.ProviderInitializer;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent.K8SNodeResourceSpec;
import com.yugabyte.yw.models.InstanceType;
import java.util.ArrayList;
import java.util.UUID;
import org.junit.Test;
import org.mockito.MockedStatic;

public class K8sResourceSpecInitializerTest {

  @Test
  public void testSkipsNonKubernetesUniverse() {
    UUID providerUUID = UUID.randomUUID();

    UserIntent ui = new UserIntent();
    TestUtils.getProviderInitializerForTests(ui, providerUUID)
        .setProviderType(CloudType.aws)
        .setInstanceType("c5.xlarge");
    UniverseDefinitionTaskParams params = paramsWith(ui);

    KubernetesUtil.initializeK8sNodeResourceSpecsFromInstanceType(params);

    assertNull(ui.getMasterK8SNodeResourceSpec(providerUUID));
    assertNull(ui.getTserverK8SNodeResourceSpec(providerUUID));
  }

  @Test
  public void testLeavesSpecsUnsetWhenInstanceTypeMissing() {
    K8SNodeResourceSpec existing = resourceSpec(1.5, 3.0);
    UUID providerUUID = UUID.randomUUID();
    UserIntent ui = k8sIntent(providerUUID, null, null, existing, null);
    KubernetesUtil.initializeK8sNodeResourceSpecsFromInstanceType(paramsWith(ui));

    assertNull(ui.getMasterK8SNodeResourceSpec(providerUUID));
    assertSame(existing, ui.getTserverK8SNodeResourceSpec(providerUUID));
  }

  @Test
  public void testSimple() {
    String tserverInstType = "tserver.type";
    String masterInstType = "master.type";
    UUID providerUUID = UUID.randomUUID();
    UserIntent ui = k8sIntent(providerUUID, tserverInstType, masterInstType, null, null);

    try (MockedStatic<InstanceType> instanceTypes = mockStatic(InstanceType.class)) {
      stubInstanceType(instanceTypes, providerUUID, masterInstType, 3.0, 4.0);
      stubInstanceType(instanceTypes, providerUUID, tserverInstType, 1.0, 2.0);
      KubernetesUtil.initializeK8sNodeResourceSpecsFromInstanceType(paramsWith(ui));
    }
    assertResourceSpec(ui.getTserverK8SNodeResourceSpec(providerUUID), 1.0, 2.0);
    assertResourceSpec(ui.getMasterK8SNodeResourceSpec(providerUUID), 3.0, 4.0);
  }

  @Test
  public void testInitializeOverwritesExistingSpecs() {
    String instType = "inst.type";
    UUID providerUUID = UUID.randomUUID();
    UserIntent ui =
        k8sIntent(providerUUID, instType, null, resourceSpec(7.0, 8.0), resourceSpec(5.0, 6.0));

    try (MockedStatic<InstanceType> instanceTypes = mockStatic(InstanceType.class)) {
      stubInstanceType(instanceTypes, providerUUID, instType, 1.0, 2.0);
      KubernetesUtil.initializeK8sNodeResourceSpecsFromInstanceType(paramsWith(ui));
    }
    assertResourceSpec(ui.getTserverK8SNodeResourceSpec(providerUUID), 1.0, 2.0);
    assertResourceSpec(ui.getMasterK8SNodeResourceSpec(providerUUID), 1.0, 2.0);
  }

  @Test
  public void testFillMissingKeepsExistingSpecs() {
    String instType = "inst.type";
    UUID providerUUID = UUID.randomUUID();
    K8SNodeResourceSpec tserverSpec = resourceSpec(7.0, 8.0);
    UserIntent ui = k8sIntent(providerUUID, instType, null, tserverSpec, null);

    try (MockedStatic<InstanceType> instanceTypes = mockStatic(InstanceType.class)) {
      stubInstanceType(instanceTypes, providerUUID, instType, 1.0, 2.0);
      KubernetesUtil.fillMissingK8sNodeResourceSpecsFromInstanceType(paramsWith(ui));
    }
    // Explicit tserver spec is kept, missing master spec is derived from the instance type.
    assertSame(tserverSpec, ui.getTserverK8SNodeResourceSpec(providerUUID));
    assertResourceSpec(ui.getTserverK8SNodeResourceSpec(providerUUID), 7.0, 8.0);
    assertResourceSpec(ui.getMasterK8SNodeResourceSpec(providerUUID), 1.0, 2.0);
  }

  @Test
  public void testFillMissingReadReplicaSkipsMaster() {
    String instType = "inst.type";
    UUID providerUUID = UUID.randomUUID();
    UserIntent ui = k8sIntent(providerUUID, instType, null, null, null);

    try (MockedStatic<InstanceType> instanceTypes = mockStatic(InstanceType.class)) {
      stubInstanceType(instanceTypes, providerUUID, instType, 1.0, 2.0);
      KubernetesUtil.fillMissingK8sNodeResourceSpecsFromInstanceType(ClusterType.ASYNC, ui);
    }
    assertResourceSpec(ui.getTserverK8SNodeResourceSpec(providerUUID), 1.0, 2.0);
    assertNull(ui.getMasterK8SNodeResourceSpec(providerUUID));
  }

  private static void stubInstanceType(
      MockedStatic<InstanceType> instanceTypes,
      UUID providerUUID,
      String code,
      double cpu,
      double memoryGib) {
    InstanceType instanceType = mock(InstanceType.class);
    when(instanceType.getNumCores()).thenReturn(cpu);
    when(instanceType.getMemSizeGB()).thenReturn(memoryGib);
    instanceTypes
        .when(() -> InstanceType.getOrBadRequest(providerUUID, code))
        .thenReturn(instanceType);
  }

  private static UserIntent k8sIntent(
      UUID providerUUID,
      String instanceType,
      String masterInstanceType,
      K8SNodeResourceSpec tserverSpec,
      K8SNodeResourceSpec masterSpec) {
    UserIntent ui = new UserIntent();
    ui.dedicatedNodes = true;
    ProviderInitializer providerInitializer =
        TestUtils.getProviderInitializerForTests(ui, providerUUID)
            .setProviderType(CloudType.kubernetes)
            .setInstanceType(instanceType);
    if (masterInstanceType != null) {
      providerInitializer.setMasterInstanceType(masterInstanceType);
    } else {
      providerInitializer.setMasterInstanceType(instanceType);
    }
    if (tserverSpec != null) {
      providerInitializer.setNodeResourceSpec(tserverSpec, UniverseTaskBase.ServerType.TSERVER);
    }
    if (masterSpec != null) {
      providerInitializer.setNodeResourceSpec(masterSpec, UniverseTaskBase.ServerType.MASTER);
    }
    return ui;
  }

  private static UniverseDefinitionTaskParams paramsWith(UserIntent ui) {
    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.clusters = new ArrayList<>();
    params.clusters.add(new Cluster(ClusterType.PRIMARY, ui));
    return params;
  }

  private static K8SNodeResourceSpec resourceSpec(double cpu, double memoryGib) {
    K8SNodeResourceSpec spec = new K8SNodeResourceSpec();
    spec.cpuCoreCount = cpu;
    spec.memoryGib = memoryGib;
    return spec;
  }

  private static void assertResourceSpec(K8SNodeResourceSpec spec, double cpu, double memoryGib) {
    assertNotNull(spec);
    assertEquals(cpu, spec.cpuCoreCount, 0.001);
    assertEquals(memoryGib, spec.memoryGib, 0.001);
  }
}
