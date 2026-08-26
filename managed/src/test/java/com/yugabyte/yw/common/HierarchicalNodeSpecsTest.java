// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.forms.HierarchicalNodesSpec;
import com.yugabyte.yw.forms.HierarchicalNodesSpec.AzNodesSpec;
import com.yugabyte.yw.forms.HierarchicalNodesSpec.NodeSpec;
import com.yugabyte.yw.forms.HierarchicalNodesSpec.RegionNodesSpec;
import com.yugabyte.yw.forms.HierarchicalNodesSpec.RootNodesSpec;
import com.yugabyte.yw.forms.HierarchicalNodesSpec.TraversePath;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.ProxyConfig;
import java.util.Arrays;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.BlockJUnit4ClassRunner;
import play.libs.Json;

@RunWith(BlockJUnit4ClassRunner.class)
public class HierarchicalNodeSpecsTest {

  @Test
  public void testGetSpec() {
    RootNodesSpec root =
        RootNodesSpec.builder()
            .tserverSpecification(NodeSpec.builder().instanceType("tserverType").build())
            .masterSpecification(NodeSpec.builder().instanceType("masterType").build())
            .build()
            .addRegion(
                RegionNodesSpec.builder()
                    .regionCode("r1")
                    .tserverSpecification(NodeSpec.builder().instanceType("r1Tserver").build())
                    .build())
            .addRegion(
                RegionNodesSpec.builder()
                    .regionCode("r2")
                    .build()
                    .addZone(
                        AzNodesSpec.builder()
                            .azCode("r2z2")
                            .tserverSpecification(
                                NodeSpec.builder().instanceType("r2z2TserverType").build())
                            .build()))
            .addRegion(
                RegionNodesSpec.builder()
                    .regionCode("r3")
                    .masterSpecification(NodeSpec.builder().instanceType("r3MasterType").build())
                    .build()
                    .addZone(
                        AzNodesSpec.builder()
                            .azCode("r3z3")
                            .tserverSpecification(
                                NodeSpec.builder().instanceType("r3z3TserverType").build())
                            .build()));

    TraversePath traversePath = TraversePath.topLevel();
    assertEquals(
        "tserverType",
        HierarchicalNodesSpec.getSpecification(
                root, UniverseTaskBase.ServerType.TSERVER, traversePath)
            .getNodeSpec()
            .getInstanceType());
    assertEquals(
        "masterType",
        HierarchicalNodesSpec.getSpecification(
                root, UniverseTaskBase.ServerType.MASTER, traversePath)
            .getNodeSpec()
            .getInstanceType());

    traversePath = TraversePath.regionLevel("r1");
    assertEquals(
        "r1Tserver",
        HierarchicalNodesSpec.getSpecification(
                root, UniverseTaskBase.ServerType.TSERVER, traversePath)
            .getNodeSpec()
            .getInstanceType());
    assertEquals(
        "masterType",
        HierarchicalNodesSpec.getSpecification(
                root, UniverseTaskBase.ServerType.MASTER, traversePath)
            .getNodeSpec()
            .getInstanceType());
    traversePath = TraversePath.azLevel("r1", "r1z1");
    assertEquals(
        "r1Tserver",
        HierarchicalNodesSpec.getSpecification(
                root, UniverseTaskBase.ServerType.TSERVER, traversePath)
            .getNodeSpec()
            .getInstanceType());
    assertEquals(
        "masterType",
        HierarchicalNodesSpec.getSpecification(
                root, UniverseTaskBase.ServerType.MASTER, traversePath)
            .getNodeSpec()
            .getInstanceType());

    traversePath = TraversePath.regionLevel("r2");
    assertEquals(
        "tserverType",
        HierarchicalNodesSpec.getSpecification(
                root, UniverseTaskBase.ServerType.TSERVER, traversePath)
            .getNodeSpec()
            .getInstanceType());
    assertEquals(
        "masterType",
        HierarchicalNodesSpec.getSpecification(
                root, UniverseTaskBase.ServerType.MASTER, traversePath)
            .getNodeSpec()
            .getInstanceType());

    traversePath = TraversePath.azLevel("r2", "r2z2");
    assertEquals(
        "r2z2TserverType",
        HierarchicalNodesSpec.getSpecification(
                root, UniverseTaskBase.ServerType.TSERVER, traversePath)
            .getNodeSpec()
            .getInstanceType());
    assertEquals(
        "masterType",
        HierarchicalNodesSpec.getSpecification(
                root, UniverseTaskBase.ServerType.MASTER, traversePath)
            .getNodeSpec()
            .getInstanceType());

    traversePath = TraversePath.regionLevel("r3");
    assertEquals(
        "tserverType",
        HierarchicalNodesSpec.getSpecification(
                root, UniverseTaskBase.ServerType.TSERVER, traversePath)
            .getNodeSpec()
            .getInstanceType());
    assertEquals(
        "r3MasterType",
        HierarchicalNodesSpec.getSpecification(
                root, UniverseTaskBase.ServerType.MASTER, traversePath)
            .getNodeSpec()
            .getInstanceType());

    traversePath = TraversePath.azLevel("r3", "r3z3");
    assertEquals(
        "r3z3TserverType",
        HierarchicalNodesSpec.getSpecification(
                root, UniverseTaskBase.ServerType.TSERVER, traversePath)
            .getNodeSpec()
            .getInstanceType());
    assertEquals(
        "r3MasterType",
        HierarchicalNodesSpec.getSpecification(
                root, UniverseTaskBase.ServerType.MASTER, traversePath)
            .getNodeSpec()
            .getInstanceType());
  }

