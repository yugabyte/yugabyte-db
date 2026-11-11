// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundle;

import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.anyMap;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.KubernetesManager;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.SupportBundleUtil.KubernetesCluster;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.junit.Test;

public class K8sInfoComponentTest extends FakeDBApplication {

  // The tasks name namespaces and releases by zone code, so the support bundle reads the
  // namespaces and Helm values of a zone whose name differs from its code.
  @Test
  public void testNamespacesAndReleasesUseTheZoneCode() {
    Customer customer = ModelFactory.testCustomer();
    Universe universe =
        ModelFactory.createUniverse("k8s-bundle", customer.getId(), CloudType.kubernetes);
    Provider provider =
        Provider.getOrBadRequest(
            UUID.fromString(universe.getUniverseDetails().getPrimaryCluster().userIntent.provider));
    Region region = Region.create(provider, "region-1", "Region 1", "default-image");
    AvailabilityZone az = AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
    KubernetesManagerFactory managerFactory = mock(KubernetesManagerFactory.class);
    KubernetesManager manager = mock(KubernetesManager.class);
    when(managerFactory.getManager()).thenReturn(manager);
    when(manager.getCurrentContext(anyMap())).thenReturn("context-1");
    K8sInfoComponent component =
        new K8sInfoComponent(mock(Config.class), mock(SupportBundleUtil.class), managerFactory);

    List<KubernetesCluster> clusters =
        component.dedupKubernetesClusters(
            Map.of(az.getUuid(), Map.of("KUBECONFIG", "test")),
            true /* isMultiAz */,
            "yb-prefix",
            universe.getUniverseDetails(),
            false /* isReadOnlyUniverseCluster */);

    assertEquals(1, clusters.size());
    assertEquals(Map.of("yb-prefix-az-1", "az-1"), clusters.get(0).namespaceToAzNameMap);
  }
}
