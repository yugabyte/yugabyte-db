// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import static org.junit.Assert.fail;

import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.models.CustomerTask;
import java.util.Set;
import org.junit.Test;

public class TaskTypeTest {

  private static final Set<TaskType> SKIP_CUSTOMER_TASK_ID_CHECK_TASKS =
      ImmutableSet.of(
          TaskType.RestorePitrConfig, TaskType.UpgradeUniverse, TaskType.AddGFlagMetadata);

  @Test
  // A simple test to enforce parent tasks to be defined together first.
  // It also ensures that the customer task IDs are not missed out.
  public void TestTaskOrderAndOperationIds() {
    boolean isInParentTask = true;
    for (TaskType taskType : TaskType.values()) {
      if (taskType.getTaskClass() == null) {
        fail(String.format("Task class must be set for %s", taskType.name()));
      }
      String packageName = taskType.getTaskClass().getPackageName();
      boolean isSubtaskPackage =
          packageName.endsWith(".subtasks") || packageName.contains(".subtasks.");
      if (isSubtaskPackage) {
        isInParentTask = false;
      } else if (!isInParentTask) {
        fail(
            String.format(
                "Task %s defined at wrong position. Parent tasks must be defined together in the"
                    + " beginning",
                taskType.name()));
      } else if (!SKIP_CUSTOMER_TASK_ID_CHECK_TASKS.contains(taskType)) {
        Set<Pair<CustomerTask.TaskType, CustomerTask.TargetType>> taskIds =
            taskType.getCustomerTaskIds();
        if (taskIds.isEmpty()) {
          fail(
              String.format(
                  "Parent task %s must have a customer task ID at least", taskType.name()));
        }
      }
    }
  }
}