  @Test
  public void testAllPropertiesAreInherited() {
    DeviceInfo rootInfo = new DeviceInfo();
    rootInfo.numVolumes = 1;
    rootInfo.volumeSize = 100;
    rootInfo.storageType = PublicCloudConstants.StorageType.GP2;
    rootInfo.storageClass = "standard";
    rootInfo.diskIops = 1000;
    rootInfo.throughput = 5000;

    ProxyConfig rootProxyConfig = new ProxyConfig();
    rootProxyConfig.setHttpsProxy("https");
    rootProxyConfig.setHttpProxy("http");
    rootProxyConfig.setNoProxyList(Arrays.asList("1", "2"));

    DeviceInfo overridenInfo = new DeviceInfo();
    overridenInfo.numVolumes = 2;
    overridenInfo.volumeSize = 200;
    overridenInfo.storageType = PublicCloudConstants.StorageType.GP3;
    overridenInfo.storageClass = "standard2";
    overridenInfo.diskIops = 2000;
    overridenInfo.throughput = 6000;

    ProxyConfig overridenProxyConfig = new ProxyConfig();
    overridenProxyConfig.setHttpsProxy("https2");
    overridenProxyConfig.setHttpProxy("http2");
    overridenProxyConfig.setNoProxyList(Arrays.asList("2", "3"));

    RootNodesSpec root =
        RootNodesSpec.builder()
            .tserverSpecification(
                NodeSpec.builder()
                    .backupProxyConfig(rootProxyConfig)
                    .instanceType("tserverType")
                    .cgroupSize(10)
                    .deviceInfo(rootInfo)
                    .build())
            .build()
            .addRegion(
                RegionNodesSpec.builder()
                    .regionCode("r1")
                    .tserverSpecification(
                        NodeSpec.builder()
                            .backupProxyConfig(overridenProxyConfig)
                            .instanceType("overridenTserverType")
                            .cgroupSize(20)
                            .deviceInfo(overridenInfo)
                            .build())
                    .build());

    TraversePath traversePath = TraversePath.topLevel();
    HierarchicalNodesSpec.NodeSpecInfo nodeSpecInfo =
        HierarchicalNodesSpec.getSpecification(
            root, UniverseTaskBase.ServerType.TSERVER, traversePath);
    NodeSpec nodeSpec = nodeSpecInfo.getNodeSpec();
    assertEquals("tserverType", nodeSpec.getInstanceType());
    assertEquals(10, nodeSpec.getCgroupSize().intValue());
    assertEquals(Json.toJson(rootInfo), Json.toJson(nodeSpec.getDeviceInfo()));
    assertEquals(
        Json.toJson(rootProxyConfig),
        Json.toJson(nodeSpecInfo.getNodeSpec().getBackupProxyConfig()));

    traversePath = TraversePath.regionLevel("r1");

    nodeSpecInfo =
        HierarchicalNodesSpec.getSpecification(
            root, UniverseTaskBase.ServerType.TSERVER, traversePath);
    nodeSpec = nodeSpecInfo.getNodeSpec();

    assertEquals("overridenTserverType", nodeSpec.getInstanceType());
    assertEquals(20, nodeSpec.getCgroupSize().intValue());
    assertEquals(Json.toJson(overridenInfo), Json.toJson(nodeSpec.getDeviceInfo()));
    assertEquals(
        Json.toJson(overridenProxyConfig),
        Json.toJson(nodeSpecInfo.getNodeSpec().getBackupProxyConfig()));

    DeviceInfo partly = new DeviceInfo();
    partly.numVolumes = 3;
    partly.volumeSize = 300;
    partly.storageClass = null;
    root.getRegionNodesSpecs().stream()
        .filter(r -> "r1".equals(r.getRegionCode()))
        .findFirst()
        .orElseThrow()
        .getTserverSpecification()
        .setDeviceInfo(partly);

    nodeSpecInfo =
        HierarchicalNodesSpec.getSpecification(
            root, UniverseTaskBase.ServerType.TSERVER, traversePath);
    nodeSpec = nodeSpecInfo.getNodeSpec();
    assertEquals(partly.numVolumes.intValue(), nodeSpec.getDeviceInfo().numVolumes.intValue());
    assertEquals(partly.volumeSize.intValue(), nodeSpec.getDeviceInfo().volumeSize.intValue());
    assertEquals(rootInfo.throughput.intValue(), nodeSpec.getDeviceInfo().throughput.intValue());
    assertEquals(rootInfo.diskIops.intValue(), nodeSpec.getDeviceInfo().diskIops.intValue());
    assertEquals(rootInfo.storageClass, nodeSpec.getDeviceInfo().storageClass);
  }

