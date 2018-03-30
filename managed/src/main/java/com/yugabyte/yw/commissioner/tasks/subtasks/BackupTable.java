// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.common.TableManager;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.models.Backup;
import play.api.Play;
import play.libs.Json;


public class BackupTable extends AbstractTaskBase {

  @Override
  protected BackupTableParams taskParams() {
    return (BackupTableParams) taskParams;
  }

  private TableManager tableManager;

  @Override
  public void initialize(ITaskParams params) {
    super.initialize(params);
    tableManager = Play.current().injector().instanceOf(TableManager.class);
  }

  @Override
  public void run() {
    ShellProcessHandler.ShellResponse response = tableManager.createBackup(taskParams());
    logShellResponse(response);
    Backup backup = Backup.fetchByTaskUUID(userTaskUUID);

    if (response.code == 0) {
      JsonNode jsonNode = Json.parse(response.message);
      if (jsonNode.has("error")) {
        backup.transitionState(Backup.BackupState.Failed);
      } else {
        backup.transitionState(Backup.BackupState.Completed);
      }
    }
  }
}
