// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.typesafe.config.ConfigFactory;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.supportbundlev2.manifest.SupportBundleV2ManifestLoader;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.BundleDetails.ComponentType;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.modules.CustomObjectMapperModule;
import java.io.File;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import org.junit.Test;

public class SupportBundleClusterScopeTest {

  private static SupportBundleV2ManifestLoader shippedManifestLoader() {
    return new SupportBundleV2ManifestLoader(
        ConfigFactory.parseString(
            String.format("yb.devops.home = \"%s\"", new File("devops").getAbsolutePath())),
        CustomObjectMapperModule.createDefaultMapper());
  }

  private static NodeDetails node(String name, boolean isTserver) {
    NodeDetails node = new NodeDetails();
    node.nodeName = name;
    node.isTserver = isTserver;
    return node;
  }

  private static NodeDetails node(
      String name, NodeState state, boolean isMaster, boolean isTserver) {
    NodeDetails node = node(name, isTserver);
    node.state = state;
    node.isMaster = isMaster;
    return node;
  }

  @Test
  public void clusterMembersComeFromTheShippedManifest() {
    assertEquals(
        Set.of("ysql_system_platform_queries", "yb_admin_commands"),
        new SupportBundleClusterScope(shippedManifestLoader()).clusterMembers());
  }

  /** First by name, so the estimate and the collection agree; YSQL and YCQL need a tserver pod. */
  @Test
  public void chosenNodeIsFirstByNameAmongNodesThatRunTheComponent() {
    List<NodeDetails> pods =
        List.of(node("yb-tserver-1", true), node("yb-master-0", false), node("yb-tserver-0", true));

    assertEquals(
        "yb-tserver-0",
        SupportBundleClusterScope.chooseNode(pods, ComponentType.YSQLComponent).nodeName);
    assertEquals(
        "yb-tserver-0",
        SupportBundleClusterScope.chooseNode(pods, ComponentType.YCQLComponent).nodeName);
    assertEquals(
        "yb-master-0",
        SupportBundleClusterScope.chooseNode(pods, ComponentType.YbAdminComponent).nodeName);
  }

  /**
   * A stopped node and a dedicated master VM can't answer YSQL or YCQL; yb-admin goes to a master.
   * With no Live node, the first that can run it by name, as before.
   */
  @Test
  public void chosenNodePrefersLiveNodesAndMastersForYbAdmin() {
    NodeDetails stopped = node("n1", NodeState.Stopped, true, true);
    NodeDetails tserverOnly = node("n2", NodeState.Live, false, true);
    NodeDetails dedicatedMaster = node("n3", NodeState.Live, true, false);
    List<NodeDetails> nodes =
        List.of(dedicatedMaster, tserverOnly, stopped, node("n4", NodeState.Live, true, true));

    assertEquals(
        "n2", SupportBundleClusterScope.chooseNode(nodes, ComponentType.YSQLComponent).nodeName);
    assertEquals(
        "n2", SupportBundleClusterScope.chooseNode(nodes, ComponentType.YCQLComponent).nodeName);
    assertEquals(
        "n3", SupportBundleClusterScope.chooseNode(nodes, ComponentType.YbAdminComponent).nodeName);
    assertFalse(
        SupportBundleClusterScope.componentRunsOn(dedicatedMaster, ComponentType.YSQLComponent));
    assertTrue(
        SupportBundleClusterScope.componentRunsOn(dedicatedMaster, ComponentType.YbAdminComponent));

    List<NodeDetails> allStopped =
        List.of(
            node("m2", NodeState.Stopped, false, true), node("m1", NodeState.Stopped, true, false));
    assertEquals(
        "m2",
        SupportBundleClusterScope.chooseNode(allStopped, ComponentType.YSQLComponent).nodeName);
    assertEquals(
        "m1",
        SupportBundleClusterScope.chooseNode(allStopped, ComponentType.YbAdminComponent).nodeName);
  }

