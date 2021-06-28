package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AsyncReplicationRelationship;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.cdc.CdcConsumer;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.YBClient;
import org.yb.master.Master;

import static org.junit.Assert.*;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.when;

@RunWith(MockitoJUnitRunner.class)
public class ResetUniverseVersionTest extends FakeDBApplication {
  private Customer defaultCustomer;
  private Universe universe;

  @Mock private BaseTaskDependencies baseTaskDependencies;
  @Mock private RuntimeConfigFactory runtimeConfigFactory;

  private UniverseDefinitionTaskParams params;
  private ResetUniverseVersion task;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse("universe", defaultCustomer.getCustomerId());

    when(baseTaskDependencies.getRuntimeConfigFactory()).thenReturn(runtimeConfigFactory);

    params = new UniverseDefinitionTaskParams();
    params.universeUUID = universe.universeUUID;

    task = new ResetUniverseVersion(baseTaskDependencies);
    task.initialize(params);
  }

  @After
  public void tearDown() {
    universe.delete();
    defaultCustomer.delete();
  }

  @Test
  public void testResetUniverseVersion() {
    task.run();
    universe.refresh();

    assertEquals(-1, universe.version);
  }
}
