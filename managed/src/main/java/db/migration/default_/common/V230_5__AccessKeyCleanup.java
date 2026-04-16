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
import io.ebean.DB;
import java.util.Optional;
import lombok.extern.slf4j.Slf4j;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;
import play.libs.Json;

@Slf4j
public class V230_5__AccessKeyCleanup extends BaseJavaMigration {

  @Override
  public void migrate(Context context) {
    DB.execute(V230_5__AccessKeyCleanup::migrateAllAccessKeys);
  }

  public static void migrateAllAccessKeys() {
    for (TmpCustomer customer : TmpCustomer.find.all()) {
      for (TmpProvider provider : TmpProvider.getAll(customer.getUuid())) {
        if (provider.getDetails() == null) {
          provider.setDetails(new TmpProviderDetails());
          provider.save();
        }
        final Optional<TmpAccessKeyDto> optAcccessKey =
            AccessKey.getLatestAccessKeyQuery(provider.getUuid())
                .select("keyInfo")
                .asDto(TmpAccessKeyDto.class)
                .findOneOrEmpty();

        // PLAT-8027:
        // Do not overwrite if there are non-default values in provider.details
        // because the provider is already created with new schema.
        if (new TmpProviderDetails().equals(provider.getDetails())) {
          optAcccessKey.ifPresent(
              latestKey -> {
                provider.getDetails().mergeFrom(latestKey.keyInfo);
                log.debug(
                    "Migrated KeyInfo fields to ProviderDetails:\n"
                        + Json.toJson(provider.getDetails()).toPrettyString());
                provider.save();
              });
        }
      }
    }
  }
}
