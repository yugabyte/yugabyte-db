// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.rollback;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.tasks.ReadOnlyClusterDelete;
import com.yugabyte.yw.commissioner.tasks.ReadOnlyKubernetesClusterDelete;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.List;
import java.util.UUID;
import javax.annotation.Nullable;
import javax.inject.Singleton;
import play.libs.Json;

/**
 * Rolls back a failed read replica create by force-deleting the read replica. The create writes the
 * cluster into the universe when it freezes it, before any pod or node exists, so the delete has to
 * cope with a partly created cluster; the force flag makes the VM delete ignore errors from
 * destroying servers that were never created.
 */
@Singleton
public class ReadOnlyClusterCreateRollbackComputer implements TaskRollbackComputer {

  /** Source read replica create to the delete that rolls it back. */
  public static final ImmutableMap<TaskType, TaskType> ROLLBACK_TASK_TYPES =
      ImmutableMap.of(
          TaskType.ReadOnlyClusterCreate,
          TaskType.ReadOnlyClusterDelete,
          TaskType.ReadOnlyKubernetesClusterCreate,
          TaskType.ReadOnlyKubernetesClusterDelete);

  /** The failed create must still hold the universe, and its cluster must still be there. */
  @Override
  public boolean canRollback(TaskInfo failedTask, UUID universeUUID) {
    UUID clusterUUID = getReadOnlyClusterUUID(parseParams(failedTask.getTaskParams()));
    return clusterUUID != null
        && Universe.maybeGet(universeUUID)
            .map(
                u ->
                    u.getUniverseDetails().isOwnedByTask(failedTask.getUuid())
                        && u.getUniverseDetails().getClusterByUuid(clusterUUID) != null)
            .orElse(false);
  }

  @Override
  public RollbackSubmission compute(RollbackContext context) {
    TaskType sourceType = context.getTaskInfo().getTaskType();
    TaskType rollbackType = ROLLBACK_TASK_TYPES.get(sourceType);
    UniverseDefinitionTaskParams createParams = parseParams(context.getOldTaskParams());
    UUID clusterUUID = getReadOnlyClusterUUID(createParams);
    if (rollbackType == null || clusterUUID == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, String.format("Cannot roll back %s: no read replica found", sourceType));
    }
    UniverseDefinitionTaskParams params;
    if (rollbackType == TaskType.ReadOnlyKubernetesClusterDelete) {
      ReadOnlyKubernetesClusterDelete.Params k8sParams =
          new ReadOnlyKubernetesClusterDelete.Params();
      k8sParams.clusterUUID = clusterUUID;
      k8sParams.isForceDelete = true;
      params = k8sParams;
    } else {
      ReadOnlyClusterDelete.Params vmParams = new ReadOnlyClusterDelete.Params();
      vmParams.clusterUUID = clusterUUID;
      vmParams.isForceDelete = true;
      params = vmParams;
    }
    params.setUniverseUUID(createParams.getUniverseUUID());
    params.expectedUniverseVersion = -1;
    return new RollbackSubmission(
        rollbackType, params, CustomerTask.TaskType.Delete, false /* setPreviousTaskUUID */);
  }

  @Nullable
  private static UniverseDefinitionTaskParams parseParams(@Nullable JsonNode taskParams) {
    return taskParams == null
        ? null
        : Json.fromJson(taskParams, UniverseDefinitionTaskParams.class);
  }

  @Nullable
  private static UUID getReadOnlyClusterUUID(@Nullable UniverseDefinitionTaskParams params) {
    if (params == null || params.clusters == null) {
      return null;
    }
    List<Cluster> readOnlyClusters = params.getReadOnlyClusters();
    return readOnlyClusters.isEmpty() ? null : readOnlyClusters.get(0).uuid;
  }
}
