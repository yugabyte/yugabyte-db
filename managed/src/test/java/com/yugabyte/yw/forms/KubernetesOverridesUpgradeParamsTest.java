// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import io.fabric8.kubernetes.api.model.Container;
import io.fabric8.kubernetes.api.model.ContainerBuilder;
import io.fabric8.kubernetes.api.model.Pod;
import io.fabric8.kubernetes.api.model.PodBuilder;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;

/**
 * A Kubernetes overrides upgrade with no override changed is accepted when it would install or
 * remove the node_exporter sidecar on a master or tserver pod, and rejected otherwise. Each test
 * universe has one master and one tserver node.
 */
public class KubernetesOverridesUpgradeParamsTest extends FakeDBApplication {

  private static final String KEY = UniverseConfKeys.k8sNodeExporterEnabled.getKey();
  private static final String SUPPORTED_VERSION =
      KubernetesUtil.MIN_VERSION_NODE_EXPORTER_SIDECAR_PREVIEW;
  private static final String UNSUPPORTED_VERSION = "2.29.0.0-b1";

  private Customer customer;
  private AvailabilityZone az;
  private PlacementInfo placement;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    Region region =
        Region.create(
            ModelFactory.kubernetesProvider(customer), "region-1", "region-1", "yb-image-1");
    az = AvailabilityZone.createOrThrow(region, "az-1", "az-1", "subnet-1");
    CloudInfoInterface.setCloudProviderInfoFromConfig(az, ImmutableMap.of("KUBECONFIG", "test"));
    az.save();
    placement = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az.getUuid(), placement);
  }

  @Test
  public void testNoChangeAcceptedToInstallSidecar() {
    Universe u = k8sUniverse(SUPPORTED_VERSION);
    RuntimeConfigEntry.upsert(u, KEY, "true");
    mockServerPods(false /* sidecar */);
    noChangeParams().verifyParams(u, true);
    verify(mockKubernetesManager)
        .getPodObject(eq(ImmutableMap.of("KUBECONFIG", "test")), eq("ns-1"), any());
  }

  @Test
  public void testNoChangeAcceptedToRemoveSidecar() {
    Universe u = k8sUniverse(SUPPORTED_VERSION);
    RuntimeConfigEntry.upsert(u, KEY, "false");
    mockServerPods(true /* sidecar */);
    noChangeParams().verifyParams(u, true);
  }

  @Test
  public void testNoChangeAcceptedToRemoveSidecarWhenKeyOffGlobally() {
    Universe u = k8sUniverse(SUPPORTED_VERSION);
    RuntimeConfigEntry.upsertGlobal(KEY, "false");
    mockServerPods(true /* sidecar */);
    noChangeParams().verifyParams(u, true);
  }

  @Test
  public void testNoChangeRejectedWhenSidecarAlreadyInstalled() {
    Universe u = k8sUniverse(SUPPORTED_VERSION);
    RuntimeConfigEntry.upsert(u, KEY, "true");
    mockServerPods(true /* sidecar */);
    assertNoOverridesModified(u);
  }

  @Test
  public void testNoChangeRejectedWhenSidecarAlreadyRemoved() {
    Universe u = k8sUniverse(SUPPORTED_VERSION);
    RuntimeConfigEntry.upsert(u, KEY, "false");
    mockServerPods(false /* sidecar */);
    assertNoOverridesModified(u);
  }

  @Test
  public void testNoChangeRejectedBelowSidecarVersion() {
    Universe u = k8sUniverse(UNSUPPORTED_VERSION);
    RuntimeConfigEntry.upsert(u, KEY, "true");
    mockServerPods(false /* sidecar */);
    assertNoOverridesModified(u);
  }

  @Test
  public void testNoChangeRejectedWhenPodCannotBeRead() {
    Universe u = k8sUniverse(SUPPORTED_VERSION);
    RuntimeConfigEntry.upsert(u, KEY, "true");
    when(mockKubernetesManager.getPodObject(any(), any(), any()))
        .thenThrow(new RuntimeException("kubectl failed"));
    assertNoOverridesModified(u);
  }

  @Test
  public void testChangedOverrideAcceptedWithoutReadingPods() {
    Universe u = k8sUniverse(SUPPORTED_VERSION);
    KubernetesOverridesUpgradeParams params = noChangeParams();
    params.universeOverrides = "tserver:\n  podLabels:\n    env: test";
    params.verifyParams(u, true);
    verify(mockKubernetesManager, never()).getPodObject(any(), any(), any());
  }

  private void assertNoOverridesModified(Universe u) {
    PlatformServiceException e =
        assertThrows(PlatformServiceException.class, () -> noChangeParams().verifyParams(u, true));
    assertEquals(BAD_REQUEST, e.getHttpStatus());
    assertEquals("No kubernetes overrides modified.", e.getUserVisibleMessage());
  }

  // The same (empty) overrides the universe already has.
  private KubernetesOverridesUpgradeParams noChangeParams() {
    return new KubernetesOverridesUpgradeParams();
  }

  private void mockServerPods(boolean sidecar) {
    when(mockKubernetesManager.getPodObject(any(), any(), eq("yb-master-0")))
        .thenReturn(pod("yb-master", sidecar));
    when(mockKubernetesManager.getPodObject(any(), any(), eq("yb-tserver-0")))
        .thenReturn(pod("yb-tserver", sidecar));
  }

  private static Pod pod(String server, boolean sidecar) {
    List<Container> containers = new ArrayList<>();
    containers.add(new ContainerBuilder().withName(server).build());
    if (sidecar) {
      containers.add(new ContainerBuilder().withName("yb-node-exporter").build());
    }
    return new PodBuilder()
        .withNewMetadata()
        .withName(server + "-0")
        .endMetadata()
        .withNewSpec()
        .withContainers(containers)
        .endSpec()
        .build();
  }

  private NodeDetails node(String server, UUID placementUuid) {
    NodeDetails node = new NodeDetails();
    node.nodeName = server + "-0";
    node.isMaster = server.equals("yb-master");
    node.isTserver = server.equals("yb-tserver");
    node.state = NodeDetails.NodeState.Live;
    node.azUuid = az.getUuid();
    node.placementUuid = placementUuid;
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.private_ip = server + "-0.ns-1.svc.cluster.local";
    node.cloudInfo.kubernetesNamespace = "ns-1";
    node.cloudInfo.kubernetesPodName = server + "-0";
    return node;
  }

  private Universe k8sUniverse(String version) {
    Universe u =
        ModelFactory.createK8sUniverseCustomCores(
            "k8s-" + UUID.randomUUID(),
            UUID.randomUUID(),
            customer.getId(),
            placement,
            null,
            false,
            1);
    return Universe.saveDetails(
        u.getUniverseUUID(),
        universe -> {
          UniverseDefinitionTaskParams details = universe.getUniverseDetails();
          details.getPrimaryCluster().userIntent.ybSoftwareVersion = version;
          UUID placementUuid = details.getPrimaryCluster().uuid;
          details.nodeDetailsSet =
              new HashSet<>(
                  List.of(node("yb-master", placementUuid), node("yb-tserver", placementUuid)));
          universe.setUniverseDetails(details);
        });
  }
}