  @Test
  public void tesSimpleUpdate() {
    RootNodesSpec root =
        RootNodesSpec.builder()
            .tserverSpecification(NodeSpec.builder().instanceType("tserverType").build())
            .build()
            .addRegion(
                RegionNodesSpec.builder()
                    .regionCode("r1")
                    .tserverSpecification(NodeSpec.builder().instanceType("r1Tserver").build())
                    .build()
                    .addZone(
                        AzNodesSpec.builder()
                            .azCode("r1z1")
                            .tserverSpecification(
                                NodeSpec.builder()
                                    .instanceType("r1z1TserverType")
                                    .cgroupSize(100)
                                    .build())
                            .build()));

    RootNodesSpec override =
        RootNodesSpec.builder()
            .tserverSpecification(NodeSpec.builder().instanceType("tserverType1").build())
            .build()
            .addRegion(
                RegionNodesSpec.builder()
                    .regionCode("r1")
                    .tserverSpecification(NodeSpec.builder().instanceType("r1Tserver1").build())
                    .masterSpecification(NodeSpec.builder().instanceType("r1Master1").build())
                    .build())
            .addRegion(
                RegionNodesSpec.builder()
                    .regionCode("r2")
                    .tserverSpecification(NodeSpec.builder().instanceType("r2Tserver1").build())
                    .build());

    HierarchicalNodesSpec.merge(
        root,
        override,
        mergeItem ->
            mergeItem.getCurrent().setInstanceType(mergeItem.getSource().getInstanceType()));

    NodeSpec nodeSpec = root.getTserverSpecification();
    assertEquals("tserverType1", nodeSpec.getInstanceType());

    nodeSpec = root.getMasterSpecification();
    assertNull(nodeSpec);

    TraversePath traversePath = TraversePath.regionLevel("r1");
    nodeSpec =
        HierarchicalNodesSpec.getSpecification(
                root, UniverseTaskBase.ServerType.TSERVER, traversePath)
            .getNodeSpec();
    assertEquals("r1Tserver1", nodeSpec.getInstanceType());

    nodeSpec =
        HierarchicalNodesSpec.getSpecification(
                root, UniverseTaskBase.ServerType.MASTER, traversePath)
            .getNodeSpec();
    assertEquals("r1Master1", nodeSpec.getInstanceType());

    traversePath = TraversePath.regionLevel("r2");
    nodeSpec =
        HierarchicalNodesSpec.getSpecification(
                root, UniverseTaskBase.ServerType.TSERVER, traversePath)
            .getNodeSpec();
    assertEquals("r2Tserver1", nodeSpec.getInstanceType());

    nodeSpec =
        HierarchicalNodesSpec.getSpecification(
                root, UniverseTaskBase.ServerType.MASTER, traversePath)
            .getNodeSpec();
    assertTrue(nodeSpec.isEmpty());

    traversePath = TraversePath.azLevel("r1", "r1z1");
    nodeSpec =
        HierarchicalNodesSpec.getSpecification(
                root, UniverseTaskBase.ServerType.TSERVER, traversePath)
            .getNodeSpec();
    // Should be the value from r1, since we erased it at az level.
    assertEquals("r1Tserver1", nodeSpec.getInstanceType());
    // Cgroup size will remain at az level.
    assertEquals(100, nodeSpec.getCgroupSize().intValue());

    nodeSpec =
        HierarchicalNodesSpec.getSpecification(
                root, UniverseTaskBase.ServerType.MASTER, traversePath)
            .getNodeSpec();
    assertEquals("r1Master1", nodeSpec.getInstanceType());
  }

