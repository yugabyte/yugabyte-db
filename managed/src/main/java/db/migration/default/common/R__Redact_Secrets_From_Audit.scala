// Copyright (c) YugaByte, Inc.
package db.migration.default.common

import com.sangupta.murmur.Murmur3
import com.yugabyte.yw.common.audit.AuditService
import org.apache.commons.lang3.StringUtils
import org.flywaydb.core.api.migration.MigrationChecksumProvider

import java.sql.Connection
import org.flywaydb.core.api.migration.jdbc.JdbcMigration

import java.util.Objects
import scala.util.hashing.MurmurHash3

class R__Redact_Secrets_From_Audit extends JdbcMigration with MigrationChecksumProvider {

  override def migrate(connection: Connection): Unit = {
    val selectStmt = "SELECT id, payload FROM audit"
    val resultSet = connection.createStatement().executeQuery(selectStmt)

    while (resultSet.next()) {
      val id = resultSet.getString("id")
      val payloadStr = resultSet.getString("payload")
      if (StringUtils.isNotEmpty(payloadStr)) {
        val payload = play.libs.Json.parse(payloadStr);
        val newPayload = AuditService.filterSecretFields(payload)
        if (!payload.equals(newPayload)) {
          connection.createStatement().execute(s"UPDATE audit SET payload = " +
            s"'${play.libs.Json.stringify(newPayload)}' WHERE id = '$id'")
        }
      }
    }
  }

  override def getChecksum: Integer = {
    val codeChecksum: Int = 82918220 // Change me if you want to force migration to run
    val secretPathsChecksum: Int = MurmurHash3.arrayHash(AuditService.SECRET_PATHS.toArray);
    MurmurHash3.arrayHash(Array(codeChecksum, secretPathsChecksum))
  }
}
