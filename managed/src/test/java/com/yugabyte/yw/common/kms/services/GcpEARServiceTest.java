/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/
 *  POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.kms.services;

import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.kms.util.GcpEARServiceUtil;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.forms.EncryptionAtRestConfig;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.Universe;
import java.io.IOException;
import java.util.UUID;
import org.apache.commons.lang3.RandomUtils;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Spy;
import org.mockito.junit.MockitoJUnitRunner;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@RunWith(MockitoJUnitRunner.class)
public class GcpEARServiceTest extends FakeDBApplication {
  public static final Logger LOG = LoggerFactory.getLogger(GcpEARService.class);

  // Create fake auth config details
  public ObjectMapper mapper = new ObjectMapper();
  public ObjectNode fakeAuthConfig = mapper.createObjectNode();
  public UUID configUUID = UUID.randomUUID();
  public String authConfigName = "fake-gcp-kms-config";
  public String authConfigLocationId = "global";
  public String authConfigProtectionLevel = "HSM";
  public String authConfigProjectId = "yugabyte";
  // Create fake key ring and crypto key resource names
  public String fakeKeyRingRN = "projects/yugabyte/locations/global/keyRings/yb-kr";
  public String fakeKeyRingId = "yb-kr";
  public String fakeCryptoKeyRN =
      "projects/yugabyte/locations/global/keyRings/yb-kr/cryptoKeys/yb-ck";
  public String fakeCryptoKeyId = "yb-ck";
  // randomBytes is a fake encryption key
  public int numBytes = 32;
  public byte[] randomBytes = RandomUtils.nextBytes(numBytes);
  public Universe universe;
  public Customer customer;

  @Spy public GcpEARServiceUtil mockGcpEARServiceUtil;
  @Spy public GcpEARService mockGcpEARService = new GcpEARService(null);

  @Before
  public void setUp() throws Exception {
    this.customer = ModelFactory.testCustomer();
    this.universe = ModelFactory.createUniverse(customer.getId());

    // Populate the fake auth config
    fakeAuthConfig.put("name", authConfigName);
    fakeAuthConfig.put("LOCATION_ID", authConfigLocationId);
    fakeAuthConfig.put("PROTECTION_LEVEL", authConfigProtectionLevel);
    fakeAuthConfig.put("GCP_KMS_ENDPOINT", "fake-kms-endpoint");
    fakeAuthConfig.put("KEY_RING_ID", fakeKeyRingId);
    fakeAuthConfig.put("CRYPTO_KEY_ID", fakeCryptoKeyId);

    ObjectNode fakeGcpConfig = fakeAuthConfig.putObject("GCP_CONFIG");
    fakeGcpConfig.put("type", "service_account");
    fakeGcpConfig.put("project_id", authConfigProjectId);

    // Spy the util class and the service class
    // Mock all methods that are called in the service class, return fake data
    // doReturn(fakeKeyRingRN).when(mockKeyRing).getName();
    doReturn(fakeAuthConfig).when(mockGcpEARServiceUtil).getAuthConfig(configUUID);
    doReturn(fakeKeyRingRN).when(mockGcpEARServiceUtil).getKeyRingRN(fakeAuthConfig);
    doReturn(fakeCryptoKeyRN).when(mockGcpEARServiceUtil).getCryptoKeyRN(fakeAuthConfig);
    doReturn(true).when(mockGcpEARServiceUtil).checkKeyRingExists(fakeAuthConfig);
    doReturn(true).when(mockGcpEARServiceUtil).checkCryptoKeyExists(fakeAuthConfig);
    doReturn(true).when(mockGcpEARServiceUtil).validateCryptoKeySettings(fakeAuthConfig);
    doNothing().when(mockGcpEARServiceUtil).checkOrCreateKeyRing(fakeAuthConfig);
    doNothing().when(mockGcpEARServiceUtil).checkOrCreateCryptoKey(fakeAuthConfig);
    doReturn(randomBytes).when(mockGcpEARServiceUtil).generateRandomBytes(fakeAuthConfig, numBytes);
    doReturn(randomBytes).when(mockGcpEARServiceUtil).encryptBytes(fakeAuthConfig, randomBytes);
    doReturn(randomBytes).when(mockGcpEARServiceUtil).decryptBytes(any(), eq(randomBytes));

    doReturn(mockGcpEARServiceUtil).when(mockGcpEARService).getGcpEarServiceUtil();
  }

