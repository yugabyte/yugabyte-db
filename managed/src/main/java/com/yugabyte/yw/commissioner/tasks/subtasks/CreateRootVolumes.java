package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellResponse;
import play.libs.Json;

import javax.inject.Inject;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.CopyOnWriteArrayList;

public class CreateRootVolumes extends NodeTaskBase {

  @Inject
  protected CreateRootVolumes(BaseTaskDependencies baseTaskDependencies, NodeManager nodeManager) {
    super(baseTaskDependencies, nodeManager);
  }

  public static class Params extends AnsibleSetupServer.Params {
    public int numVolumes;
    public String machineImage;
    public Map<UUID, List<String>> bootDisksPerZone;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    ShellResponse response =
        getNodeManager().nodeCommand(NodeManager.NodeCommandType.Create_Root_Volumes, taskParams());
    processShellResponse(response);
    JsonNode parsedResponse = parseShellResponseAsJson(response);
    List<String> disks = Json.fromJson(parsedResponse, CopyOnWriteArrayList.class);
    taskParams().bootDisksPerZone.putIfAbsent(taskParams().azUuid, disks);
  }
}
