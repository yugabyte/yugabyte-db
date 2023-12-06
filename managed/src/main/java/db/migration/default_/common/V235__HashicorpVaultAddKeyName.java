// Copyright (c) YugaByte, Inc.

package db.migration.default_.common;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.kms.util.HashicorpEARServiceUtil;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.common.kms.util.hashicorpvault.HashicorpVaultConfigParams;
import com.yugabyte.yw.models.KmsConfig;
import io.ebean.DB;
import java.util.List;
import lombok.extern.slf4j.Slf4j;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;

@Slf4j
public class V235__HashicorpVaultAddKeyName extends BaseJavaMigration {

  @Override
  public void migrate(Context context) {
    DB.execute(V235__HashicorpVaultAddKeyName::addKeyNameToAuthConfig);
  }

  public static void addKeyNameToAuthConfig() {
    List<KmsConfig> kmsConfigs = KmsConfig.listAllKMSConfigs();
    for (KmsConfig kmsConfig : kmsConfigs) {
      if (KeyProvider.HASHICORP.equals(kmsConfig.getKeyProvider())) {
        V235__HashicorpVaultAddKeyName.populateKeyNameIfNotExists(kmsConfig);
      }
    }
  }

  public static void populateKeyNameIfNotExists(KmsConfig kmsConfig) {
    ObjectNode authConfig = kmsConfig.getAuthConfig();
    if (!authConfig.has(HashicorpVaultConfigParams.HC_VAULT_KEY_NAME)) {
      authConfig.put(
          HashicorpVaultConfigParams.HC_VAULT_KEY_NAME, HashicorpEARServiceUtil.HC_VAULT_EKE_NAME);
      kmsConfig.setAuthConfig(authConfig);
      kmsConfig.save();
      log.info(
          "Running Hashicorp migration: Added key name to config UUID '{}'.",
          kmsConfig.getConfigUUID());
    }
  }
}