  @Test
  public void testValidateRootNodesSpec_valid() {
    RootNodesSpec root =
        RootNodesSpec.builder()
            .tserverSpecification(NodeSpec.builder().instanceType("tserverType").build())
            .build()
            .addRegion(
                RegionNodesSpec.builder()
                    .regionCode("r1")
                    .build()
                    .addZone(
                        AzNodesSpec.builder()
                            .azCode("r1z1")
                            .tserverSpecification(
                                NodeSpec.builder().instanceType("r1z1Type").build())
                            .build()))
            .addRegion(RegionNodesSpec.builder().regionCode("r2").build());
    root.validate();
  }

  @Test
  public void testValidateRootNodesSpec_noRegions() {
    RootNodesSpec root =
        RootNodesSpec.builder()
            .tserverSpecification(NodeSpec.builder().instanceType("tserverType").build())
            .build();
    root.validate();
  }

  @Test
  public void testValidateRootNodesSpec_duplicateRegionCode() {
    RootNodesSpec root =
        RootNodesSpec.builder()
            .tserverSpecification(NodeSpec.builder().instanceType("tserverType").build())
            .regionNodesSpecs(
                Arrays.asList(
                    RegionNodesSpec.builder().regionCode("r1").build(),
                    RegionNodesSpec.builder().regionCode("r1").build()))
            .build();
    expectValidationFailure(root, "RootNodesSpec: found duplicate code: r1");
  }

  @Test
  public void testValidateRootNodesSpec_nullRegion() {
    RootNodesSpec root =
        RootNodesSpec.builder()
            .tserverSpecification(NodeSpec.builder().instanceType("tserverType").build())
            .regionNodesSpecs(Arrays.asList((RegionNodesSpec) null))
            .build();
    expectValidationFailure(root, "RootNodesSpec: null children are not allowed");
  }

  @Test
  public void testValidateRootNodesSpec_emptyRegionCode() {
    RootNodesSpec root =
        RootNodesSpec.builder()
            .tserverSpecification(NodeSpec.builder().instanceType("tserverType").build())
            .regionNodesSpecs(Arrays.asList(RegionNodesSpec.builder().build()))
            .build();
    expectValidationFailure(root, "RootNodesSpec: empty code is not allowed");
  }

  @Test
  public void testValidateRootNodesSpec_duplicateAzCode() {
    RootNodesSpec root =
        RootNodesSpec.builder()
            .tserverSpecification(NodeSpec.builder().instanceType("tserverType").build())
            .regionNodesSpecs(
                Arrays.asList(
                    RegionNodesSpec.builder()
                        .regionCode("r1")
                        .azNodesSpecs(
                            Arrays.asList(
                                AzNodesSpec.builder().azCode("z1").build(),
                                AzNodesSpec.builder().azCode("z1").build()))
                        .build()))
            .build();
    expectValidationFailure(root, "RegionNodesSpec(r1): found duplicate code: z1");
  }

  @Test
  public void testValidateRootNodesSpec_nullAz() {
    RootNodesSpec root =
        RootNodesSpec.builder()
            .tserverSpecification(NodeSpec.builder().instanceType("tserverType").build())
            .regionNodesSpecs(
                Arrays.asList(
                    RegionNodesSpec.builder()
                        .regionCode("r1")
                        .azNodesSpecs(Arrays.asList((AzNodesSpec) null))
                        .build()))
            .build();
    expectValidationFailure(root, "RegionNodesSpec(r1): null children are not allowed");
  }

  private void expectValidationFailure(RootNodesSpec root, String expectedMessage) {
    try {
      root.validate();
      fail("Expected PlatformServiceException");
    } catch (PlatformServiceException e) {
      assertEquals(BAD_REQUEST, e.getHttpStatus());
      assertEquals(expectedMessage, e.getMessage());
    }
  }
}
