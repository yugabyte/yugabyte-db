// Copyright (c) Yugabyte, Inc.

package db.migration.default_.postgres;

import com.yugabyte.yw.models.migrations.V360.Principal;
import com.yugabyte.yw.models.migrations.V360.Users;
import java.sql.SQLException;
import lombok.extern.slf4j.Slf4j;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;

@Slf4j
public class V361__Populate_Princpals extends BaseJavaMigration {

  @Override
  public void migrate(Context context) throws SQLException {
    for (Users user : Users.find.all()) {
      Principal principal = Principal.get(user.getUuid());
      if (principal == null) {
        log.info("Adding Principal entry for user with UUID: " + user.getUuid());
        new Principal(user).save();
      }
    }
  }
}
