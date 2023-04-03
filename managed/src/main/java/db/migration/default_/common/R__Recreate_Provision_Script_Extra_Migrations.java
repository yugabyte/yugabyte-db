// Copyright (c) YugaByte, Inc.

package db.migration.default_.common;

import com.fasterxml.jackson.core.JsonProcessingException;
import java.sql.Connection;
import java.sql.SQLException;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;

public class R__Recreate_Provision_Script_Extra_Migrations extends BaseJavaMigration {
  public void migrate(Context context) throws SQLException, JsonProcessingException {
    Connection connection = context.getConnection();
    connection
        .createStatement()
        .execute(
            "INSERT INTO extra_migration VALUES "
                + "('R__Recreate_Provision_Script_Extra_Migrations')");
  }
}