  @Test
  public void clusterMemberRunsOnlyOnTheChosenNode() {
    SupportBundleClusterScope scope = new SupportBundleClusterScope(shippedManifestLoader());
    UUID bundleUuid = UUID.randomUUID();
    NodeDetails n1 = node("n1", true);
    NodeDetails n2 = node("n2", true);
    scope.register(bundleUuid, List.of(n2, n1));

    assertTrue(scope.runsOn(bundleUuid, ComponentType.YbAdminComponent, "yb_admin_commands", n1));
    assertFalse(scope.runsOn(bundleUuid, ComponentType.YbAdminComponent, "yb_admin_commands", n2));
    assertTrue(scope.runsOn(bundleUuid, ComponentType.YSQLComponent, "ysql_yugabyte_queries", n2));

    scope.forgetBundle(bundleUuid);
    assertTrue(scope.runsOn(bundleUuid, ComponentType.YbAdminComponent, "yb_admin_commands", n2));
    assertNull(scope.chosenNode(bundleUuid, ComponentType.YbAdminComponent));
  }

  /** Collection never fails over scope: without a manifest, every member runs everywhere. */
  @Test
  public void withoutAManifestEveryMemberRunsEverywhere() {
    SupportBundleV2ManifestLoader loader = mock(SupportBundleV2ManifestLoader.class);
    when(loader.load())
        .thenThrow(new PlatformServiceException(INTERNAL_SERVER_ERROR, "manifest missing"));
    SupportBundleClusterScope scope = new SupportBundleClusterScope(loader);
    UUID bundleUuid = UUID.randomUUID();
    NodeDetails n2 = node("n2", true);
    scope.register(bundleUuid, List.of(node("n1", true), n2));

    assertEquals("n1", scope.chosenNode(bundleUuid, ComponentType.YbAdminComponent));
    assertTrue(scope.runsOn(bundleUuid, ComponentType.YbAdminComponent, "yb_admin_commands", n2));
  }

  /** cdc_state and threadz need the node's own tserver; other members run anywhere. */
  @Test
  public void localTserverMembersNeedATserver() {
    NodeDetails dedicatedMaster = node("m1", NodeState.Live, true, false);
    NodeDetails tserver = node("t1", NodeState.Live, false, true);
    for (String member : List.of("collect_cdc_state", "collect_threadz_memz_rpcz_stats")) {
      assertFalse(member, SupportBundleClusterScope.componentRunsOn(dedicatedMaster, member));
      assertTrue(member, SupportBundleClusterScope.componentRunsOn(tserver, member));
      assertTrue(member, SupportBundleClusterScope.componentRunsOn((NodeDetails) null, member));
    }
    for (String member : List.of("YbcLogs", "collect_heap_snapshot", "collect_data_dir_map")) {
      assertTrue(member, SupportBundleClusterScope.componentRunsOn(dedicatedMaster, member));
    }
  }

  /**
   * One check for both: a Kubernetes master pod also skips YbcLogs; a dedicated master VM runs it.
   */
  @Test
  public void scriptComponentsNeedingATserverSkipNodesWithoutOne() {
    Universe k8s = universeOn(CloudType.kubernetes);
    Universe vms = universeOn(CloudType.aws);
    NodeDetails master = node("m1", NodeState.Live, true, false);
    NodeDetails tserver = node("t1", NodeState.Live, false, true);
    for (String local : List.of("collect_cdc_state", "collect_threadz_memz_rpcz_stats")) {
      assertFalse(local, SupportBundleClusterScope.componentRunsOn(k8s, master, local));
      assertFalse(local, SupportBundleClusterScope.componentRunsOn(vms, master, local));
    }
    assertFalse(SupportBundleClusterScope.componentRunsOn(k8s, master, "YbcLogs"));
    assertTrue(SupportBundleClusterScope.componentRunsOn(vms, master, "YbcLogs"));
    assertTrue(SupportBundleClusterScope.componentRunsOn(k8s, master, "UniverseLogs"));
    for (String any : List.of("YbcLogs", "collect_cdc_state", "UniverseLogs")) {
      assertTrue(any, SupportBundleClusterScope.componentRunsOn(k8s, tserver, any));
      assertTrue(any, SupportBundleClusterScope.componentRunsOn(vms, tserver, any));
    }
  }

  private static Universe universeOn(CloudType cloudType) {
    UserIntent intent = new UserIntent();
    intent.providerType = cloudType;
    UniverseDefinitionTaskParams details = new UniverseDefinitionTaskParams();
    details.clusters.add(new Cluster(ClusterType.PRIMARY, intent));
    Universe universe = mock(Universe.class);
    when(universe.getUniverseDetails()).thenReturn(details);
    return universe;
  }
}
