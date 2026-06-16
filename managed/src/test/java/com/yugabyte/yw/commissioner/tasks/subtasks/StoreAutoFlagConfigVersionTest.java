// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.PrevYBSoftwareConfig;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.client.GetAutoFlagsConfigResponse;
import org.yb.client.YBClient;
import org.yb.master.MasterClusterOuterClass.GetAutoFlagsConfigResponsePB;

@RunWith(MockitoJUnitRunner.class)
public class StoreAutoFlagConfigVersionTest extends FakeDBApplication {

  private static final String OLD_VERSION = "2.21.0.0-b1";
  private static final String NEW_VERSION = "2.21.0.0-b2";

  private Customer defaultCustomer;
  private Universe universe;
  private StoreAutoFlagConfigVersion task;
  private YBClientService ybClientService;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse("universe", defaultCustomer.getId());
    ybClientService = app.injector().instanceOf(YBClientService.class);
    task = app.injector().instanceOf(StoreAutoFlagConfigVersion.class);
  }

  @Test
  public void testSkipsWhenPrevAlreadyCapturedForTargetVersion() throws Exception {
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams details = u.getUniverseDetails();
              details.updateInProgress = true;
              details.getPrimaryCluster().userIntent.ybSoftwareVersion = NEW_VERSION;
              PrevYBSoftwareConfig prev = new PrevYBSoftwareConfig();
              prev.setSoftwareVersion(OLD_VERSION);
              prev.setTargetUpgradeSoftwareVersion(NEW_VERSION);
              prev.setAutoFlagConfigVersion(42);
              details.prevYBSoftwareConfig = prev;
              u.setUniverseDetails(details);
            });

    StoreAutoFlagConfigVersion.Params params = new StoreAutoFlagConfigVersion.Params();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.targetUpgradeSoftwareVersion = NEW_VERSION;
    task.initialize(params);

    task.run();

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    PrevYBSoftwareConfig prev = universe.getUniverseDetails().prevYBSoftwareConfig;
    assertEquals(OLD_VERSION, prev.getSoftwareVersion());
    assertEquals(NEW_VERSION, prev.getTargetUpgradeSoftwareVersion());
    assertEquals(42, prev.getAutoFlagConfigVersion());
    verify(ybClientService, never()).getUniverseClient(any());
  }

  @Test
  public void testStoresPrevFromUserIntentWhenNotYetCaptured() throws Exception {
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams details = u.getUniverseDetails();
              details.updateInProgress = true;
              details.getPrimaryCluster().userIntent.ybSoftwareVersion = OLD_VERSION;
              u.setUniverseDetails(details);
            });

    YBClient mockClient = org.mockito.Mockito.mock(YBClient.class);
    when(ybClientService.getUniverseClient(any())).thenReturn(mockClient);
    GetAutoFlagsConfigResponsePB responsePb =
        GetAutoFlagsConfigResponsePB.newBuilder()
            .setConfig(
                org.yb.WireProtocol.AutoFlagsConfigPB.newBuilder().setConfigVersion(7).build())
            .build();
    when(mockClient.autoFlagsConfig())
        .thenReturn(new GetAutoFlagsConfigResponse(0, null, responsePb));

    StoreAutoFlagConfigVersion.Params params = new StoreAutoFlagConfigVersion.Params();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.targetUpgradeSoftwareVersion = NEW_VERSION;
    task.initialize(params);

    task.run();

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    PrevYBSoftwareConfig prev = universe.getUniverseDetails().prevYBSoftwareConfig;
    assertEquals(OLD_VERSION, prev.getSoftwareVersion());
    assertEquals(NEW_VERSION, prev.getTargetUpgradeSoftwareVersion());
    assertEquals(7, prev.getAutoFlagConfigVersion());
  }
}
