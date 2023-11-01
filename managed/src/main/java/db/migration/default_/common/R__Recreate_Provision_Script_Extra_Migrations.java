// Copyright (c) YugaByte, Inc.

package db.migration.default_.common;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.yugabyte.yw.common.AppConfigHelper;
import java.math.BigInteger;
import java.nio.file.Files;
import java.nio.file.NoSuchFileException;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.security.MessageDigest;
import java.sql.Connection;
import java.sql.SQLException;
import lombok.extern.slf4j.Slf4j;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;
import scala.util.hashing.MurmurHash3;

@Slf4j
public class R__Recreate_Provision_Script_Extra_Migrations extends BaseJavaMigration {

  public void migrate(Context context) throws SQLException, JsonProcessingException {
    Connection connection = context.getConnection();
    connection
        .createStatement()
        .execute(
            "INSERT INTO extra_migration VALUES "
                + "('R__Recreate_Provision_Script_Extra_Migrations')");
  }

  @Override
  public Integer getChecksum() {
    int codeChecksum = 82918230; // Change me if you want to force migration to run
    MurmurHash3 murmurHash3 = new MurmurHash3();
    final String templateHash = getProvisionTemplateHash();
    if (templateHash == null) {
      return super.getChecksum();
    }
    return murmurHash3.stringHash(templateHash, codeChecksum);
  }

  private String getProvisionTemplateHash() {
    String devopsHome = AppConfigHelper.getDevopsHomePath();

    final String filePath = "opscli/ybops/data/provision_instance.py.j2";
    final Path fullPath = Paths.get(devopsHome, filePath);
    log.debug("checking provision_instance at {}", fullPath.toString());
    try {
      byte[] data = Files.readAllBytes(fullPath);
      byte[] hash = MessageDigest.getInstance("MD5").digest(data);
      String checksum = new BigInteger(1, hash).toString(16);
      return checksum;
    } catch (NoSuchFileException e) {
      log.warn("no provision_instance.py.j2 found", e);
      return null;
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }
}
