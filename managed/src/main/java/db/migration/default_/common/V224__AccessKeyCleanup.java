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

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AccessKey.MigratedKeyInfoFields;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import io.ebean.Ebean;
import java.sql.Connection;
import java.util.Optional;
import lombok.ToString;
import lombok.extern.slf4j.Slf4j;
import org.flywaydb.core.api.migration.jdbc.BaseJdbcMigration;
import play.libs.Json;

@Slf4j
public class V224__AccessKeyCleanup extends BaseJdbcMigration {

  @JsonIgnoreProperties(ignoreUnknown = true)
  @ToString
  public static class AccessKeyTmpDto {
    private final MigratedKeyInfoFields keyInfo;

    public AccessKeyTmpDto(JsonNode keyInfoJson) {
      log.debug("AccessKey.KeyInfo:\n" + keyInfoJson.toPrettyString());
      this.keyInfo = Json.fromJson(keyInfoJson, MigratedKeyInfoFields.class);
    }
  }

  @Override
  public void migrate(Connection connection) throws Exception {
    Ebean.execute(V224__AccessKeyCleanup::migrateAllAccessKeys);
  }

  public static void migrateAllAccessKeys() {
    for (Customer customer : Customer.getAll()) {
      for (Provider provider : Provider.getAll(customer.uuid)) {
        if (provider.details == null) {
          provider.details = new ProviderDetails();
        }
        final Optional<AccessKeyTmpDto> optAcccessKey =
            AccessKey.getLatestAccessKeyQuery(provider.uuid)
                .select("keyInfo")
                .asDto(AccessKeyTmpDto.class)
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
