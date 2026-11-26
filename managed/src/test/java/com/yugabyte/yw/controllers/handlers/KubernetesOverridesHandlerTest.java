// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.controllers.handlers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.KubernetesManager;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.util.Map;
import java.util.Set;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;

public class KubernetesOverridesHandlerTest extends FakeDBApplication {

  private KubernetesOverridesHandler handler;
  private Provider provider;
  private AvailabilityZone az;

  @Before
  public void setUp() {
    Customer customer = ModelFactory.testCustomer();
    provider = ModelFactory.kubernetesProvider(customer);
    Region region = Region.create(provider, "region-1", "Region 1", "default-image");
    az = AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
    az.updateConfig(Map.of("KUBECONFIG", "test-kubeconfig"));
    az.save();
    KubernetesManager kubernetesManager = mock(KubernetesManager.class);
    when(kubernetesManager.validateOverrides(any(), any(), any(), any(), any(), any()))
        .thenReturn(Set.of());
    KubernetesManagerFactory factory = mock(KubernetesManagerFactory.class);
    when(factory.getManager()).thenReturn(kubernetesManager);
    handler = new KubernetesOverridesHandler(factory);
  }

  private JsonNode validate(String universeOverrides, boolean fipsEnabled) {
    UserIntent userIntent = new UserIntent();
    userIntent.ybSoftwareVersion = "2.31.0.0-b1";
    userIntent.universeOverrides = universeOverrides;
    Cluster cluster = new Cluster(ClusterType.PRIMARY, userIntent);
    PlacementInfo placementInfo = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az.getUuid(), placementInfo);
    cluster.placementInfo = placementInfo;
    UniverseConfigureTaskParams taskParams = new UniverseConfigureTaskParams();
    taskParams.clusters.add(cluster);
    taskParams.nodePrefix = "yb-test";
    taskParams.fipsEnabled = fipsEnabled;
    return Json.toJson(handler.validateKubernetesOverrides(taskParams)).get("overridesErrors");
  }

  @Test
  public void testReportsOverridesTheFipsChecksRefuse() {
    JsonNode errors =
        validate("gflags:\n  tserver:\n    openssl_require_fips: \"false\"\n", true /* fips */);
    assertEquals(errors.toString(), 1, errors.size());
    assertEquals(
        "A FIPS enabled universe needs openssl_require_fips set to true or left unset, but the"
            + " Kubernetes universe overrides (gflags.tserver) set it to false.",
        errors.get(0).get("errorString").asText());
  }

  @Test
  public void testAcceptsTheSameOverridesWithoutFips() {
    JsonNode errors =
        validate("gflags:\n  tserver:\n    openssl_require_fips: \"false\"\n", false /* fips */);
    assertTrue(errors.toString(), errors.isEmpty());
  }
}
