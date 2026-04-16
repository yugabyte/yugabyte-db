package com.yugabyte.yw.api.v2;

import static org.junit.Assert.assertThrows;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import api.v2.handlers.UniverseManagementHandler;
import api.v2.models.UniverseOperatorImportReq;
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
import org.mockito.InjectMocks;
import org.mockito.MockitoAnnotations;
import play.mvc.Http.Request;
import play.test.Helpers;

@RunWith(JUnitParamsRunner.class)
public class UniverseManagementHandlerOperatorImportTest extends FakeDBApplication {
  // Resources
  private Universe defaultUniverse;
  private Customer defaultCustomer;

  // Handler we are testing
  @InjectMocks private UniverseManagementHandler handler;

  // Mock Dependencies
  private RuntimeConfGetter mockConfGetter;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUniverse =
        ModelFactory.createUniverse(
            "test-k8s-uni", defaultCustomer.getId(), Common.CloudType.kubernetes);
    mockConfGetter = mock(RuntimeConfGetter.class);
    MockitoAnnotations.openMocks(this);
  }

  private Request createRequest() {
    return Helpers.fakeRequest(
            "POST",
            "/customers/"
                + defaultCustomer.getUuid()
                + "/universes/"
                + defaultUniverse.getUniverseUUID()
                + "/operator-import/precheck")
        .build();
  }

  @Test
  public void testPrecheckSuccess() {
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)).thenReturn(true);
    UniverseOperatorImportReq req = new UniverseOperatorImportReq();
    req.setNamespace("namespace");
    handler.precheckOperatorImportUniverse(
        createRequest(), defaultCustomer.getUuid(), defaultUniverse.getUniverseUUID(), req);
    // Implement your test logic here
  }

  @Test
  public void testOperatorDisabled() {
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)).thenReturn(false);
    UniverseOperatorImportReq req = new UniverseOperatorImportReq();
    req.setNamespace("namespace");
    assertThrows(
        PlatformServiceException.class,
        () ->
            handler.precheckOperatorImportUniverse(
                createRequest(),
                defaultCustomer.getUuid(),
                defaultUniverse.getUniverseUUID(),
                req));
  }

  @Test
  public void testNonKubernetesUniverse() {
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)).thenReturn(true);
    Universe nonK8sUniverse =
        ModelFactory.createUniverse(
            "test-non-k8s-uni", defaultCustomer.getId(), Common.CloudType.onprem);
    UniverseOperatorImportReq req = new UniverseOperatorImportReq();
    req.setNamespace("namespace");
    assertThrows(
        PlatformServiceException.class,
        () ->
            handler.precheckOperatorImportUniverse(
                createRequest(), defaultCustomer.getUuid(), nonK8sUniverse.getUniverseUUID(), req));
  }

  @Test
  public void testReadOnlyClusters() {
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled)).thenReturn(true);
    defaultUniverse.getUniverseDetails().upsertCluster(null, null, UUID.randomUUID());
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        u -> {
          UniverseDefinitionTaskParams details = u.getUniverseDetails();
          details.upsertCluster(null, null, UUID.randomUUID());
          u.setUniverseDetails(details);
        });
    UniverseOperatorImportReq req = new UniverseOperatorImportReq();
    req.setNamespace("namespace");
    assertThrows(
        PlatformServiceException.class,
        () ->
            handler.precheckOperatorImportUniverse(
                createRequest(),
                defaultCustomer.getUuid(),
                defaultUniverse.getUniverseUUID(),
                req));
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
    UniverseOperatorImportReq req = new UniverseOperatorImportReq();
    req.setNamespace("namespace");
    assertThrows(
        PlatformServiceException.class,
        () ->
            handler.precheckOperatorImportUniverse(
                createRequest(),
                defaultCustomer.getUuid(),
                defaultUniverse.getUniverseUUID(),
                req));
  }
}
