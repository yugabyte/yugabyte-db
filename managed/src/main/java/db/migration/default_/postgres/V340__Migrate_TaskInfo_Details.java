// Copyright (c) YugaByte, Inc.

package db.migration.default_.postgres;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Types;
import java.util.UUID;
import org.apache.commons.lang3.StringUtils;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;
import play.libs.Json;

public class V340__Migrate_TaskInfo_Details extends BaseJavaMigration {

  @Override
  public void migrate(Context context) throws SQLException {
    Connection connection = context.getConnection();
    PreparedStatement updateTaskInfoStmt =
        connection.prepareStatement("UPDATE task_info SET details = ? WHERE uuid = ?");
    ResultSet taskInfoRes =
        connection.createStatement().executeQuery("SELECT uuid, task_params from task_info");
    while (taskInfoRes.next()) {
      ObjectNode newDetailsNode = Json.newObject();
      ObjectNode newErrNode = Json.newObject();
      String taskUuid = taskInfoRes.getString("uuid");
      String details = taskInfoRes.getString("task_params");
      if (StringUtils.isEmpty(details)) {
        // No need to update as there is no error.
        continue;
      }
      JsonNode detailsNode = Json.parse(details);
      JsonNode errNode = detailsNode.get("errorString");
      if (errNode == null || errNode.isNull()) {
        // No need to update as there is no error.
        continue;
      }
      String errMsg = errNode.asText();
      String code = "INTERNAL_ERROR";
      if (errMsg.contains("Platform shutdown")) {
        code = "PLATFORM_SHUTDOWN";
      } else if (errMsg.contains("Platform restarted")) {
        code = "PLATFORM_RESTARTED";
      }
      newErrNode.set("code", Json.toJson(code));
      newErrNode.set("message", Json.toJson(errMsg));
      newDetailsNode.set("error", newErrNode);
      updateTaskInfoStmt.setObject(1, Json.stringify(newDetailsNode), Types.OTHER);
      updateTaskInfoStmt.setObject(2, UUID.fromString(taskUuid));
      updateTaskInfoStmt.executeUpdate();
    }
    updateTaskInfoStmt.close();
  }
}
