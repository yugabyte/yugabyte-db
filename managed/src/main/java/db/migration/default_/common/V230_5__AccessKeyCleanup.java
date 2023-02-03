/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package db.migration.default_.common;

import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.migrations.V230_5.TmpAccessKeyDto;
import com.yugabyte.yw.models.migrations.V230_5.TmpCustomer;
import com.yugabyte.yw.models.migrations.V230_5.TmpProvider;
import com.yugabyte.yw.models.migrations.V230_5.TmpProviderDetails;
import io.ebean.Ebean;
import java.sql.Connection;
import java.util.Optional;
import lombok.extern.slf4j.Slf4j;
import org.flywaydb.core.api.migration.jdbc.BaseJdbcMigration;
import play.libs.Json;

@Slf4j
public class V230_5__AccessKeyCleanup extends BaseJdbcMigration {

  @Override
  public void migrate(Connection connection) {
    Ebean.execute(V230_5__AccessKeyCleanup::migrateAllAccessKeys);
  }

  public static void migrateAllAccessKeys() {
    for (TmpCustomer customer : TmpCustomer.find.all()) {
      for (TmpProvider provider : TmpProvider.getAll(customer.uuid)) {
        if (provider.details == null) {
          provider.details = new TmpProviderDetails();
        }
        final Optional<TmpAccessKeyDto> optAcccessKey =
            AccessKey.getLatestAccessKeyQuery(provider.uuid)
                .select("keyInfo")
                .asDto(TmpAccessKeyDto.class)
                .findOneOrEmpty();
        optAcccessKey.ifPresent(
            latestKey -> {
              provider.details.mergeFrom(latestKey.keyInfo);
              log.debug(
                  "Migrated KeyInfo fields to ProviderDetails:\n"
                      + Json.toJson(provider.details).toPrettyString());
              provider.save();
            });
      }
    }
  }
}
