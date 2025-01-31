/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 */
package com.yugabyte.yw.common.operator;

import com.yugabyte.yw.models.Universe;
import java.nio.file.Path;
import java.util.UUID;

/*
 * OperatorStatusUpdater interface contains all possible status update methods needed for the k8s
 * operator. Each of these methods are implemented as a default, no-op implementation, making it
 * easy to create implementations that only support a sub-set of functionality for specific use
 * cases. This also allows for easy implementation of the no-op class.
 */
public interface OperatorStatusUpdater {

  public enum UniverseState {
    CREATING("Creating"),
    READY("Ready"),
    ERROR_UPDATING("Error Updating"),
    ERROR_CREATING("Error Creating"),
    ERROR_PAUSING("Error Pausing"),
    ERROR_RESUMING("Error Resuming"),
    EDITING("Editing"),
    DELETING("Deleting"),
    PAUSED("Paused"),
    PAUSING("Pausing"),
    RESUMING("Resuming");

    UniverseState(String universeState) {
      this.universeState = universeState;
    }

    public String getUniverseStateString() {
      return universeState;
    }

    private String universeState;
  }

  default void createYBUniverseEventStatus(
      Universe universe, KubernetesResourceDetails universeName, String taskName) {
    // no-op implementation
  }

  default void startYBUniverseEventStatus(
      Universe universe,
      KubernetesResourceDetails universeName,
      String taskName,
      UUID taskUUID,
      UniverseState state,
      boolean isRetry) {
    // no-op implementation
  }

  default void startYBUniverseEventStatus(
      Universe universe,
      KubernetesResourceDetails universeName,
      String taskName,
      UUID taskUUID,
      UniverseState state) {
    startYBUniverseEventStatus(universe, universeName, taskName, taskUUID, state, false);
  }

  default void updateRestoreJobStatus(String message, UUID taskUUID, Universe universe) {
    // no-op implementation
  }

  default void updateBackupStatus(
      com.yugabyte.yw.models.Backup backup, String taskName, UUID taskUUID) {
    // no-op implementation
  }

  default void updateYBUniverseStatus(
      Universe universe,
      KubernetesResourceDetails universeName,
      String taskName,
      UUID taskUUID,
      UniverseState state,
      Throwable t) {
    // no-op implementation
  }

  default void markSupportBundleFinished(
      com.yugabyte.yw.models.SupportBundle supportBundle,
      KubernetesResourceDetails bundleName,
      Path localPath) {
    // no-op implementation
  }

  default void markSupportBundleFailed(
      com.yugabyte.yw.models.SupportBundle supportBundle, KubernetesResourceDetails bundleName) {
    // no-op implementation
  }

  default void doKubernetesEventUpdate(KubernetesResourceDetails universeName, String status) {
    // no-op implementation
  }

  default void updateUniverseState(KubernetesResourceDetails universeName, UniverseState state) {
    // no-op implementation
  }
}
