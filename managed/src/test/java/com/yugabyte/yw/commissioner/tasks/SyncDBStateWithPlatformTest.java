package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.SubTaskGroup;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.Spy;
import org.mockito.junit.MockitoJUnitRunner;

import static org.mockito.Mockito.*;

@RunWith(MockitoJUnitRunner.class)
public class SyncDBStateWithPlatformTest extends FakeDBApplication {
  private Customer defaultCustomer;
  private Universe universe;

  @Mock private BaseTaskDependencies baseTaskDependencies;
  @Mock private RuntimeConfigFactory runtimeConfigFactory;
  @Mock private SubTaskGroup subTaskGroup;

  private SyncDBStateWithPlatform task;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse("universe", defaultCustomer.getCustomerId());

    when(baseTaskDependencies.getRuntimeConfigFactory()).thenReturn(runtimeConfigFactory);

    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.universeUUID = universe.universeUUID;
    params.expectedUniverseVersion = -1;

    task = spy(new SyncDBStateWithPlatform(baseTaskDependencies));
    task.initialize(params);

    doReturn(subTaskGroup).when(task).createAsyncReplicationPlatformSyncTask();
    doReturn(subTaskGroup).when(task).createResetUniverseVersionTask();
  }

  @After
  public void tearDown() {
    universe.delete();
    defaultCustomer.delete();
  }

  @Test
  public void testRun() {
    task.run();

    verify(task, times(1)).createAsyncReplicationPlatformSyncTask();
    verify(task, times(1)).createResetUniverseVersionTask();
  }
}
