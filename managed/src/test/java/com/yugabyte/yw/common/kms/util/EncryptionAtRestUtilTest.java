package com.yugabyte.yw.common.kms.util;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.kms.util.hashicorpvault.HashicorpVaultConfigParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.Universe;
import java.util.Base64;
import java.util.UUID;
import org.junit.Before;
import org.junit.Ignore;
import org.junit.Test;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

public class EncryptionAtRestUtilTest extends FakeDBApplication {
  protected static final Logger LOG = LoggerFactory.getLogger(EncryptionAtRestUtilTest.class);

  Customer testCustomer;
  Universe testUniverse;

  EncryptionAtRestUtil encryptionUtil;
  private KmsConfig testKMSConfig;

  @Before
  public void setup() {
    encryptionUtil = new EncryptionAtRestUtil();
    testCustomer = ModelFactory.testCustomer();
    testUniverse = ModelFactory.createUniverse();
    testKMSConfig =
        KmsConfig.createKMSConfig(
            testCustomer.getUuid(),
            KeyProvider.AWS,
            Json.newObject().put("test_key", "test_val"),
            "some config name");
  }

  @Test
  public void testGenerateSalt() {
    String salt = encryptionUtil.generateSalt(testCustomer.getUuid(), KeyProvider.SMARTKEY);
    assertNotNull(salt);
  }

  @Test
  public void testMaskAndUnmaskConfigData() {
    JsonNode originalObj = Json.newObject().put("test_key", "test_val");
    ObjectNode encryptedObj =
        encryptionUtil.maskConfigData(testCustomer.getUuid(), originalObj, KeyProvider.SMARTKEY);
    JsonNode unencryptedObj =
        encryptionUtil.unmaskConfigData(testCustomer.getUuid(), encryptedObj, KeyProvider.SMARTKEY);
    assertEquals(originalObj.get("test_key").asText(), unencryptedObj.get("test_key").asText());
  }

  @Test
  @Ignore
  public void testUnmaskConfigDataHCVault() {

    String data = "";
    String outData = "";
    try {
      ObjectMapper om = new ObjectMapper();

      UUID uid = UUID.fromString("f33e3c9b-75ab-4c30-80ad-cba85646ea39");
      data = "";
      ObjectNode originalObj = Json.newObject().put("encrypted", data);
      JsonNode unencryptedObj = encryptionUtil.unmaskConfigData(uid, originalObj, KeyProvider.AWS);
      outData = om.writeValueAsString(unencryptedObj);

    } catch (Exception e) {
      LOG.debug("test:: failed");
    }
  }

  @Test
  public void testMaskConfigDataHCVault() throws Exception {

    String outData = "";
    String jsonString =
        "{"
            + "\"HC_VAULT_ADDRESS\":\"http://127.0.0.1:8200\","
            + "\"HC_VAULT_TOKEN\":\"s.fj4WtEMQ1MV1fnxQkpXA9ytV\","
            + "\"HC_VAULT_ENGINE\":\"transit\","
            + "\"HC_VAULT_MOUNT_PATH\":\"transit/\""
            + "}";

    try {
      ObjectMapper om = new ObjectMapper();

      UUID uid = UUID.fromString("f33e3c9b-75ab-4c30-80ad-cba85646ea39");

      JsonNode originalObj = Json.parse(jsonString);
      assertEquals("transit", originalObj.get(HashicorpVaultConfigParams.HC_VAULT_ENGINE).asText());

      ObjectNode encryptedObj =
          EncryptionAtRestUtil.maskConfigData(uid, originalObj, KeyProvider.HASHICORP);
      assertNotNull(encryptedObj);
      // Following fails and dumps the simulated column data for KMS_CONFIG
      // assertEquals(jsonString, om.writeValueAsString(encryptedObj));

      JsonNode unencryptedObj =
          EncryptionAtRestUtil.unmaskConfigData(uid, encryptedObj, KeyProvider.HASHICORP);
      assertNotNull(unencryptedObj);
      outData = om.writeValueAsString(unencryptedObj);
      assertEquals(jsonString, outData);

      assertEquals(
          originalObj.get(HashicorpVaultConfigParams.HC_VAULT_ADDRESS),
          unencryptedObj.get(HashicorpVaultConfigParams.HC_VAULT_ADDRESS));
      assertEquals(
          originalObj.get(HashicorpVaultConfigParams.HC_VAULT_TOKEN),
          unencryptedObj.get(HashicorpVaultConfigParams.HC_VAULT_TOKEN));
    } catch (Exception e) {
      LOG.error("test:: failed", e);
      throw e;
    }
  }

