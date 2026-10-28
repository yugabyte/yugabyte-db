// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.google.inject.Injector;
import com.yugabyte.yw.commissioner.tasks.subtasks.ReplaceRootVolume;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.List;
import org.junit.Before;
import org.junit.Test;
import org.mockito.ArgumentCaptor;

public class NodeManagerTest extends FakeDBApplication {

  private NodeManager nodeManager;
  private Universe universe;

  @Before
  public void setUp() {
    // FakeDBApplication binds NodeManager to a mock, so build a real one around the mocked shell.
    nodeManager = new NodeManager();
    app.injector().instanceOf(Injector.class).injectMembers(nodeManager);
    when(mockShellProcessHandler.run(anyList(), any(ShellProcessContext.class)))
        .thenReturn(ShellResponse.create(0, ""));

    Customer customer = ModelFactory.testCustomer();
    Provider provider = ModelFactory.ociProvider(customer);
    UserIntent userIntent = ApiUtils.getDefaultUserIntentSingleAZ(provider);
    universe = ModelFactory.createUniverse(customer.getId());
    universe =
        Universe.saveDetails(universe.getUniverseUUID(), ApiUtils.mockUniverseUpdater(userIntent));
  }

  @Test
  public void testReplaceRootVolumeOciForced() {
    List<String> command = runOciReplaceRootVolume(true);
    assertTrue(command.toString(), command.contains("--force_replacement"));
  }

  @Test
  public void testReplaceRootVolumeOciNotForced() {
    List<String> command = runOciReplaceRootVolume(false);
    assertFalse(command.toString(), command.contains("--force_replacement"));
  }

  private List<String> runOciReplaceRootVolume(boolean force) {
    NodeDetails node = universe.getUniverseDetails().nodeDetailsSet.iterator().next();
    ReplaceRootVolume.Params params = new ReplaceRootVolume.Params();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.nodeName = node.nodeName;
    params.azUuid = node.azUuid;
    params.replacementDisk = "target-image";
    params.forceReplacement = force;
    nodeManager.nodeCommand(NodeManager.NodeCommandType.Replace_Root_Volume, params);

    @SuppressWarnings("unchecked")
    ArgumentCaptor<List<String>> captor = ArgumentCaptor.forClass(List.class);
    verify(mockShellProcessHandler).run(captor.capture(), any(ShellProcessContext.class));
    List<String> command = captor.getValue();
    int diskIdx = command.indexOf("--replacement_disk");
    assertTrue(command.toString(), diskIdx >= 0);
    assertEquals("target-image", command.get(diskIdx + 1));
    return command;
  }
}
