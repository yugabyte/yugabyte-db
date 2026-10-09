// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.helpers;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.DeltaEvaluator;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.NoArgsConstructor;
import play.libs.Json;

/** Captures a before-to-target state transition for an in-flight universe task. */
@Data
@NoArgsConstructor
@AllArgsConstructor
public class StateTransitionDetails {
  private boolean rollbackSafe;
  private JsonNode delta;

  /**
   * Validates that this transition can be rolled back (delta present and still within the safe
   * window). Throws {@link PlatformServiceException} otherwise.
   */
  public void requireRollbackable() {
    if (delta == null || delta.isNull()) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot roll back edit universe: state_transition_details.delta is missing");
    }
    if (!rollbackSafe) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Cannot roll back edit universe: rollback checkpoint was crossed"
              + " (rollbackSafe=false)");
    }
    if (isDedicatedNodesChanged()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Cannot roll back edit universe: dedicatedNodes changed (Live tserver gflags were"
              + " rewritten before the rollback checkpoint)");
    }
  }

  /**
   * True when primary {@code dedicatedNodes} differs between before and target. Such edits rewrite
   * Live tserver gflags before {@code MarkRollbackUnsafe}; manual rollback does not re-apply before
   * gflags (same refusal as edit auto-rollback).
   */
  @JsonIgnore
  public boolean isDedicatedNodesChanged() {
    UniverseDefinitionTaskParams before = getBeforeUniverseDetails();
    UniverseDefinitionTaskParams target = getTargetUniverseDetails();
    Cluster beforePrimary = before.getPrimaryCluster();
    Cluster targetPrimary = target.getPrimaryCluster();
    if (beforePrimary == null
        || targetPrimary == null
        || beforePrimary.userIntent == null
        || targetPrimary.userIntent == null) {
      return false;
    }
    return beforePrimary.userIntent.dedicatedNodes != targetPrimary.userIntent.dedicatedNodes;
  }

  /** Reconstructs pre-task universe details from {@code delta}. */
  @JsonIgnore
  public UniverseDefinitionTaskParams getBeforeUniverseDetails() {
    JsonNode beforeJson = DeltaEvaluator.generateOldValue(requireDelta());
    if (beforeJson == null || beforeJson.isNull()) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot roll back edit universe: generateOldValue returned null");
    }
    return Json.fromJson(beforeJson, UniverseDefinitionTaskParams.class);
  }

  /** Reconstructs the failed task's target universe details from {@code delta}. */
  @JsonIgnore
  public UniverseDefinitionTaskParams getTargetUniverseDetails() {
    JsonNode targetJson = DeltaEvaluator.generateNewValue(requireDelta());
    if (targetJson == null || targetJson.isNull()) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot roll back edit universe: generateNewValue returned null");
    }
    return Json.fromJson(targetJson, UniverseDefinitionTaskParams.class);
  }

  /**
   * Builds restored pre-task details (edit-owned topology) while preserving lock fields owned by
   * the current rollback task so {@code unlockUniverseForUpdate} still works, YBC fields that may
   * have been updated by a YBC upgrade outside this edit, and {@code sequenceNumber} advanced by
   * {@code UpdateConsistencyCheck} after freeze (must not be restored from the checkpoint).
   */
  public UniverseDefinitionTaskParams applyBeforeTo(UniverseDefinitionTaskParams current) {
    UniverseDefinitionTaskParams before = getBeforeUniverseDetails();
    before.updateInProgress = current.updateInProgress;
    before.updatingTask = current.updatingTask;
    before.updatingTaskUUID = current.updatingTaskUUID;
    before.placementModificationTaskUuid = current.placementModificationTaskUuid;
    before.updateSucceeded = current.updateSucceeded;
    before.setYbcSoftwareVersion(current.getYbcSoftwareVersion());
    before.setEnableYbc(current.isEnableYbc());
    before.setYbcInstalled(current.isYbcInstalled());
    before.sequenceNumber = current.sequenceNumber;
    return before;
  }

  private JsonNode requireDelta() {
    if (delta == null || delta.isNull()) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot roll back edit universe: state_transition_details.delta is missing");
    }
    return delta;
  }
}
