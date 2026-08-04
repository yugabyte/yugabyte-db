// Copyright (c) YugaByte, Inc.
package db.migration.default_.common;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.yugabyte.yw.common.RedactingService;
import java.sql.SQLException;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;
import scala.util.hashing.MurmurHash3;

public class R__Redact_Secrets_From_Audit extends BaseJavaMigration {

  public void migrate(Context context) throws SQLException, JsonProcessingException {
    // Do nothing but the checksum is upgraded.
  }

  @Override
  public Integer getChecksum() {
    int codeChecksum = 82918230; // Change me if you want to force migration to run
    MurmurHash3 murmurHash3 = new MurmurHash3();
    return murmurHash3.arrayHash(RedactingService.SECRET_PATHS_FOR_LOGS.toArray(), codeChecksum);
  }
}
