package com.yugabyte.yw.api.v2;

import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;

import com.yugabyte.yba.v2.client.ApiClient;
import com.yugabyte.yba.v2.client.ApiException;
import com.yugabyte.yba.v2.client.Configuration;
import com.yugabyte.yba.v2.client.api.UniverseApi;
import com.yugabyte.yba.v2.client.models.UniverseRestart;
import com.yugabyte.yba.v2.client.models.YBATask;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.controllers.UniverseControllerTestBase;
import com.yugabyte.yw.controllers.handlers.UpgradeUniverseHandler;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.mockito.Mock;
import play.inject.guice.GuiceApplicationBuilder;

public class UniverseUtilitiesTest extends UniverseControllerTestBase {
  private Customer customer;
  private Users user;
  private String authToken;
  Universe universe;

  ApiClient v2Client;
  UniverseApi apiClient;

  @Mock UpgradeUniverseHandler mockUpgradeUniverseHandler;

  @Before
  @Override
  public void setUp() {
    this.customer = ModelFactory.testCustomer();
    this.user = ModelFactory.testUser(customer);
    this.authToken = user.createAuthToken();
    this.universe = ModelFactory.createUniverse(customer.getId());

    v2Client = Configuration.getDefaultApiClient();
    String basePath = String.format("http://localhost:%d/api/v2", port);
    v2Client = v2Client.setBasePath(basePath).addDefaultHeader("X-AUTH-TOKEN", authToken);
    Configuration.setDefaultApiClient(v2Client);
    apiClient = new UniverseApi();
  }

  @Override
  protected GuiceApplicationBuilder appOverrides(GuiceApplicationBuilder builder) {
    return builder.overrides(
        bind(UpgradeUniverseHandler.class).toInstance(mockUpgradeUniverseHandler));
  }

  @Test
  public void testV2RestartNullPayload() throws ApiException {
    UUID taskUUID = UUID.randomUUID();
    when(mockUpgradeUniverseHandler.restartUniverse(any(), eq(customer), eq(universe)))
        .thenReturn(taskUUID);
    YBATask resp = apiClient.restartUniverse(customer.getUuid(), universe.getUniverseUUID(), null);
    assertEquals(taskUUID, resp.getTaskUuid());
    verify(mockUpgradeUniverseHandler).restartUniverse(any(), eq(customer), eq(universe));
  }

  @Test
  public void testV2RestartEmptyPayload() throws ApiException {
    UUID taskUUID = UUID.randomUUID();
    when(mockUpgradeUniverseHandler.restartUniverse(any(), eq(customer), eq(universe)))
        .thenReturn(taskUUID);
    UniverseRestart payload = new UniverseRestart();
    YBATask resp =
        apiClient.restartUniverse(customer.getUuid(), universe.getUniverseUUID(), payload);
    assertEquals(taskUUID, resp.getTaskUuid());
    verify(mockUpgradeUniverseHandler).restartUniverse(any(), eq(customer), eq(universe));
  }

  @Test
  public void testV2RestartServiceOnly() throws ApiException {
    UUID taskUUID = UUID.randomUUID();
    when(mockUpgradeUniverseHandler.restartUniverse(any(), eq(customer), eq(universe)))
        .thenReturn(taskUUID);
    UniverseRestart payload = new UniverseRestart();
    payload.setRestartType(UniverseRestart.RestartTypeEnum.SERVICE);
    YBATask resp =
        apiClient.restartUniverse(customer.getUuid(), universe.getUniverseUUID(), payload);
    assertEquals(taskUUID, resp.getTaskUuid());
    verify(mockUpgradeUniverseHandler).restartUniverse(any(), eq(customer), eq(universe));
  }

  @Test
  public void testV2RestartSoft() throws ApiException {
    UUID taskUUID = UUID.randomUUID();
    when(mockUpgradeUniverseHandler.rebootUniverse(any(), eq(customer), eq(universe)))
        .thenReturn(taskUUID);
    UniverseRestart payload = new UniverseRestart();
    payload.setRestartType(UniverseRestart.RestartTypeEnum.OS);
    YBATask resp =
        apiClient.restartUniverse(customer.getUuid(), universe.getUniverseUUID(), payload);
    assertEquals(taskUUID, resp.getTaskUuid());
    verify(mockUpgradeUniverseHandler).rebootUniverse(any(), eq(customer), eq(universe));
  }

  @Test
  public void testV2RestartKubernetesSoft() throws ApiException {
    UUID taskUUID = UUID.randomUUID();
    Universe k8sUniverse =
        ModelFactory.createUniverse("k8sUniverse", customer.getId(), Common.CloudType.kubernetes);
    when(mockUpgradeUniverseHandler.restartUniverse(any(), eq(customer), eq(k8sUniverse)))
        .thenReturn(taskUUID);
    UniverseRestart payload = new UniverseRestart();
    payload.setRestartType(UniverseRestart.RestartTypeEnum.OS);
    YBATask resp =
        apiClient.restartUniverse(customer.getUuid(), k8sUniverse.getUniverseUUID(), payload);
    assertEquals(taskUUID, resp.getTaskUuid());
    verify(mockUpgradeUniverseHandler).restartUniverse(any(), eq(customer), eq(k8sUniverse));
  }
}
