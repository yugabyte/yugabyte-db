package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellResponse;

import play.libs.Json;

import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.CopyOnWriteArrayList;

import com.fasterxml.jackson.databind.JsonNode;

public class CreateRootVolumes extends NodeTaskBase {
  private final Map<UUID, List<String>> bootDisksPerZone;

  public CreateRootVolumes(Map<UUID, List<String>> bootDisksPerZone) {
    this.bootDisksPerZone = bootDisksPerZone;
  }

  public static class Params extends AnsibleSetupServer.Params {
    public int numVolumes;
    public String machineImage;
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
    bootDisksPerZone.putIfAbsent(taskParams().azUuid, disks);
  }
}
