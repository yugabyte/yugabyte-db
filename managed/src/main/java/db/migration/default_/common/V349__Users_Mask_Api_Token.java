// Copyright (c) YugaByte, Inc.

package db.migration.default_.common;

import com.yugabyte.yw.common.encryption.HashBuilder;
import com.yugabyte.yw.common.encryption.bc.BcOpenBsdHasher;
import com.yugabyte.yw.models.migrations.V349.Users;
import io.ebean.DB;
import java.util.List;
import lombok.extern.slf4j.Slf4j;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;

@Slf4j
public class V349__Users_Mask_Api_Token extends BaseJavaMigration {

  private static final HashBuilder hasher = new BcOpenBsdHasher();

  @Override
  public void migrate(Context context) {
    DB.execute(V349__Users_Mask_Api_Token::maskApiToken);
  }

  public static void maskApiToken() {
    List<Users> users = Users.getAll();
    for (Users user : users) {
      String userApiToken = user.getApiToken();
      if (userApiToken != null) {
        String hashedApiToken = hasher.hash(userApiToken);
        user.setApiToken(hashedApiToken);
        user.save();
        log.info("Running migration: Masked API token for user UUID '{}'.", user.getUuid());
      }
    }
  }
}
