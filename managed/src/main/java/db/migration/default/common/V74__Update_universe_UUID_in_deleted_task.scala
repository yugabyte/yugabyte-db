// Copyright (c) YugaByte, Inc.
package db.migration.default.common

import java.sql.Connection
import org.flywaydb.core.api.migration.jdbc.JdbcMigration

class V74__Update_universe_UUID_in_deleted_task extends JdbcMigration {

  override def migrate(connection: Connection): Unit = {
    connection.createStatement().execute(s"UPDATE customer_task as ta " +
        s"SET target_uuid = (b.backup_info->>'universeUUID')::uuid " +
        s"FROM backup as b WHERE b.customer_uuid=ta.customer_uuid AND ta.target_uuid=b.backup_uuid " +
        s"AND ta.target_type='Backup' AND ta.type='Delete';")
  }
}

