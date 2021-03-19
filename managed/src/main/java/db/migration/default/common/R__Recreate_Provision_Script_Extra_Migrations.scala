// Copyright (c) YugaByte, Inc.

package db.migration.default.common

import java.sql.Connection
import java.util.UUID
import play.api.libs.json._

import org.flywaydb.core.api.migration.jdbc.JdbcMigration

class R__Recreate_Provision_Script_Extra_Migrations extends JdbcMigration {
  override def migrate(connection: Connection): Unit = {
    connection.createStatement().execute(s"INSERT INTO extra_migration VALUES " +
      s"('R__Recreate_Provision_Script_Extra_Migrations')")
  }
}
