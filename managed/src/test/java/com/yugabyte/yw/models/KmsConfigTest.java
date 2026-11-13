/*
 * Copyright 2019 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.models;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import io.ebean.DB;
import java.util.List;
import java.util.UUID;
import org.junit.Test;
import play.libs.Json;

public class KmsConfigTest extends FakeDBApplication {
  @Test
  public void testCreateAndListConfig() {
    UUID customerUUID = UUID.randomUUID();
    KmsConfig config =
        KmsConfig.createKMSConfig(
            customerUUID,
            KeyProvider.AWS,
            Json.newObject().put("test_key", "test_val"),
            "some config name");
    assertEquals(1, KmsConfig.listKMSConfigs(customerUUID).size());
  }

  @Test
  public void testListAllWithoutAuthConfig() {
    KmsConfig created =
        KmsConfig.createKMSConfig(
            UUID.randomUUID(),
            KeyProvider.HASHICORP,
            Json.newObject().put("test_key", "test_val"),
            "vault config");

    List<KmsConfig> configs = KmsConfig.listAllKMSConfigsWithoutAuthConfig();
    assertEquals(1, configs.size());
    KmsConfig listed = configs.get(0);
    assertEquals(created.getConfigUUID(), listed.getConfigUUID());
    assertEquals("vault config", listed.getName());
    assertEquals(KeyProvider.HASHICORP, listed.getKeyProvider());
    assertFalse(DB.beanState(listed).loadedProps().contains("authConfig"));
    assertEquals(
        "test_val", KmsConfig.getKMSAuthObj(listed.getConfigUUID()).get("test_key").asText());
  }
}
