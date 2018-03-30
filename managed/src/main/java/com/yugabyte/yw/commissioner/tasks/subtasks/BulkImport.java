// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.common.TableManager;
import com.yugabyte.yw.forms.BulkImportParams;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.TableDefinitionTaskParams;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.api.Play;

public class BulkImport extends AbstractTaskBase {

  public static final Logger LOG = LoggerFactory.getLogger(BulkImport.class);

  private TableManager tableManager;

  @Override
  protected BulkImportParams taskParams() {
    return (BulkImportParams) taskParams;
  }

  @Override
  public void initialize(ITaskParams params) {
    super.initialize(params);
    tableManager = Play.current().injector().instanceOf(TableManager.class);
  }

  @Override
  public void run() {
    // Execute the ansible command and log its result.
    logShellResponse(tableManager.bulkImport(taskParams()));
  }
}
