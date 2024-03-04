// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.helpers.TaskType;

public enum NodeActionType {
  // Add a previously removed (or not in-use) node to the cluster and balance data onto it.
  ADD,
  // Remove a node the cluster use and move its data out.
  // The same instance is not expected to be used for this cluster again.
  REMOVE,
  // Start the server processes on a previously stopped node.
  // Ideally it is added back very soon.
  START,
  // Stop the server processes running on the node.
  STOP,
  // Delete the node from yugaware db if it failed to come up during creation.
  // Shown only for ToBeAdded node status.
  DELETE,
  // Allows node to be included in live queries dashboard
  QUERY,
  // Release the instance to the IaaS/provider. Shown only for stopped/removed nodes.
  RELEASE,
  // Reboot the node.
  REBOOT,
  // Start the Master server on the node.
  START_MASTER,
  // Precheck for detached node.
  PRECHECK_DETACHED(true),
  // Hard reboot the node (stop + start).
  HARD_REBOOT,
  // Re-provision node with already stopped processes.
  REPROVISION,
  // REplace an node.
  REPLACE;

  NodeActionType() {
    this(false);
  }

  NodeActionType(boolean forDetached) {
    this.forDetached = forDetached;
  }

  private final boolean forDetached;

  public boolean isForDetached() {
    return forDetached;
  }

  public String toString(boolean completed) {
    switch (this) {
      case ADD:
        return completed ? "Added" : "Adding";
      case REMOVE:
        return completed ? "Removed" : "Removing";
      case START:
        return completed ? "Started" : "Starting";
      case STOP:
        return completed ? "Stopped" : "Stopping";
      case DELETE:
        return completed ? "Deleted" : "Deleting";
      case QUERY:
        return "Queries";
      case RELEASE:
        return completed ? "Released" : "Releasing";
      case REBOOT:
        return completed ? "Rebooted" : "Rebooting";
      case HARD_REBOOT:
        return completed ? "Hard rebooted" : "Hard rebooting";
      case START_MASTER:
        return completed ? "Started Master" : "Starting Master";
      case PRECHECK_DETACHED:
        return completed ? "Performed preflight check" : "Performing preflight check";
      case REPROVISION:
        return completed ? "Re-provisioned" : "Re-provisioning";
      case REPLACE:
        return completed ? "Replaced" : "Replacing";
      default:
        return null;
    }
  }

  public TaskType getCommissionerTask() {
    switch (this) {
      case ADD:
        return TaskType.AddNodeToUniverse;
      case REMOVE:
        return TaskType.RemoveNodeFromUniverse;
      case START:
        return TaskType.StartNodeInUniverse;
      case STOP:
        return TaskType.StopNodeInUniverse;
      case DELETE:
        return TaskType.DeleteNodeFromUniverse;
      case REBOOT:
      case HARD_REBOOT:
        return TaskType.RebootNodeInUniverse;
      case RELEASE:
        return TaskType.ReleaseInstanceFromUniverse;
      case START_MASTER:
        return TaskType.StartMasterOnNode;
      case PRECHECK_DETACHED:
        return TaskType.PrecheckNodeDetached;
      case REPROVISION:
        return TaskType.ReprovisionNode;
      case REPLACE:
        return TaskType.ReplaceNodeInUniverse;
      default:
        return null;
    }
  }

  public CustomerTask.TaskType getCustomerTask() {
    switch (this) {
      case ADD:
        return CustomerTask.TaskType.Add;
      case REMOVE:
        return CustomerTask.TaskType.Remove;
      case START:
        return CustomerTask.TaskType.Start;
      case STOP:
        return CustomerTask.TaskType.Stop;
      case DELETE:
        return CustomerTask.TaskType.Delete;
      case RELEASE:
        return CustomerTask.TaskType.Release;
      case REBOOT:
        return CustomerTask.TaskType.Reboot;
      case HARD_REBOOT:
        return CustomerTask.TaskType.HardReboot;
      case START_MASTER:
        return CustomerTask.TaskType.StartMaster;
      case PRECHECK_DETACHED:
        return CustomerTask.TaskType.PrecheckNode;
      case REPROVISION:
        return CustomerTask.TaskType.ReprovisionNode;
      case REPLACE:
        return CustomerTask.TaskType.Replace;
      default:
        return null;
    }
  }
}
