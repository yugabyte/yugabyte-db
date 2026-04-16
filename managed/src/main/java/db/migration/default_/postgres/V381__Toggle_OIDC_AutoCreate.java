package db.migration.default_.postgres;

import static com.yugabyte.yw.models.ScopedRuntimeConfig.GLOBAL_SCOPE_UUID;

import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.models.migrations.V381.RuntimeConfigEntry;
import java.sql.SQLException;
import lombok.extern.slf4j.Slf4j;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;

@Slf4j
public class V381__Toggle_OIDC_AutoCreate extends BaseJavaMigration {

  @Override
  public void migrate(Context context) throws SQLException {
    RuntimeConfigEntry autoCreate =
        RuntimeConfigEntry.find
            .query()
            .where()
            .eq("path", GlobalConfKeys.enableOidcAutoCreateUser.getKey())
            .findOne();
    // No-op if the key is already overriden.
    if (autoCreate != null) {
      log.info("OIDC auto create users already setup. Skipping additional config changes.");
      return;
    }
    RuntimeConfigEntry useOauth =
        RuntimeConfigEntry.find
            .query()
            .where()
            .eq("path", GlobalConfKeys.useOauth.getKey())
            .findOne();
    // If this key is present, it means OIDC was configured sometime in the past, so
    // default should be off.
    if (useOauth != null) {
      log.info("Previous OIDC config found. Disabling auto create users.");
      autoCreate =
          new RuntimeConfigEntry(
              GLOBAL_SCOPE_UUID, GlobalConfKeys.enableOidcAutoCreateUser.getKey(), "false");
      autoCreate.save();
    }
  }
}
