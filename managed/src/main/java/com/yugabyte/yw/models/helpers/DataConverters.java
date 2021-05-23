// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.Alert.TargetType;

import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.CustomerTask;

public class DataConverters {

  public static Alert.TargetType taskTargetToAlertTargetType(CustomerTask.TargetType srcType) {
    switch (srcType) {
      case Universe:
        return TargetType.UniverseType;
      case Cluster:
        return TargetType.ClusterType;
      case Table:
        return TargetType.TableType;
      case Provider:
        return TargetType.ProviderType;
      case Node:
        return TargetType.NodeType;
      case Backup:
        return TargetType.BackupType;
      case KMSConfiguration:
        return TargetType.KMSConfigurationType;
      default:
        return TargetType.TaskType;
    }
  }

  public static Alert.TargetType definitionToAlertTargetType(AlertDefinition.TargetType srcType) {
    switch (srcType) {
      case Universe:
        return TargetType.UniverseType;
      default:
        throw new IllegalArgumentException("Unexpected definition type " + srcType.name());
    }
  }

  private DataConverters() {}
}
