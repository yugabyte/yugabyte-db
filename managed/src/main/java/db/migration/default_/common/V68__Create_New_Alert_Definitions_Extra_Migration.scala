// Copyright (c) YugaByte, Inc.
package db.migration.default_.common

import org.flywaydb.core.api.migration.{BaseJavaMigration, Context}

class V68__Create_New_Alert_Definitions_Extra_Migration extends BaseJavaMigration {
  override def migrate(context: Context): Unit = {
    // Can't remove migration, just leave it here
  }
}
