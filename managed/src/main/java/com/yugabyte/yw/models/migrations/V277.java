package com.yugabyte.yw.models.migrations;

import com.yugabyte.yw.models.FileDataId;
import io.ebean.DB;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.SqlQuery;
import io.ebean.SqlRow;
import io.ebean.SqlUpdate;
import io.ebean.annotation.WhenCreated;
import jakarta.persistence.EmbeddedId;
import jakarta.persistence.Entity;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import lombok.Data;
import play.data.validation.Constraints;

/** Snapshot View of ORM entities at the time migration V277 was added. */
public class V277 {

  @Entity
  @Data
  public static class FileData extends Model {

    @EmbeddedId private FileDataId file;

    @Constraints.Required private UUID parentUUID;

    // The task creation time.
    @WhenCreated private Date timestamp;

    @Constraints.Required private String fileContent;

    private static final Finder<UUID, FileData> find =
        new Finder<UUID, FileData>(FileData.class) {};

    public static List<String> getAllFilePathWithMultipleEntries() {
      SqlQuery query =
          DB.sqlQuery("SELECT file_path FROM file_data GROUP BY file_path having COUNT(*) > 1;");
      List<SqlRow> results = query.findList();

      List<String> filePaths = new ArrayList<>();
      for (SqlRow row : results) {
        filePaths.add(row.getString("file_path"));
      }

      return filePaths;
    }

    public static List<FileData> getAllFilesForAPath(String filePath) {
      return find.query()
          .select("file.file_path, file.extension, timestamp")
          .where()
          .eq("file_path", filePath)
          .order()
          .desc("timestamp")
          .setDistinct(true)
          .findList();
    }

    public static void deleteFileWithPathAndTimestamp(String filePath, Date timestamp) {
      String sql = "DELETE FROM file_data WHERE file_path = :filePath AND timestamp = :timestamp";
      SqlUpdate deleteQuery = DB.sqlUpdate(sql);
      deleteQuery.setParameter("filePath", filePath);
      deleteQuery.setParameter("timestamp", timestamp);
      deleteQuery.execute();
    }
  }
}
