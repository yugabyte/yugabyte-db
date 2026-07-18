package db.migration.default_.postgres;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.yw.models.migrations.V417.Backup;
import com.yugabyte.yw.models.migrations.V417.BackupState;
import io.ebean.DB;
import io.ebean.Transaction;
import java.util.List;
import java.util.stream.StreamSupport;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;

public class V417__Add_First_Snapshot_Time_Backup extends BaseJavaMigration {

  @Override
  public void migrate(Context context) throws Exception {
    List<Backup> backups =
        Backup.find
            .query()
            .where()
            .eq("state", BackupState.Completed)
            .raw(
                "first_snapshot_time = 0 AND (CASE    WHEN"
                    + " json_typeof(backup_info -> 'backupList') = 'array'    THEN"
                    + " json_array_length(backup_info -> 'backupList') > 0    ELSE false  END) AND"
                    + " (backup_info -> 'backupList' -> 0 -> 'backupPointInTimeRestoreWindow') IS"
                    + " NOT NULL")
            .findList();
    ObjectMapper mapper = new ObjectMapper();
    for (Backup backup : backups) {
      JsonNode rootNode = mapper.readTree(backup.getBackupInfo());
      JsonNode backupListNode = rootNode.path("backupList");
      long earliestSnapshotTime =
          StreamSupport.stream(backupListNode.spliterator(), false) // Convert iterator to stream
              .map(
                  bP ->
                      bP.path("backupPointInTimeRestoreWindow")
                          .path("timestampRetentionWindowEndMillis"))
              .filter(JsonNode::isNumber) // Ensure the field exists and is a number
              .mapToLong(JsonNode::asLong)
              .min()
              .getAsLong();
      backup.setFirstSnapshotTime(earliestSnapshotTime);
    }
    try (Transaction transaction = DB.beginTransaction()) {
      DB.updateAll(backups);
      transaction.commit();
    }
  }
}
