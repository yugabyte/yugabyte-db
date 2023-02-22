// Copyright (c) YugaByte, Inc.

package db.migration.default_.common

import org.flywaydb.core.api.migration.{BaseJavaMigration, Context}

class R__Recreate_Provision_Script_Extra_Migrations extends BaseJavaMigration {
  override def migrate(context: Context): Unit = {
    val connection = context.getConnection
    connection.createStatement().execute(s"INSERT INTO extra_migration VALUES " +
      s"('R__Recreate_Provision_Script_Extra_Migrations')")
  }
}