  @Test
  public void testGetUniverseKeyCacheEntryNoEntry() {
    assertNull(
        encryptionUtil.getUniverseKeyCacheEntry(
            UUID.randomUUID(), new String("some_key_ref").getBytes()));
  }

  @Test
  public void testSetAndGetUniverseKeyCacheEntry() {
    UUID universeUUID = UUID.randomUUID();
    byte[] keyRef = new String("some_key_ref").getBytes();
    byte[] keyVal = new String("some_key_val").getBytes();
    encryptionUtil.setUniverseKeyCacheEntry(universeUUID, keyRef, keyVal);
    assertEquals(
        Base64.getEncoder().encodeToString(keyVal),
        Base64.getEncoder()
            .encodeToString(encryptionUtil.getUniverseKeyCacheEntry(universeUUID, keyRef)));
  }

  @Test
  public void testSetUpdateAndGetUniverseKeyCacheEntry() {
    UUID universeUUID = UUID.randomUUID();
    byte[] keyRef = new String("some_key_ref").getBytes();
    byte[] keyVal = new String("some_key_val").getBytes();
    encryptionUtil.setUniverseKeyCacheEntry(universeUUID, keyRef, keyVal);
    assertEquals(
        Base64.getEncoder().encodeToString(keyVal),
        Base64.getEncoder()
            .encodeToString(encryptionUtil.getUniverseKeyCacheEntry(universeUUID, keyRef)));
    keyVal = new String("some_new_key_val").getBytes();
    encryptionUtil.setUniverseKeyCacheEntry(universeUUID, keyRef, keyVal);
    assertEquals(
        Base64.getEncoder().encodeToString(keyVal),
        Base64.getEncoder()
            .encodeToString(encryptionUtil.getUniverseKeyCacheEntry(universeUUID, keyRef)));
  }

  @Test
  public void testClearUniverseKeyCacheEntry() {
    UUID universeUUID = UUID.randomUUID();
    byte[] keyRef = new String("some_key_ref").getBytes();
    byte[] keyVal = new String("some_key_val").getBytes();
    encryptionUtil.setUniverseKeyCacheEntry(universeUUID, keyRef, keyVal);
    assertEquals(
        Base64.getEncoder().encodeToString(keyVal),
        Base64.getEncoder()
            .encodeToString(encryptionUtil.getUniverseKeyCacheEntry(universeUUID, keyRef)));
    encryptionUtil.removeUniverseKeyCacheEntry(universeUUID);
    assertNull(encryptionUtil.getUniverseKeyCacheEntry(universeUUID, keyRef));
  }

  @Test
  public void testGetNumUniverseKeysNoHistory() {
    int numRotations = encryptionUtil.getNumUniverseKeys(testUniverse.getUniverseUUID());
    assertEquals(numRotations, 0);
  }

  @Test
  public void testGetNumUniverseKeys() {
    encryptionUtil.addKeyRef(
        testUniverse.getUniverseUUID(), testKMSConfig.getConfigUUID(), "some_key_ref".getBytes());
    int numRotations = encryptionUtil.getNumUniverseKeys(testUniverse.getUniverseUUID());
    assertEquals(1, numRotations);
  }

  @Test
  public void testClearUniverseKeyHistory() {
    encryptionUtil.addKeyRef(
        testUniverse.getUniverseUUID(), testKMSConfig.getConfigUUID(), "some_key_ref".getBytes());
    int numRotations = encryptionUtil.getNumUniverseKeys(testUniverse.getUniverseUUID());
    assertEquals(numRotations, 1);
    encryptionUtil.removeKeyRotationHistory(
        testUniverse.getUniverseUUID(), testKMSConfig.getConfigUUID());
    numRotations = encryptionUtil.getNumUniverseKeys(testUniverse.getUniverseUUID());
    assertEquals(0, numRotations);
  }
}
