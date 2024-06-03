// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.google.common.net.HostAndPort;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.yb.client.ListTabletServersResponse;
import org.yb.client.YBClient;
import play.libs.Json;
import play.mvc.Result;

public class TabletServerControllerTest extends FakeDBApplication {
  private TabletServerController tabletController;
  private YBClient mockClient;
  private ListTabletServersResponse mockResponse;
  private final HostAndPort testHostAndPort = HostAndPort.fromString("0.0.0.0").withDefaultPort(11);

  @Before
  public void setUp() throws Exception {
    mockClient = mock(YBClient.class);
    mockResponse = mock(ListTabletServersResponse.class);
    when(mockClient.listTabletServers()).thenReturn(mockResponse);
    when(mockClient.getLeaderMasterHostAndPort()).thenReturn(testHostAndPort);
    when(mockService.getClient(any())).thenReturn(mockClient);
    when(mockService.getClient(any(), any())).thenReturn(mockClient);
    tabletController = new TabletServerController(mockApiHelper);
    when(mockApiHelper.getRequest(anyString())).thenReturn(Json.newObject());
  }

  @Test
  public void testListTabletServersWrapperSuccess() {
    Customer customer = ModelFactory.testCustomer();
    Universe u1 = createUniverse(customer.getId());
    u1 = Universe.saveDetails(u1.getUniverseUUID(), ApiUtils.mockUniverseUpdater());
    Result r = tabletController.listTabletServers(customer.getUuid(), u1.getUniverseUUID());
    assertEquals(200, r.status());
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testListTabletServersWrapperFailure() {
    when(mockApiHelper.getRequest(anyString())).thenThrow(new RuntimeException("Unknown Error"));
    Customer customer = ModelFactory.testCustomer();
    Universe u1 = createUniverse(customer.getId());
    u1 = Universe.saveDetails(u1.getUniverseUUID(), ApiUtils.mockUniverseUpdater());
    UUID universeUUID = u1.getUniverseUUID();
    Result result =
        assertPlatformException(
            () -> tabletController.listTabletServers(customer.getUuid(), universeUUID));
    assertEquals(500, result.status());
    assertAuditEntry(0, customer.getUuid());
  }
}
