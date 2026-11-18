// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.helpers;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.DeltaEvaluator;
import com.yugabyte.yw.common.NodeDetailsArrayComparator;
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
          BAD_REQUEST, "Cannot roll back: state_transition_details.delta is missing");
    }
    if (!rollbackSafe) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot roll back: rollback checkpoint was crossed (rollbackSafe=false)");
    }
    if (isDedicatedNodesChanged()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Cannot roll back: dedicatedNodes changed (Live tserver gflags were rewritten before"
              + " the rollback checkpoint)");
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
          BAD_REQUEST, "Cannot roll back: generateOldValue returned null");
    }
    return Json.fromJson(beforeJson, UniverseDefinitionTaskParams.class);
  }

  /** Reconstructs the failed task's target universe details from {@code delta}. */
  @JsonIgnore
  public UniverseDefinitionTaskParams getTargetUniverseDetails() {
    JsonNode targetJson = DeltaEvaluator.generateNewValue(requireDelta());
    if (targetJson == null || targetJson.isNull()) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot roll back: generateNewValue returned null");
    }
    return Json.fromJson(targetJson, UniverseDefinitionTaskParams.class);
  }

  /**
   * Builds restored pre-task details (edit-owned topology) while keeping the current values of the
   * fields annotated with {@link DeltaEvaluator.DeltaRetainLatest}: the lock fields owned by the
   * current rollback task so {@code unlockUniverseForUpdate} still works, {@code sequenceNumber}
   * advanced by {@code UpdateConsistencyCheck} after freeze, and the fields other tasks write while
   * the failed task waits (YBC version and throttle flags, node agent flags, Perf Advisor
   * registration). Clusters are matched by uuid; a cluster missing in current keeps its own values.
   */
  public UniverseDefinitionTaskParams applyBeforeTo(UniverseDefinitionTaskParams current) {
    UniverseDefinitionTaskParams before = getBeforeUniverseDetails();
    DeltaEvaluator.mergeWithLatest(before, current, new NodeDetailsArrayComparator());
    return before;
  }

  private JsonNode requireDelta() {
    if (delta == null || delta.isNull()) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot roll back: state_transition_details.delta is missing");
    }
    return delta;
  }
}
