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

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.kms.util.HashicorpEARServiceUtil;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.models.KmsConfig;

import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import java.nio.charset.StandardCharsets;

import static org.junit.Assert.assertTrue;
import org.junit.Before;
import org.junit.Test;
import java.util.UUID;
import play.libs.Json;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

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

  public static final String jsonString =
      "{"
          + "\""
          + HashicorpEARServiceUtil.HC_VAULT_ADDRESS
          + "\":\""
          + vaultAddr
          + "\",\""
          + HashicorpEARServiceUtil.HC_VAULT_TOKEN
          + "\":\""
          + vaultToken
          + "\",\""
          + HashicorpEARServiceUtil.HC_VAULT_ENGINE
          + "\":\""
          + sEngine
          + "\",\""
          + HashicorpEARServiceUtil.HC_VAULT_MOUNT_PATH
          + "\":\""
          + mountPath
          + "\""
          + "}";

  UUID testUniverseID = UUID.randomUUID();
  // EncryptionAtRestConfig config;
  KmsConfig config;

  @Before
  public void setUp() {
    config = new KmsConfig();
    config.authConfig = Json.parse(jsonString);
  }

  @Test
  public void testGetVaultSecretEngine() {
    String key = "key1";
    byte[] data = "TestData".getBytes();

    try {
      VaultSecretEngineBase transitEngine;
      if (MOCK_RUN) {
        transitEngine = mock(VaultTransit.class);
        byte[] edata = "ENCRYPTED_TestData".getBytes();

        when(transitEngine.encryptString(key, data)).thenReturn(edata);
        when(transitEngine.decryptString(key, edata)).thenReturn(data);
      } else {
        transitEngine =
            HashicorpEARServiceUtil.getVaultSecretEngine((ObjectNode) config.authConfig);
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
}
