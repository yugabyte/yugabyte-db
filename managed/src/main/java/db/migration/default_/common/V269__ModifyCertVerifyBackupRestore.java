// Copyright (c) YugaByte, Inc.

package db.migration.default_.common;

import java.sql.Connection;
import java.sql.ResultSet;
import lombok.extern.slf4j.Slf4j;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;

@Slf4j
public class V269__ModifyCertVerifyBackupRestore extends BaseJavaMigration {

  private final String CERT_VERIFY_CONFIG = "yb.certVerifyBackupRestore.is_enforced";

  @Override
  public void migrate(Context context) throws Exception {
    Connection connection = context.getConnection();
    String findQuery =
        String.format(
            "SELECT value, path from runtime_config_entry where path='%s'", CERT_VERIFY_CONFIG);
    ResultSet runtimeEntry = connection.createStatement().executeQuery(findQuery);
    if (runtimeEntry.next()) {
      String path = runtimeEntry.getString("path");
      log.debug("{} runtime config already exists. Not overriding", path);
    } else {
      ResultSet storageConfig =
          connection
              .createStatement()
              .executeQuery(
                  "SELECT config_uuid, config_name from customer_config where type='STORAGE'"
                      + "and name='S3' and state='Active'");
      if (storageConfig.next()) {
        // If there's a storage config then we want to ensure that YBA upgrades does not render a
        // working storage config unusable.
        log.warn("Disabling server cert verification during backups as S3 storage config exists.");
        String queryLine =
            String.format(
                "INSERT into runtime_config_entry(scope_uuid, path, value) VALUES "
                    + "('00000000-0000-0000-0000-000000000000', '%s', 'false')",
                CERT_VERIFY_CONFIG);
        log.debug("Executing query {}", queryLine);
        connection.createStatement().executeUpdate(queryLine);
      } else {
        log.info("No storage config exists. Enforcing {} by default.", CERT_VERIFY_CONFIG);
      }
    }
  }
}
