// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.TaskListQueue;
import com.yugabyte.yw.forms.TableDefinitionTaskParams;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.Common;

public class CreateCassandraTable extends UniverseTaskBase {
  public static final Logger LOG = LoggerFactory.getLogger(CreateCassandraTable.class);

  @Override
  protected TableDefinitionTaskParams taskParams() {
    return (TableDefinitionTaskParams) taskParams;
  }

  @Override
  public void run() {
    try {
      // Create the task list sequence.
      taskListQueue = new TaskListQueue();

      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      lockUniverseForUpdate(-1 /* expectedUniverseVersion */);

      // Create table task
      createTableTask(Common.TableType.YSQL_TABLE_TYPE, taskParams().tableDetails.tableName, -1,
                      taskParams().tableDetails);

      // TODO: wait for table creation

      // Run all the tasks.
      taskListQueue.run();
    } catch (Throwable t) {
      LOG.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      throw t;
    } finally {
      // Mark the update of the universe as done. This will allow future edits/updates to the
      // universe to happen.
      unlockUniverseForUpdate();
    }
    LOG.info("Finished {} task.", getName());
  }
}
