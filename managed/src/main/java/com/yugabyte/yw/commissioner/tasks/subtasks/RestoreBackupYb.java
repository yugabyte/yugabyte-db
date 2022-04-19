package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.DevopsBase;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.PlacementInfo;

import javax.inject.Inject;

import java.util.HashMap;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Slf4j
public class RestoreBackupYb extends AbstractTaskBase {

  @Inject
  public RestoreBackupYb(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected RestoreBackupParams taskParams() {
    return (RestoreBackupParams) taskParams;
  }

  @Override
  public void run() {

    try {
      ShellResponse response = restoreManagerYb.runCommand(taskParams());
      JsonNode jsonNode = null;
      try {
        jsonNode = Json.parse(response.message);
      } catch (Exception e) {
        log.error("Response code={}, output={}.", response.code, response.message);
        throw e;
      }
      if (response.code != 0 || jsonNode.has("error")) {
        log.error("Response code={}, hasError={}.", response.code, jsonNode.has("error"));
        throw new RuntimeException(response.message);
      } else {
        log.info("[" + getName() + "] STDOUT: " + response.message);
      }
    } catch (Exception e) {
      log.error("Errored out with: " + e);
      throw new RuntimeException(e);
    }
  }
}
