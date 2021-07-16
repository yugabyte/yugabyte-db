// Copyright (c) YugaByte, Inc.
package db.migration.default.common

import java.sql.Connection

import com.yugabyte.yw.common.audit.AuditService
import org.apache.commons.lang3.StringUtils
import org.flywaydb.core.api.migration.MigrationChecksumProvider
import org.flywaydb.core.api.migration.jdbc.JdbcMigration

import scala.util.hashing.MurmurHash3

class R__Redact_Secrets_From_Audit extends JdbcMigration with MigrationChecksumProvider {

  override def migrate(connection: Connection): Unit = {
    val selectStmt = "SELECT id, payload FROM audit"
    val resultSet = connection.createStatement().executeQuery(selectStmt)

    val updateStatement = connection.prepareStatement("UPDATE audit SET payload = ? WHERE id = ?");
    while (resultSet.next()) {
      val id = resultSet.getLong("id")
      val payloadStr = resultSet.getString("payload")
      if (StringUtils.isNotEmpty(payloadStr)) {
        val payload = play.libs.Json.parse(payloadStr);
        val newPayload = AuditService.filterSecretFields(payload)
        if (!payload.equals(newPayload)) {
          updateStatement.setString(1, play.libs.Json.stringify(newPayload))
          updateStatement.setLong(2, id)
          updateStatement.executeUpdate()
        }
      }
    }
  }

  override def getChecksum: Integer = {
    val codeChecksum: Int = 82918230 // Change me if you want to force migration to run
    val secretPathsChecksum: Int = MurmurHash3.arrayHash(AuditService.SECRET_PATHS.toArray);
    MurmurHash3.arrayHash(Array(codeChecksum, secretPathsChecksum))
  }
}
