// Copyright (c) YugaByte, Inc.

package db.migration.default_.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.BooleanNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Common.CloudType;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;
import lombok.extern.slf4j.Slf4j;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;
import play.libs.Json;

/**
 * The V1 create and edit APIs used to accept userIntent.useYbdbInbuiltYbc on VM universes, though
 * inbuilt YBC exists only in the Kubernetes DB image. On a VM the flag blocks rollback to a version
 * without inbuilt YBC, and the only API that clears it is Kubernetes-only. It also makes backups
 * and restores skip the YBC upgrade and restore-overwrite validation. Those APIs now reject it.
 *
 * <p>For a universe whose primary cluster is on VMs, this clears every copy of the flag that can be
 * written back to the universe: its details, the edit rollback checkpoint in
 * state_transition_details, and the params of the task that owns the universe, which is the only
 * one that can be retried. Retries skip the API checks. Every copy in these goes, as the checkpoint
 * can split a cluster into old and new values, and nothing reads a read replica's copy.
 *
 * <p>A universe whose primary cluster has a kubernetes, unknown or missing provider type is left
 * alone: clearing the flag on a Kubernetes universe would make YBA manage a YBC that ships in the
 * DB image.
 */
@Slf4j
public class V482__Clear_Ybdb_Inbuilt_Ybc_On_Non_K8s extends BaseJavaMigration {
  private static final String FLAG = "useYbdbInbuiltYbc";

  @Override
  public void migrate(Context context) throws SQLException {
    migrate(context.getConnection());
  }

  static void migrate(Connection connection) throws SQLException {
    try (Statement select = connection.createStatement();
        ResultSet resultSet =
            select.executeQuery(
                "SELECT universe_uuid, universe_details_json, state_transition_details"
                    + " FROM universe");
        PreparedStatement updateDetails =
            connection.prepareStatement(
                "UPDATE universe SET universe_details_json = ? WHERE universe_uuid = ?::uuid");
        PreparedStatement updateTransition =
            connection.prepareStatement(
                "UPDATE universe SET state_transition_details = ?::json"
                    + " WHERE universe_uuid = ?::uuid");
        PreparedStatement selectTask =
            connection.prepareStatement("SELECT task_params FROM task_info WHERE uuid = ?::uuid");
        PreparedStatement updateTask =
            connection.prepareStatement(
                "UPDATE task_info SET task_params = ?::json WHERE uuid = ?::uuid")) {
      while (resultSet.next()) {
        String universeUUID = resultSet.getString("universe_uuid");
        JsonNode details = Json.parse(resultSet.getString("universe_details_json"));
        if (!isKnownNonK8sUniverse(details)) {
          continue;
        }
        if (clearFlag(details)) {
          log.info("Universe {}: clearing {} in universe details", universeUUID, FLAG);
          execute(updateDetails, details, universeUUID);
        }
        String transition = resultSet.getString("state_transition_details");
        if (transition != null) {
          JsonNode transitionJson = Json.parse(transition);
          if (clearFlag(transitionJson)) {
            log.info("Universe {}: clearing {} in state transition details", universeUUID, FLAG);
            execute(updateTransition, transitionJson, universeUUID);
          }
        }
        for (String taskUUID : owningTaskUuids(details)) {
          selectTask.setString(1, taskUUID);
          try (ResultSet task = selectTask.executeQuery()) {
            if (!task.next() || task.getString("task_params") == null) {
              continue;
            }
            JsonNode params = Json.parse(task.getString("task_params"));
            if (clearFlag(params)) {
              log.info("Universe {}: clearing {} in task {}", universeUUID, FLAG, taskUUID);
              execute(updateTask, params, taskUUID);
            }
          }
        }
      }
    }
  }

  private static void execute(PreparedStatement update, JsonNode json, String uuid)
      throws SQLException {
    update.setString(1, Json.stringify(json));
    update.setString(2, uuid);
    update.execute();
  }

  static boolean isKnownNonK8sUniverse(JsonNode universeDetails) {
    if (universeDetails == null) {
      return false;
    }
    for (JsonNode cluster : universeDetails.path("clusters")) {
      if ("PRIMARY".equals(cluster.path("clusterType").asText(null))) {
        return isKnownNonK8s(cluster.path("userIntent"));
      }
    }
    return false;
  }

  // Mirrors UserIntent.getAllCloudTypes: multicloud clusters keep a type per provider spec.
  private static boolean isKnownNonK8s(JsonNode userIntent) {
    List<String> types = new ArrayList<>();
    JsonNode specs = userIntent.path("providerSpecifications");
    if (specs.isArray() && specs.size() > 0) {
      specs.forEach(spec -> types.add(spec.path("providerType").asText(null)));
    } else {
      types.add(userIntent.path("providerType").asText(null));
    }
    return types.stream()
        .allMatch(
            t ->
                t != null
                    && !CloudType.kubernetes.name().equals(t)
                    && !CloudType.unknown.name().equals(t));
  }

  // Only the owning task can be retried (CustomerTaskManager.isTaskRetryable).
  static Set<String> owningTaskUuids(JsonNode universeDetails) {
    Set<String> uuids = new LinkedHashSet<>();
    for (String field : List.of("updatingTaskUUID", "placementModificationTaskUuid")) {
      String uuid = universeDetails.path(field).asText(null);
      if (uuid != null) {
        uuids.add(uuid);
      }
    }
    return uuids;
  }

  /**
   * Sets every useYbdbInbuiltYbc in the tree to false. A checkpoint delta node holding the old and
   * new values is replaced by a plain false, so both reconstruct as false.
   */
  static boolean clearFlag(JsonNode node) {
    boolean cleared = false;
    if (node instanceof ObjectNode && node.has(FLAG) && !BooleanNode.FALSE.equals(node.get(FLAG))) {
      ((ObjectNode) node).put(FLAG, false);
      cleared = true;
    }
    for (JsonNode child : node) {
      cleared |= clearFlag(child);
    }
    return cleared;
  }
}
