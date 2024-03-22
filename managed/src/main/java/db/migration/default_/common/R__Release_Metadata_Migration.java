package db.migration.default_.common;

import com.fasterxml.jackson.core.JsonProcessingException;
import io.ebean.DB;
import jakarta.persistence.PersistenceException;
import java.sql.Connection;
import java.sql.SQLException;
import lombok.extern.slf4j.Slf4j;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;
import scala.util.hashing.MurmurHash3;

@Slf4j
public class R__Release_Metadata_Migration extends BaseJavaMigration {

  public void migrate(Context context) throws SQLException, JsonProcessingException {
    Connection connection = context.getConnection();
    connection
        .createStatement()
        .execute("INSERT INTO extra_migration VALUES " + "('R__Release_Metadata_Migration')");
  }

  @Override
  public Integer getChecksum() {
    int codeChecksum = 82918230; // Change me if you want to force migration to run
    MurmurHash3 murmurHash3 = new MurmurHash3();
    try {
      String jsonStr =
          DB.sqlQuery("SELECT value FROM yugaware_property WHERE name='SoftwareReleases'")
              .findOne()
              .getString("value");
      return murmurHash3.stringHash(jsonStr, codeChecksum);
    } catch (PersistenceException e) {
      log.warn("failed to query yugaware property", e);
      return super.getChecksum();
    }
  }
}
