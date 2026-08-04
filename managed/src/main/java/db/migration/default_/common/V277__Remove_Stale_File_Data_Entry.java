package db.migration.default_.common;

import com.yugabyte.yw.models.migrations.V277.FileData;
import io.ebean.DB;
import java.util.List;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;

/*
 * Prior to this migration we were not having the constraint to ensure
 * a single entry exist for a given file path in the file_data table,
 * which can lead to problems in case we re-create the FS state from the DB.
 *
 * The aim of this migration is to remove the entry from the file_data table
 * in case of multiple entry for a given file path keeping only the latest entry.
 *
 * V277 migration adds the constraints on `file_path` column so as to ensure we do
 * not allow this behaviour in future.
 */
public class V277__Remove_Stale_File_Data_Entry extends BaseJavaMigration {

  @Override
  public void migrate(Context context) {
    DB.execute(V277__Remove_Stale_File_Data_Entry::removeStaleEntry);
  }

  public static void removeStaleEntry() {
    for (String filePath : FileData.getAllFilePathWithMultipleEntries()) {
      List<FileData> files = FileData.getAllFilesForAPath(filePath);
      // Delete all the files but the first.

      for (FileData file : files.subList(1, files.size())) {
        FileData.deleteFileWithPathAndTimestamp(file.getFile().getFilePath(), file.getTimestamp());
      }
    }
  }
}
