// Copyright (c) YugaByte, Inc.
package db.migration.default_.common;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.RedactingService;
import com.yugabyte.yw.common.RedactingService.RedactionTarget;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import org.apache.commons.lang3.StringUtils;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;
import scala.util.hashing.MurmurHash3;

public class R__Redact_Secrets_From_Audit extends BaseJavaMigration {

  public void migrate(Context context) throws SQLException, JsonProcessingException {
    Connection connection = context.getConnection();
    String selectStmt = "SELECT id, payload FROM audit";
    ResultSet resultSet = connection.createStatement().executeQuery(selectStmt);

    PreparedStatement updateStatement =
        connection.prepareStatement("UPDATE audit SET payload = ? WHERE id = ?");
    while (resultSet.next()) {
      long id = resultSet.getLong("id");
      String payloadStr = resultSet.getString("payload");
      if (StringUtils.isNotEmpty(payloadStr)) {
        JsonNode payload = play.libs.Json.parse(payloadStr);
        JsonNode newPayload = RedactingService.filterSecretFields(payload, RedactionTarget.LOGS);
        if (!payload.equals(newPayload)) {
          updateStatement.setString(1, play.libs.Json.stringify(newPayload));
          updateStatement.setLong(2, id);
          updateStatement.executeUpdate();
        }
      }
    }
  }

  @Override
  public Integer getChecksum() {
    int codeChecksum = 82918230; // Change me if you want to force migration to run
    MurmurHash3 murmurHash3 = new MurmurHash3();
    return murmurHash3.arrayHash(RedactingService.SECRET_PATHS_FOR_LOGS.toArray(), codeChecksum);
  }
}
