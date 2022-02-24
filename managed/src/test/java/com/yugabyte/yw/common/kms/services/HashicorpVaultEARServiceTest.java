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

package com.yugabyte.yw.common.kms.services;

import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertEquals;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.forms.UniverseTaskParams.EncryptionAtRestConfig;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.kms.util.HashicorpEARServiceUtil;
import com.yugabyte.yw.common.kms.util.hashicorpvault.VaultEARServiceUtilTest;
import com.yugabyte.yw.common.kms.util.hashicorpvault.HashicorpVaultConfigParams;

import java.nio.charset.StandardCharsets;
import java.util.UUID;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import play.libs.Json;

import com.fasterxml.jackson.databind.node.ObjectNode;
import java.util.Base64;

import static org.mockito.Mockito.mockStatic;
import org.mockito.MockedStatic;
import org.mockito.junit.MockitoJUnitRunner;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@RunWith(MockitoJUnitRunner.class)
public class HashicorpVaultEARServiceTest extends FakeDBApplication {

  public static final Logger LOG = LoggerFactory.getLogger(HashicorpVaultEARServiceTest.class);

  private class TestEncryptionAtRestService extends HashicorpEARService {
    // private static final Logger LOG = LoggerFactory.getLogger(TestEncryptionAtRestService.class);

    @Override
    public ObjectNode getAuthConfig(UUID configUUID) {
      ObjectNode n = Json.newObject();
      n.put(HashicorpVaultConfigParams.HC_VAULT_ADDRESS, VaultEARServiceUtilTest.vaultAddr);
      n.put(HashicorpVaultConfigParams.HC_VAULT_TOKEN, VaultEARServiceUtilTest.vaultToken);
      n.put(HashicorpVaultConfigParams.HC_VAULT_ENGINE, VaultEARServiceUtilTest.sEngine);
      n.put(HashicorpVaultConfigParams.HC_VAULT_MOUNT_PATH, VaultEARServiceUtilTest.mountPath);
      return n;
    }
  }

  public boolean MOCK_RUN;

  UUID testUniUUID = UUID.randomUUID();
  UUID testConfigUUID = UUID.randomUUID();
  TestEncryptionAtRestService encryptionService;
  EncryptionAtRestConfig config;

  MockedStatic<HashicorpEARServiceUtil> mockUtil;

  String mockEnrData;

  @Before
  public void setUp() {
    MOCK_RUN = VaultEARServiceUtilTest.MOCK_RUN;

    encryptionService = new TestEncryptionAtRestService();

    config = new EncryptionAtRestConfig();
    config.kmsConfigUUID = testConfigUUID;
    mockEnrData = "vault:v1:DCSAyVIIPF1t49p+OaYsX3R9PmJAtcxf6bVS3bE8Tg0kcbHypligQGAb0eLMU1";
    if (MOCK_RUN) mockUtil = mockStatic(HashicorpEARServiceUtil.class);
  }

  @After
  public void tearDown() {
    if (MOCK_RUN) mockUtil.close();
  }

  @Test
  public void testGetServiceSingleton() {
    EncryptionAtRestService newService =
        new EncryptionAtRestManager().getServiceInstance("HASHICORP");
    assertEquals(KeyProvider.HASHICORP.getServiceInstance().hashCode(), newService.hashCode());
  }

  @Test
  public void testCreateAuthConfigWithService() {

    if (MOCK_RUN) return;

    ObjectNode input = encryptionService.getAuthConfig(testConfigUUID);
    assertNotNull(input);
    ObjectNode result = encryptionService.createAuthConfigWithService(testConfigUUID, input);
    assertNotNull(result);
  }

  @Test
  public void testCreateAuthConfigWithServiceTTL() {

    if (MOCK_RUN) return;

    ObjectNode result =
        encryptionService.createAuthConfigWithService(
            testConfigUUID, encryptionService.getAuthConfig(testConfigUUID));
    assertNotNull(result);
    assertNotNull(result.get(HashicorpVaultConfigParams.HC_VAULT_TTL));
    assertNotNull(result.get(HashicorpVaultConfigParams.HC_VAULT_TTL_EXPIRY));

    LOG.info(
        "TTL is {} and Expiry is {}",
        result.get(HashicorpVaultConfigParams.HC_VAULT_TTL),
        result.get(HashicorpVaultConfigParams.HC_VAULT_TTL_EXPIRY));
  }

  @Test
  public void testCreateKeyUsingBase() throws Exception {

    String key;
    int encryptedKeySize;

    if (MOCK_RUN) {

      encryptedKeySize = mockEnrData.length();

      // mock getVaultKeyForUniverse
      mockUtil
          .when(() -> HashicorpEARServiceUtil.getVaultKeyForUniverse(testUniUUID, testConfigUUID))
          .thenCallRealMethod();

      key = HashicorpEARServiceUtil.getVaultKeyForUniverse(testUniUUID, testConfigUUID);

      // mock createVaultKEK
      mockUtil
          .when(
              () ->
                  HashicorpEARServiceUtil.createVaultKEK(
                      testUniUUID, testConfigUUID, encryptionService.getAuthConfig(testConfigUUID)))
          .thenReturn(key);

      // mock generateUniverseKey
      mockUtil
          .when(
              () ->
                  HashicorpEARServiceUtil.generateUniverseKey(
                      testUniUUID,
                      testConfigUUID,
                      "AES",
                      256,
                      encryptionService.getAuthConfig(testConfigUUID)))
          .thenReturn(mockEnrData.getBytes(StandardCharsets.UTF_8));
    } else {
      key = HashicorpEARServiceUtil.getVaultKeyForUniverse(testUniUUID, testConfigUUID);
      encryptedKeySize = 89;
    }

    byte[] encryptionKey = encryptionService.createKey(testUniUUID, testConfigUUID, config);

    String data = new String(encryptionKey, StandardCharsets.UTF_8);
    LOG.debug("Data received :: {}", data);
    assertNotNull(encryptionKey);
    assertEquals(encryptedKeySize, encryptionKey.length);

    encryptionService.cleanup(testUniUUID, testConfigUUID);
  }

