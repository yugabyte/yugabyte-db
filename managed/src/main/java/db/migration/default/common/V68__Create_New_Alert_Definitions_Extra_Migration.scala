// Copyright (c) YugaByte, Inc.
package db.migration.default.common

import java.sql.Connection
import org.flywaydb.core.api.migration.jdbc.JdbcMigration

class V68__Create_New_Alert_Definitions_Extra_Migration extends JdbcMigration {

  override def migrate(connection: Connection): Unit = {
    connection.createStatement().execute(s"INSERT INTO extra_migration VALUES " +
      s"('V68__Create_New_Alert_Definitions_Extra_Migration')")
  }
}