  @Test
  public void testCreateAuthConfigWithService() throws Exception {
    // Check if create key ring and crypto key functions are called
    ObjectNode fakeAuthConfigCopy = fakeAuthConfig.deepCopy();

    // Make a fake auth config object in db to update proper protection level
    KmsConfig kmsConfig = new KmsConfig();
    kmsConfig.setConfigUUID(this.configUUID);
    kmsConfig.setCustomerUUID(this.customer.getUuid());
    kmsConfig.setKeyProvider(KeyProvider.GCP);
    kmsConfig.setAuthConfig(fakeAuthConfigCopy);
    kmsConfig.setVersion(KmsConfig.SCHEMA_VERSION);
    kmsConfig.setName(authConfigName);
    kmsConfig.save();

    ObjectNode createdAuthConfig =
        mockGcpEARService.createAuthConfigWithService(configUUID, fakeAuthConfigCopy);
    assertEquals(createdAuthConfig, fakeAuthConfig);
    verify(mockGcpEARServiceUtil, times(1)).checkOrCreateKeyRing(fakeAuthConfig);
    verify(mockGcpEARServiceUtil, times(1)).checkOrCreateCryptoKey(fakeAuthConfig);
  }

  @Test
  public void testCreateKeyWithService() throws IOException {
    // Creating the crypto key after a key ring has been created
    // Using the crypto key, it creates and encrpyts the generated random universe key
    EncryptionAtRestConfig encryptionAtRestConfig = new EncryptionAtRestConfig();
    byte[] keyRef =
        mockGcpEARService.createKeyWithService(
            universe.getUniverseUUID(), configUUID, encryptionAtRestConfig);
    assertEquals(keyRef, randomBytes);
    verify(mockGcpEARServiceUtil, times(1)).generateRandomBytes(fakeAuthConfig, numBytes);
    verify(mockGcpEARServiceUtil, times(1)).encryptBytes(fakeAuthConfig, randomBytes);
  }

  @Test
  public void testRotateKeyWithService() throws IOException {
    // Generating a new universe key and using the existing crypto key to encrypt and store
    EncryptionAtRestConfig encryptionAtRestConfig = new EncryptionAtRestConfig();
    byte[] keyRef =
        mockGcpEARService.rotateKeyWithService(
            universe.getUniverseUUID(), configUUID, encryptionAtRestConfig);
    assertEquals(keyRef, randomBytes);
    verify(mockGcpEARServiceUtil, times(1)).generateRandomBytes(fakeAuthConfig, numBytes);
    verify(mockGcpEARServiceUtil, times(1)).encryptBytes(fakeAuthConfig, randomBytes);
  }

  @Test
  public void testRetrieveKeyWithService() throws IOException {
    // Decrypting the stored encrypted universe key known as keyRef
    byte[] keyRef = mockGcpEARService.retrieveKeyWithService(configUUID, randomBytes);
    assertEquals(keyRef, randomBytes);
    verify(mockGcpEARServiceUtil, times(1)).decryptBytes(fakeAuthConfig, randomBytes);
  }

  @Test
  public void testValidateRetrieveKeyWithService() throws IOException {
    // Decrypting the stored encrypted universe key known as keyRef using a new auth config
    // Used for KMS  edit operation
    byte[] keyRef =
        mockGcpEARService.validateRetrieveKeyWithService(configUUID, randomBytes, fakeAuthConfig);
    assertEquals(keyRef, randomBytes);
    verify(mockGcpEARServiceUtil, times(1)).decryptBytes(fakeAuthConfig, randomBytes);
  }
}
