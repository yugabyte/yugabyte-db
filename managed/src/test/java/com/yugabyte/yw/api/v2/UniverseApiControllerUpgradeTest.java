// Copyright (c) YugaByte, Inc.
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
import com.yugabyte.yba.v2.client.models.UniverseSoftwareUpgradeStart;
import com.yugabyte.yba.v2.client.models.YBATask;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.controllers.UniverseControllerTestBase;
import com.yugabyte.yw.controllers.handlers.UpgradeUniverseHandler;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Release;
import com.yugabyte.yw.models.ReleaseArtifact;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import play.inject.guice.GuiceApplicationBuilder;

public class UniverseApiControllerUpgradeTest extends UniverseControllerTestBase {
  private Customer customer;
  private Users user;
  private String authToken;
  Universe universe;
  Release upgradeRelease;

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
    this.upgradeRelease = Release.create("2.23.0.0-b213", "PREVIEW");
    upgradeRelease.addArtifact(
        ReleaseArtifact.create(
            null,
            ReleaseArtifact.Platform.LINUX,
            Architecture.x86_64,
            "https://download.yugabyte.com/release1"));

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
  public void testV2UniverseUpgradeRollbackExplicit() throws ApiException {
    UUID taskUUID = UUID.randomUUID();
    when(mockUpgradeUniverseHandler.upgradeDBVersion(any(), eq(customer), eq(universe)))
        .thenReturn(taskUUID);
    UniverseSoftwareUpgradeStart req = new UniverseSoftwareUpgradeStart();
    req.setAllowRollback(true);
    req.setYugabyteRelease(upgradeRelease.getReleaseUUID());
    YBATask resp =
        apiClient.startSoftwareUpgrade(customer.getUuid(), universe.getUniverseUUID(), req);
    assertEquals(taskUUID, resp.getTaskUuid());
    ArgumentCaptor<SoftwareUpgradeParams> captor =
        ArgumentCaptor.forClass(SoftwareUpgradeParams.class);
    verify(mockUpgradeUniverseHandler)
        .upgradeDBVersion(captor.capture(), eq(customer), eq(universe));
    SoftwareUpgradeParams params = captor.getValue();
    assertEquals(upgradeRelease.getVersion(), params.ybSoftwareVersion);
  }

  @Test
  public void testV2UniverseUpgradeRollbackImplicit() throws ApiException {
    UUID taskUUID = UUID.randomUUID();
    when(mockUpgradeUniverseHandler.upgradeDBVersion(any(), eq(customer), eq(universe)))
        .thenReturn(taskUUID);
    UniverseSoftwareUpgradeStart req = new UniverseSoftwareUpgradeStart();
    req.setYugabyteRelease(upgradeRelease.getReleaseUUID());
    YBATask resp =
        apiClient.startSoftwareUpgrade(customer.getUuid(), universe.getUniverseUUID(), req);
    assertEquals(taskUUID, resp.getTaskUuid());
    ArgumentCaptor<SoftwareUpgradeParams> captor =
        ArgumentCaptor.forClass(SoftwareUpgradeParams.class);
    verify(mockUpgradeUniverseHandler)
        .upgradeDBVersion(captor.capture(), eq(customer), eq(universe));
    SoftwareUpgradeParams params = captor.getValue();
    assertEquals(upgradeRelease.getVersion(), params.ybSoftwareVersion);
  }

  @Test
  public void testV2UniverseUpgradeNoRollback() throws ApiException {
    UUID taskUUID = UUID.randomUUID();
    when(mockUpgradeUniverseHandler.upgradeSoftware(any(), eq(customer), eq(universe)))
        .thenReturn(taskUUID);
    UniverseSoftwareUpgradeStart req = new UniverseSoftwareUpgradeStart();
    req.setAllowRollback(false);
    req.setYugabyteRelease(upgradeRelease.getReleaseUUID());
    YBATask resp =
        apiClient.startSoftwareUpgrade(customer.getUuid(), universe.getUniverseUUID(), req);
    assertEquals(taskUUID, resp.getTaskUuid());
    ArgumentCaptor<SoftwareUpgradeParams> captor =
        ArgumentCaptor.forClass(SoftwareUpgradeParams.class);
    verify(mockUpgradeUniverseHandler)
        .upgradeSoftware(captor.capture(), eq(customer), eq(universe));
    SoftwareUpgradeParams params = captor.getValue();
    assertEquals(upgradeRelease.getVersion(), params.ybSoftwareVersion);
  }
}
