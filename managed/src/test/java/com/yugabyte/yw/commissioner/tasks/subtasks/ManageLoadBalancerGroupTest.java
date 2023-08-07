package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.cloud.CloudAPI;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.CloudUtil.Protocol;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NLBHealthCheckConfiguration;
import java.util.ArrayList;
import java.util.List;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class ManageLoadBalancerGroupTest extends FakeDBApplication {
  private Customer defaultCustomer;
  private Universe universe;

  @Mock private BaseTaskDependencies baseTaskDependencies;
  @Mock private RuntimeConfGetter runtimeConfGetter;
  @Mock private RuntimeConfigFactory runtimeConfigFactory;
  @Mock private CloudAPI.Factory cloudAPIFactory;

  private ManageLoadBalancerGroup task;
  private List<Integer> portsToForward;

  @Before
  public void setup() {
    defaultCustomer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse("universe", defaultCustomer.getId());

    when(baseTaskDependencies.getExecutorFactory())
        .thenReturn(app.injector().instanceOf(PlatformExecutorFactory.class));
    when(baseTaskDependencies.getConfGetter()).thenReturn(runtimeConfGetter);
    when(runtimeConfGetter.getConfForScope(
            eq(universe), eq(UniverseConfKeys.customHealthCheckPath)))
        .thenReturn("/");
    when(runtimeConfGetter.getConfForScope(
            eq(universe), eq(UniverseConfKeys.customHealthCheckPort)))
        .thenReturn(-1);
    when(runtimeConfGetter.getConfForScope(
            eq(universe), eq(UniverseConfKeys.customHealthCheckProtocol)))
        .thenReturn(Protocol.TCP);

    task = new ManageLoadBalancerGroup(baseTaskDependencies, cloudAPIFactory);
    portsToForward = new ArrayList<>();
    portsToForward.add(0, 5433);
  }

  @Test
  public void testGetNlbHealthCheckConfigDefault() {
    NLBHealthCheckConfiguration config =
        task.getNlbHealthCheckConfiguration(universe, portsToForward);
    assertEquals(Protocol.TCP, config.getHealthCheckProtocol());
    assert (config.getHealthCheckPath().equals("/"));
    assertEquals(1, config.getHealthCheckPorts().size());
    assertEquals(5433, config.getHealthCheckPorts().get(0).intValue());
  }

  @Test
  public void testGetNlbHealthCheckConfigCustomPort() {
    when(runtimeConfGetter.getConfForScope(
            eq(universe), eq(UniverseConfKeys.customHealthCheckPort)))
        .thenReturn(5432);
    NLBHealthCheckConfiguration config =
        task.getNlbHealthCheckConfiguration(universe, portsToForward);
    assertEquals(Protocol.TCP, config.getHealthCheckProtocol());
    assert (config.getHealthCheckPath().equals("/"));
    assertEquals(1, config.getHealthCheckPorts().size());
    assertEquals(5432, config.getHealthCheckPorts().get(0).intValue());
  }

  @Test
  public void testGetNlbHealthCheckConfigCustomPath() {
    when(runtimeConfGetter.getConfForScope(
            eq(universe), eq(UniverseConfKeys.customHealthCheckPath)))
        .thenReturn("/path");
    NLBHealthCheckConfiguration config =
        task.getNlbHealthCheckConfiguration(universe, portsToForward);
    assertEquals(Protocol.TCP, config.getHealthCheckProtocol());
    assert (config.getHealthCheckPath().equals("/path"));
    assertEquals(1, config.getHealthCheckPorts().size());
    assertEquals(5433, config.getHealthCheckPorts().get(0).intValue());
  }

  @Test
  public void testGetNlbHealthCheckConfigCustomProtocol() {
    when(runtimeConfGetter.getConfForScope(
            eq(universe), eq(UniverseConfKeys.customHealthCheckProtocol)))
        .thenReturn(Protocol.HTTP);
    NLBHealthCheckConfiguration config =
        task.getNlbHealthCheckConfiguration(universe, portsToForward);
    assertEquals(Protocol.HTTP, config.getHealthCheckProtocol());
    assert (config.getHealthCheckPath().equals("/"));
    assertEquals(1, config.getHealthCheckPorts().size());
    assertEquals(5433, config.getHealthCheckPorts().get(0).intValue());
  }
}
