package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.junit.Assert.assertEquals;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

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
    universe = ModelFactory.createUniverse("universe", defaultCustomer.getId());

    when(baseTaskDependencies.getRuntimeConfigFactory()).thenReturn(runtimeConfigFactory);
    when(baseTaskDependencies.getExecutorFactory())
        .thenReturn(app.injector().instanceOf(PlatformExecutorFactory.class));

    params = new UniverseDefinitionTaskParams();
    params.setUniverseUUID(universe.getUniverseUUID());

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

    assertEquals(-1, universe.getVersion());
  }
}
