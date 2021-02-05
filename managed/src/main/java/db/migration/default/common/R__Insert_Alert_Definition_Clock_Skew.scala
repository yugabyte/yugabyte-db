// Copyright (c) YugaByte, Inc.

import java.sql.Connection
import org.flywaydb.core.api.migration.jdbc.JdbcMigration

class R__Insert_Alert_Definition_Clock_Skew extends JdbcMigration {

  override def migrate(connection: Connection): Unit = {
    // The migration exists in 2.4.x.
    // Here it is empty to avoid an exception of 'Missing migration found'.
  }
}
