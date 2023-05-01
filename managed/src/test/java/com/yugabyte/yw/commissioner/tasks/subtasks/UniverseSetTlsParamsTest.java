// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.core.StringContains.containsString;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;

public class UniverseSetTlsParamsTest extends FakeDBApplication {

  Customer defaultCustomer;
  Universe defaultUniverse;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUniverse = ModelFactory.createUniverse();
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        universe -> {
          UniverseDefinitionTaskParams uParams = defaultUniverse.getUniverseDetails();
          PlacementInfo pi = uParams.getPrimaryCluster().placementInfo;
          UserIntent ui = uParams.getPrimaryCluster().userIntent;
          uParams.upsertCluster(ui, pi, null);
        });
  }

  private void prepareUniverse(
      boolean updateInProgress,
      boolean enableNodeToNodeEncrypt,
      boolean enableClientToNodeEncrypt,
      boolean allowInsecure,
      UUID rootCA) {
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          universeDetails.clusters.forEach(
              c -> {
                c.userIntent.enableClientToNodeEncrypt = enableClientToNodeEncrypt;
                c.userIntent.enableNodeToNodeEncrypt = enableNodeToNodeEncrypt;
              });
          universeDetails.updateInProgress = updateInProgress;
          universeDetails.rootCA = rootCA;
          universeDetails.allowInsecure = allowInsecure;
        });
  }

  private UniverseSetTlsParams getTask(
      boolean enableNodeToNodeEncrypt,
      boolean enableClientToNodeEncrypt,
      boolean allowInsecure,
      UUID rootCA) {
    UniverseSetTlsParams.Params params = new UniverseSetTlsParams.Params();
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.enableNodeToNodeEncrypt = enableNodeToNodeEncrypt;
    params.enableClientToNodeEncrypt = enableClientToNodeEncrypt;
    params.allowInsecure = allowInsecure;
    params.rootCA = rootCA;
    UniverseSetTlsParams task = AbstractTaskBase.createTask(UniverseSetTlsParams.class);
    task.initialize(params);
    return task;
  }

  private void assertUniverseDetails(
      boolean enableNodeToNodeEncrypt,
      boolean enableClientToNodeEncrypt,
      boolean allowInsecure,
      UUID rootCA) {
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    universeDetails.clusters.forEach(
        c -> {
          assertEquals(enableNodeToNodeEncrypt, c.userIntent.enableNodeToNodeEncrypt);
          assertEquals(enableClientToNodeEncrypt, c.userIntent.enableClientToNodeEncrypt);
        });
    assertEquals(allowInsecure, universeDetails.allowInsecure);
    assertEquals(rootCA, universeDetails.rootCA);
  }

  @Test
  public void testParamsUpdateWhenUpdateNotInProgress() {
    prepareUniverse(false, false, false, true, null);
    UniverseSetTlsParams task = getTask(true, true, false, null);
    String errorMessage = assertThrows(RuntimeException.class, task::run).getMessage();
    assertThat(errorMessage, allOf(notNullValue(), containsString("is not being edited")));
  }

  @Test
  public void testEnableTlsParams() {
    UUID certUuid = UUID.randomUUID();
    prepareUniverse(true, false, false, true, null);
    UniverseSetTlsParams task = getTask(true, true, false, certUuid);
    task.run();
    assertUniverseDetails(true, true, false, certUuid);
  }

  @Test
  public void testDisableTlsParams() {
    prepareUniverse(true, true, true, false, UUID.randomUUID());
    UniverseSetTlsParams task = getTask(false, false, true, UUID.randomUUID());
    task.run();
    assertUniverseDetails(false, false, true, null);
  }
}