  @Test
  public void testCreateKeyUsingHCVaultEARService() {

    String key;
    int encryptedKeySize;

    if (MOCK_RUN) {
      encryptedKeySize = mockEnrData.length();

      // mock getVaultKeyForUniverse
      mockUtil
          .when(() -> HashicorpEARServiceUtil.getVaultKeyForUniverse(testUniUUID, testConfigUUID))
          .thenCallRealMethod();

      key = HashicorpEARServiceUtil.getVaultKeyForUniverse(testUniUUID, testConfigUUID);

      // mock createVaultKEK
      mockUtil
          .when(
              () ->
                  HashicorpEARServiceUtil.createVaultKEK(
                      testUniUUID, testConfigUUID, encryptionService.getAuthConfig(testConfigUUID)))
          .thenReturn(key);

      // mock generateUniverseKey
      mockUtil
          .when(
              () ->
                  HashicorpEARServiceUtil.generateUniverseKey(
                      testUniUUID,
                      testConfigUUID,
                      "AES",
                      256,
                      encryptionService.getAuthConfig(testConfigUUID)))
          .thenReturn(mockEnrData.getBytes(StandardCharsets.UTF_8));
    } else {
      key = HashicorpEARServiceUtil.getVaultKeyForUniverse(testUniUUID, testConfigUUID);
      encryptedKeySize = 89;
    }

    final byte[] encryptionKey =
        encryptionService.createKeyWithService(testUniUUID, testConfigUUID, config);
    String data = new String(encryptionKey, StandardCharsets.UTF_8);
    LOG.debug("Data received :: {}", data);
    assertNotNull(encryptionKey);
    assertEquals(encryptedKeySize, encryptionKey.length);

    encryptionService.cleanupWithService(testUniUUID, testConfigUUID);
  }

  @Test
  public void testRetrieveKeyUsingHCVaultEARService() {

    byte[] keyRef = null;
    boolean hardcodedKey = false;
    String key = HashicorpEARServiceUtil.getVaultKeyForUniverse(testUniUUID, testConfigUUID);
    int encryptedKeySize = 0;

    if (MOCK_RUN) {
      encryptedKeySize = mockEnrData.length();
      // mock createVaultKEK
      mockUtil
          .when(
              () ->
                  HashicorpEARServiceUtil.createVaultKEK(
                      testUniUUID, testConfigUUID, encryptionService.getAuthConfig(testConfigUUID)))
          .thenReturn(key);

      // mock generateUniverseKey
      mockUtil
          .when(
              () ->
                  HashicorpEARServiceUtil.generateUniverseKey(
                      testUniUUID,
                      testConfigUUID,
                      "AES",
                      256,
                      encryptionService.getAuthConfig(testConfigUUID)))
          .thenReturn(mockEnrData.getBytes(StandardCharsets.UTF_8));
    } else {
      encryptedKeySize = 89;
    }

    if (hardcodedKey) {
      String keyVal =
          "vault:v5:1Qs8S6MyrbyeVm0RMthKGTFMLpvud8rLSJqAc8IH/"
              + "olOiXcyBIvd0ZsMDn2HdcdSO4RQDrVgEaMXr4Yd";
      keyRef = keyVal.getBytes(StandardCharsets.UTF_8);
      encryptedKeySize = keyVal.length();
    } else {
      // create new key
      keyRef = encryptionService.createKeyWithService(testUniUUID, testConfigUUID, config);
      String data = new String(keyRef, StandardCharsets.UTF_8);
      LOG.debug("Data received :: {}", data);
      assertEquals(encryptedKeySize, keyRef.length);
    }

    if (MOCK_RUN) {

      String mockUniverseKey = "Z00gwQeyBZROEKjX8T3QEEi43scsl/o2FFNRsWKUFJo=";
      final byte[] keyRef1 = keyRef;
      // mock decryptUniverseKey
      mockUtil
          .when(
              () ->
                  HashicorpEARServiceUtil.decryptUniverseKey(
                      testUniUUID,
                      testConfigUUID,
                      keyRef1,
                      encryptionService.getAuthConfig(testConfigUUID)))
          .thenReturn(Base64.getDecoder().decode(mockUniverseKey));
    }

    final byte[] encryptionKey =
        encryptionService.retrieveKeyWithService(testUniUUID, testConfigUUID, keyRef, config);
    // String data = new String(encryptionKey, StandardCharsets.UTF_8);
    LOG.debug("Data received :: {}", new String(Base64.getEncoder().encode(encryptionKey)));
    assertNotNull(encryptionKey);
    assertEquals(32, encryptionKey.length);

    if (hardcodedKey) {
      String b64key = "o47XMjKpRyBjY5TLI3JASYzIEc0D/SixghSVCZlc9LA=";
      assertEquals(b64key, new String(Base64.getEncoder().encode(encryptionKey)));
    }

    encryptionService.cleanupWithService(testUniUUID, testConfigUUID);
  }
}
