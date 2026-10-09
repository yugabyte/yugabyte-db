// Copyright (c) YugabyteDB, Inc.

package db.migration.default_.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import lombok.extern.slf4j.Slf4j;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;
import play.libs.Json;

/**
 * Backfills the {@code creationSucceeded} flag on {@code universe_details_json} for every existing
 * universe.
 *
 * <p>The flag was introduced to distinguish "universe was successfully brought up at least once"
 * from "the most recent operation on this universe succeeded" (which is what {@code
 * updateSucceeded} tracks). Existing universes have already been running in production and
 * receiving health checks and alerts, so we preserve that behavior by marking them all as created
 * regardless of the current {@code updateSucceeded} value. {@code CustomerTask} history is
 * unreliable here because those rows may have been garbage-collected.
 *
 * <p>Uses raw JDBC (like V403) rather than an Ebean snapshot model. Ebean-based updates via a
 * stripped-down entity have historically been flaky for this table because the dirty-tracking
 * enhancement doesn't always fire when the entity only declares a couple of columns, which leaves
 * the JSON silently unchanged in production DBs while unit tests happily pass.
 */
@Slf4j
public class V456__Backfill_Universe_Creation_Succeeded extends BaseJavaMigration {

  @Override
  public void migrate(Context context) throws SQLException {
    log.info("Backfilling creationSucceeded flag on existing universes");
    Connection connection = context.getConnection();
    String selectStmt = "SELECT universe_uuid, universe_details_json FROM universe";
    int updated = 0;
    int skipped = 0;
    try (PreparedStatement select = connection.prepareStatement(selectStmt);
        ResultSet resultSet = select.executeQuery();
        PreparedStatement update =
            connection.prepareStatement(
                "UPDATE universe SET universe_details_json = ? WHERE universe_uuid = ?::uuid")) {
      while (resultSet.next()) {
        String universeUuid = resultSet.getString("universe_uuid");
        String detailsJson = resultSet.getString("universe_details_json");
        if (detailsJson == null) {
          log.warn("Skipping universe {} - null universe_details_json", universeUuid);
          skipped++;
          continue;
        }
        try {
          JsonNode parsed = Json.parse(detailsJson);
          if (!(parsed instanceof ObjectNode)) {
            log.warn(
                "Skipping universe {} - unexpected non-object universe_details_json", universeUuid);
            skipped++;
            continue;
          }
          ObjectNode details = (ObjectNode) parsed;
          JsonNode existing = details.get("creationSucceeded");
          if (existing != null && existing.isBoolean() && existing.asBoolean()) {
            skipped++;
            continue;
          }
          details.put("creationSucceeded", true);
          update.setString(1, Json.stringify(details));
          update.setString(2, universeUuid);
          update.execute();
          updated++;
        } catch (Exception e) {
          log.warn(
              "Failed to backfill creationSucceeded for universe {}: {}",
              universeUuid,
              e.getMessage());
          skipped++;
        }
      }
    }
    log.info(
        "Finished backfilling creationSucceeded flag: {} universe(s) updated, {} skipped",
        updated,
        skipped);
  }
}
