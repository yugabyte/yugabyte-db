// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.helpers.TaskType;

public enum NodeActionType {
    START,
    STOP,
    DELETE;

    public String toString(boolean completed) {
      switch(this) {
        case START:
          return completed ? "Started" : "Starting";
        case STOP:
          return completed ? "Stopped" : "Stopping";
        case DELETE:
          return completed ? "Deleted" : "Deleting";
        default:
          return null;
      }
    }

    public TaskType getCommissionerTask() {
      switch (this) {
        case START:
          return TaskType.StartNodeInUniverse;
        case STOP:
          return TaskType.StopNodeInUniverse;
        case DELETE:
          return TaskType.DeleteNodeFromUniverse;
        default:
          return null;
      }
    }

    public CustomerTask.TaskType getCustomerTask() {
      switch (this) {
        case START:
          return CustomerTask.TaskType.Start;
        case STOP:
          return CustomerTask.TaskType.Stop;
        case DELETE:
          return CustomerTask.TaskType.Delete;
        default:
          return null;
      }
    }
}
