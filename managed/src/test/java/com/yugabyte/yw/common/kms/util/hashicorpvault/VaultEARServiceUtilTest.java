/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/
 *  POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.kms.util.hashicorpvault;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil.KeyType;
import com.yugabyte.yw.common.kms.util.HashicorpEARServiceUtil;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.models.KmsConfig;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

public class VaultEARServiceUtilTest extends FakeDBApplication {
  protected static final Logger LOG = LoggerFactory.getLogger(VaultEARServiceUtilTest.class);

  /**
   * MOCK_RUN: UTs written for Hashicorp vault can be run in two modes. If MOCK_RUN set to true, it
   * runs with mocked objects otherwise with running vault.
   *
   * <p>1. using vault at location specified by following parameters pre-req: vaule is already
   * running sealed and have transit enabled. Following parameters are set to connect to given
   * vault. For more info: https://www.vaultproject.io/docs/secrets/transit 2. using mocked objects
   * of Vault and related classes.
   */
  public static final boolean MOCK_RUN = true;

  KeyProvider testKP = KeyProvider.HASHICORP;

  public static final String vaultAddr = "http://127.0.0.1:8200";
  public static final String vaultToken = "s.fj4WtEMQ1MV1fnxQkpXA9ytV";
  public static final String sEngine = "transit";
  public static final String mountPath = "qa_transit/";
  public static final String keyName = "key_yugabyte";

  public static final String jsonString =
      "{"
          + "\""
          + HashicorpVaultConfigParams.HC_VAULT_ADDRESS
          + "\":\""
          + vaultAddr
          + "\",\""
          + HashicorpVaultConfigParams.HC_VAULT_TOKEN
          + "\":\""
          + vaultToken
          + "\",\""
          + HashicorpVaultConfigParams.HC_VAULT_ENGINE
          + "\":\""
          + sEngine
          + "\",\""
          + HashicorpVaultConfigParams.HC_VAULT_MOUNT_PATH
          + "\":\""
          + mountPath
          + "\",\""
          + HashicorpVaultConfigParams.HC_VAULT_KEY_NAME
          + "\":\""
          + keyName
          + "\""
          + "}";

  UUID testUniverseID = UUID.randomUUID();
  UUID customerID = UUID.randomUUID();
  UUID testConfigID = UUID.randomUUID();
  KmsConfig config;

  boolean LOCAL_MOCK_RUN = false;

  @Before
  public void setUp() {
    LOCAL_MOCK_RUN = MOCK_RUN;
    config = new KmsConfig();
    config.setAuthConfig((ObjectNode) Json.parse(jsonString));
    config.setCustomerUUID(customerID);
    config.setConfigUUID(testConfigID);
  }

  @Test
  public void testGetVaultSecretEngine() {
    String key = "key1";
    byte[] data = "TestData".getBytes();
    ObjectNode cfg = config.getAuthConfig();

    try {
      VaultSecretEngineBase transitEngine;
      if (LOCAL_MOCK_RUN) {
        transitEngine = mock(VaultTransit.class);
        byte[] edata = "ENCRYPTED_TestData".getBytes();

        when(transitEngine.encryptString(key, data)).thenReturn(edata);
        when(transitEngine.decryptString(key, edata)).thenReturn(data);
      } else {
        transitEngine = HashicorpEARServiceUtil.getVaultSecretEngine(cfg);
      }

      byte[] eData = transitEngine.encryptString(key, data);
      byte[] dData = transitEngine.decryptString(key, eData);

      assertEquals(
          new String(data, StandardCharsets.UTF_8), new String(dData, StandardCharsets.UTF_8));
    } catch (Exception exception) {
      LOG.error("Exception occured :" + exception);
      assertTrue(false);
    }
  }

  @Test
  public void testUpdateAuthConfigObj() throws Exception {

    VaultAccessor vAccessor;
    VaultSecretEngineBase transitEngine;
    ObjectNode cfg = config.getAuthConfig().deepCopy();

    if (LOCAL_MOCK_RUN) {
      vAccessor = mock(VaultAccessor.class);
      when(vAccessor.getTokenExpiryFromVault()).thenReturn(Arrays.asList(0L, 120L));
      transitEngine = new VaultTransit(vAccessor, mountPath, KeyType.CMK);
    } else {
      transitEngine = HashicorpEARServiceUtil.getVaultSecretEngine(cfg);
    }

    HashicorpEARServiceUtil.updateAuthConfigObj(testConfigID, transitEngine, cfg);

    JsonNode ttl = cfg.get(HashicorpVaultConfigParams.HC_VAULT_TTL);
    JsonNode ttlExpiry = cfg.get(HashicorpVaultConfigParams.HC_VAULT_TTL_EXPIRY);
    assertNotNull(ttl);
    assertNotNull(ttlExpiry);
    assertEquals(0L, (long) Long.valueOf(ttl.asText()));
  }

  @Test
  public void testUpdateAuthConfigObjFail() throws Exception {

    VaultAccessor vAccessor;
    VaultSecretEngineBase transitEngine;
    ObjectNode cfg = config.getAuthConfig().deepCopy();
    if (!LOCAL_MOCK_RUN) return;

    vAccessor = mock(VaultAccessor.class);
    when(vAccessor.getTokenExpiryFromVault()).thenThrow(NullPointerException.class);
    transitEngine = new VaultTransit(vAccessor, mountPath, KeyType.CMK);
    HashicorpEARServiceUtil.updateAuthConfigObj(testConfigID, transitEngine, cfg);
    JsonNode ttl = cfg.get(HashicorpVaultConfigParams.HC_VAULT_TTL);
    JsonNode ttlExpiry = cfg.get(HashicorpVaultConfigParams.HC_VAULT_TTL_EXPIRY);
    assertNull(ttl);
    assertNull(ttlExpiry);
  }
}
