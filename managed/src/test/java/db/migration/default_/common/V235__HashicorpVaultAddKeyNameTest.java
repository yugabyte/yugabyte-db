/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package db.migration.default_.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.common.kms.util.hashicorpvault.HashicorpVaultConfigParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.KmsConfig;
import org.junit.Before;
import org.junit.Test;

public class V235__HashicorpVaultAddKeyNameTest extends FakeDBApplication {

  KmsConfig fakeKmsConfig;

  @Before
  public void setup() {
    Customer testCustomer = ModelFactory.testCustomer();
    ObjectNode fakeAuthConfig = TestHelper.getFakeKmsAuthConfig(KeyProvider.HASHICORP);

    // Insert the key metadata from the service and compare.
    fakeKmsConfig =
        KmsConfig.createKMSConfig(
            testCustomer.getUuid(),
            KeyProvider.HASHICORP,
            fakeAuthConfig,
            fakeAuthConfig.get("name").asText());
  }

  @Test
  public void checkMigrationNoKeyName() {
    // Verify that "HC_VAULT_KEY_NAME" is not initially present in the authConfig object.
    ObjectNode authConfigUpdated = KmsConfig.getKMSAuthObj(fakeKmsConfig.getConfigUUID());
    assertFalse(authConfigUpdated.has("HC_VAULT_KEY_NAME"));

    V235__HashicorpVaultAddKeyName.addKeyNameToAuthConfig();

    // Verify if "HC_VAULT_KEY_NAME" got added in the authConfig object.
    authConfigUpdated = KmsConfig.getKMSAuthObj(fakeKmsConfig.getConfigUUID());
    assertTrue(authConfigUpdated.has("HC_VAULT_KEY_NAME"));
  }

  @Test
  public void checkMigrationHasKeyName() {
    String keyName = "modified_key_name";
    ObjectNode authConfigUpdated = KmsConfig.getKMSAuthObj(fakeKmsConfig.getConfigUUID());
    authConfigUpdated.put(HashicorpVaultConfigParams.HC_VAULT_KEY_NAME, keyName);
    KmsConfig testKmsConfig = KmsConfig.get(fakeKmsConfig.getConfigUUID());
    testKmsConfig.setAuthConfig(authConfigUpdated);
    testKmsConfig.save();

    // Verify that the key name is already present in the authConfig object.
    assertEquals(
        keyName, authConfigUpdated.get(HashicorpVaultConfigParams.HC_VAULT_KEY_NAME).asText());

    V235__HashicorpVaultAddKeyName.addKeyNameToAuthConfig();

    authConfigUpdated = KmsConfig.getKMSAuthObj(fakeKmsConfig.getConfigUUID());
    // Verify that the same key name is still present in the authConfig object.
    // Migration should not update the key name to default "key_yugabyte".
    assertEquals(
        keyName, authConfigUpdated.get(HashicorpVaultConfigParams.HC_VAULT_KEY_NAME).asText());
  }
}
