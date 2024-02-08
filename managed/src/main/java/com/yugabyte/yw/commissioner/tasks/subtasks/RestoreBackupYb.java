package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.models.Restore;
import com.yugabyte.yw.models.RestoreKeyspace;
import javax.inject.Inject;
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
    RestoreKeyspace restoreKeyspace = null;
    try {

      log.info("Creating entry for restore keyspace: {}", getTaskUUID());
      restoreKeyspace = RestoreKeyspace.create(getTaskUUID(), taskParams());

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
        if (restoreKeyspace != null) {
          restoreKeyspace.update(getTaskUUID(), RestoreKeyspace.State.Failed);
        }
        throw new RuntimeException(response.message);
      } else {
        log.info("[" + getName() + "] STDOUT: " + response.message);
        if (restoreKeyspace != null) {
          long backupSize = restoreKeyspace.getBackupSizeFromStorageLocation();
          Restore.updateRestoreSizeForRestore(taskParams().prefixUUID, backupSize);
          restoreKeyspace.update(getTaskUUID(), RestoreKeyspace.State.Completed);
        }
      }
    } catch (Exception e) {
      log.error("Errored out with: " + e);
      if (restoreKeyspace != null) {
        restoreKeyspace.update(getTaskUUID(), RestoreKeyspace.State.Failed);
      }
      throw new RuntimeException(e);
    }
  }
}
