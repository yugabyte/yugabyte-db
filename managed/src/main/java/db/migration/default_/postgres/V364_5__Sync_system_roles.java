// Copyright (c) YugaByte, Inc.

package db.migration.default_.postgres;

import db.migration.default_.common.R__Sync_System_Roles;
import java.sql.SQLException;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;

public class V364_5__Sync_system_roles extends BaseJavaMigration {

  @Override
  public void migrate(Context context) throws SQLException {
    R__Sync_System_Roles.syncSystemRoles();
  }
}
