// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellResponse;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.CopyOnWriteArrayList;
import javax.inject.Inject;
import play.libs.Json;

public class CreateRootVolumes extends NodeTaskBase {

  private static final String BOOT_DISK_KEY = "boot_disks_per_zone";
  private static final String ROOT_DEVICE_KEY = "root_device_name";

  @Inject
  protected CreateRootVolumes(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends AnsibleCreateServer.Params {
    public int numVolumes;
    public Map<UUID, List<String>> bootDisksPerZone;
    public Map<UUID, String> rootDevicePerZone;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    ShellResponse response =
        getNodeManager()
            .nodeCommand(NodeManager.NodeCommandType.Create_Root_Volumes, taskParams())
            .processErrors();
    JsonNode parsedResponse = parseShellResponseAsJson(response);
    JsonNode parsedBootDisks = parsedResponse.get(BOOT_DISK_KEY);
    if (parsedBootDisks != null) {
      List<String> disks = Json.fromJson(parsedBootDisks, CopyOnWriteArrayList.class);
      taskParams().bootDisksPerZone.putIfAbsent(taskParams().azUuid, disks);
    }
    JsonNode parsedRootDevice = parsedResponse.get(ROOT_DEVICE_KEY);
    if (parsedRootDevice != null) {
      taskParams().rootDevicePerZone.putIfAbsent(taskParams().azUuid, parsedRootDevice.asText());
    }
  }
}
