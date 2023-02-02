// Copyright (c) YugaByte, Inc.

package db.migration.default_.common;

import java.sql.Connection;
import java.util.List;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.kms.util.HashicorpEARServiceUtil;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.common.kms.util.hashicorpvault.HashicorpVaultConfigParams;
import com.yugabyte.yw.models.KmsConfig;
import io.ebean.Ebean;
import lombok.extern.slf4j.Slf4j;

import org.flywaydb.core.api.migration.jdbc.BaseJdbcMigration;

@Slf4j
public class V235__HashicorpVaultAddKeyName extends BaseJdbcMigration {

  @Override
  public void migrate(Connection connection) throws Exception {
    Ebean.execute(V235__HashicorpVaultAddKeyName::addKeyNameToAuthConfig);
  }

  public static void addKeyNameToAuthConfig() {
    List<KmsConfig> kmsConfigs = KmsConfig.listAllKMSConfigs();
    for (KmsConfig kmsConfig : kmsConfigs) {
      if (KeyProvider.HASHICORP.equals(kmsConfig.keyProvider)) {
        V235__HashicorpVaultAddKeyName.populateKeyNameIfNotExists(kmsConfig);
      }
    }
  }

  public static void populateKeyNameIfNotExists(KmsConfig kmsConfig) {
    ObjectNode authConfig = kmsConfig.authConfig;
    if (!authConfig.has(HashicorpVaultConfigParams.HC_VAULT_KEY_NAME)) {
      authConfig.put(
          HashicorpVaultConfigParams.HC_VAULT_KEY_NAME, HashicorpEARServiceUtil.HC_VAULT_EKE_NAME);
      kmsConfig.authConfig = authConfig;
      kmsConfig.save();
      log.info(
          "Running Hashicorp migration: Added key name to config UUID '{}'.", kmsConfig.configUUID);
    }
  }
}
