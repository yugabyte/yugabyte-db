package com.yugabyte.yw.controllers.handlers;

import static org.junit.Assert.assertThrows;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import java.util.HashMap;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(JUnitParamsRunner.class)
public class OperatorResourceMigrateHandlerTest extends FakeDBApplication {
  // Resources
  private Universe defaultUniverse;
  private Customer defaultCustomer;

  // Handler we are testing
  private OperatorResourceMigrateHandler handler;

  // Mock Dependencies
  private RuntimeConfGetter mockConfGetter;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUniverse =
        ModelFactory.createUniverse(
            "test-k8s-uni", defaultCustomer.getId(), Common.CloudType.kubernetes);
    mockConfGetter = mock(RuntimeConfGetter.class);
    handler = new OperatorResourceMigrateHandler(mockConfGetter);
  }

  @Test
  public void testPrecheckSuccess() {
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)).thenReturn(true);
    handler.precheckUniverseImport(defaultUniverse);
    // Implement your test logic here
  }

  @Test
  public void testOperatorDisabled() {
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)).thenReturn(false);
    assertThrows(
        PlatformServiceException.class, () -> handler.precheckUniverseImport(defaultUniverse));
  }

  @Test
  public void testNonKubernetesUniverse() {
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)).thenReturn(true);
    Universe nonK8sUniverse =
        ModelFactory.createUniverse(
            "test-non-k8s-uni", defaultCustomer.getId(), Common.CloudType.onprem);
    assertThrows(
        PlatformServiceException.class, () -> handler.precheckUniverseImport(nonK8sUniverse));
  }

  @Test
  public void testReadOnlyClusters() {
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)).thenReturn(true);
    defaultUniverse.getUniverseDetails().upsertCluster(null, null, null, UUID.randomUUID());
    assertThrows(
        PlatformServiceException.class, () -> handler.precheckUniverseImport(defaultUniverse));
  }

  @Test
  public void testAzOverrides() {
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)).thenReturn(true);
    UniverseDefinitionTaskParams details = defaultUniverse.getUniverseDetails();
    details.getPrimaryCluster().userIntent.azOverrides = new HashMap<>();
    details.getPrimaryCluster().userIntent.azOverrides.put("az1", "override");
    // Add an AZ override to trigger the exception
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        u -> {
          u.getUniverseDetails().getPrimaryCluster().userIntent.azOverrides = new HashMap<>();
          u.getUniverseDetails().getPrimaryCluster().userIntent.azOverrides.put("az1", "override");
        });
    assertThrows(
        PlatformServiceException.class, () -> handler.precheckUniverseImport(defaultUniverse));
  }
}
