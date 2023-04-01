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

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;

import com.azure.security.keyvault.keys.KeyClient;
import com.azure.security.keyvault.keys.cryptography.CryptographyClient;
import com.azure.security.keyvault.keys.models.KeyVaultKey;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.kms.util.AzuEARServiceUtil;
import com.yugabyte.yw.forms.EncryptionAtRestConfig;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.Spy;
import org.mockito.junit.MockitoJUnitRunner;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@RunWith(MockitoJUnitRunner.class)
public class AzuEARServiceTest extends FakeDBApplication {
  public static final Logger LOG = LoggerFactory.getLogger(GcpEARService.class);

  // Create fake auth config details
  public ObjectMapper mapper = new ObjectMapper();
  public ObjectNode fakeAuthConfig = mapper.createObjectNode();
  public UUID configUUID = UUID.randomUUID();
  public String authConfigName = "fake-azu-kms-config";
  public String authConfigClientId = "fake-client-id";
  public String authConfigClientSecret = "fake-client-secret";
  public String authConfigTenantId = "fake-tenant-id";
  // Create fake key vault and key details
  public String authConfigAzuVaultUrl = "fake-vault-url";
  public String authConfigAzuKeyName = "fake-key-name";
  public String authConfigAzuKeyAlgorithm = "RSA";
  public int authConfigAzuKeySize = 2048;
  public int numBytes = 32;
  public byte[] fakeUniverseKey;
  public byte[] fakeWrappedUniverseKey;
  public Universe universe;
  public Customer customer;

  @Spy public AzuEARServiceUtil mockAzuEARServiceUtil;
  @Spy public AzuEARService mockAzuEARService = new AzuEARService(null);
  @Mock public KeyClient mockKeyClient;
  @Mock public CryptographyClient mockCryptographyClient;
  @Mock public KeyVaultKey mockKeyVaultKey;

  @Before
  public void setUp() {
    this.customer = ModelFactory.testCustomer();
    this.universe = ModelFactory.createUniverse(customer.getId());
    this.fakeUniverseKey = mockAzuEARServiceUtil.generateRandomBytes(numBytes);
    this.fakeWrappedUniverseKey = mockAzuEARServiceUtil.generateRandomBytes(numBytes);

    // Populate the fake auth config
    fakeAuthConfig.put("name", authConfigName);
    fakeAuthConfig.put("CLIENT_ID", authConfigClientId);
    fakeAuthConfig.put("CLIENT_SECRET", authConfigClientSecret);
    fakeAuthConfig.put("TENANT_ID", authConfigTenantId);
    fakeAuthConfig.put("AZU_VAULT_URL", authConfigAzuVaultUrl);
    fakeAuthConfig.put("AZU_KEY_NAME", authConfigAzuKeyName);
    fakeAuthConfig.put("AZU_KEY_ALGORITHM", authConfigAzuKeyAlgorithm);
    fakeAuthConfig.put("AZU_KEY_SIZE", authConfigAzuKeySize);

    // Spy the util class
    // Mock all methods that are called on the AZU KMS client, return fake data
    doReturn(mockKeyVaultKey).when(mockKeyClient).getKey(authConfigAzuKeyName);
    doReturn(mockKeyClient).when(mockAzuEARServiceUtil).getKeyClient(fakeAuthConfig);
    doReturn(true)
        .when(mockAzuEARServiceUtil)
        .validateKeySettings(fakeAuthConfig, authConfigAzuKeyName);
    doNothing().when(mockAzuEARServiceUtil).testWrapAndUnwrapKey(fakeAuthConfig);
    doNothing().when(mockAzuEARServiceUtil).createKey(fakeAuthConfig);
    doReturn(fakeAuthConfig).when(mockAzuEARServiceUtil).getAuthConfig(configUUID);
    doReturn(fakeWrappedUniverseKey).when(mockAzuEARServiceUtil).wrapKey(any(), any());
    doReturn(fakeUniverseKey).when(mockAzuEARServiceUtil).unwrapKey(any(), any());
    doReturn(mockAzuEARServiceUtil).when(mockAzuEARService).getAzuEarServiceUtil();
  }

  @Test
  public void testCreateAuthConfigWithService() {
    ObjectNode fakeAuthConfigCopy = fakeAuthConfig.deepCopy();
    ObjectNode createdAuthConfig =
        mockAzuEARService.createAuthConfigWithService(configUUID, fakeAuthConfigCopy);
    assertEquals(createdAuthConfig, fakeAuthConfig);
    verify(mockAzuEARServiceUtil, times(1))
        .validateKeySettings(fakeAuthConfig, authConfigAzuKeyName);
    verify(mockAzuEARServiceUtil, times(1)).testWrapAndUnwrapKey(fakeAuthConfig);
  }

  @Test
  public void testCreateAuthConfigWithServiceKeyInvalidSettings() {
    doReturn(false)
        .when(mockAzuEARServiceUtil)
        .validateKeySettings(fakeAuthConfig, authConfigAzuKeyName);

    ObjectNode fakeAuthConfigCopy = fakeAuthConfig.deepCopy();
    ObjectNode createdAuthConfig =
        mockAzuEARService.createAuthConfigWithService(configUUID, fakeAuthConfigCopy);
    assertEquals(createdAuthConfig, null);
  }

  @Test
  public void testCreateAuthConfigWithServiceKeyNotExists() {
    doReturn(false)
        .when(mockAzuEARServiceUtil)
        .checkKeyExists(fakeAuthConfig, authConfigAzuKeyName);

    ObjectNode fakeAuthConfigCopy = fakeAuthConfig.deepCopy();
    ObjectNode createdAuthConfig =
        mockAzuEARService.createAuthConfigWithService(configUUID, fakeAuthConfigCopy);
    assertEquals(createdAuthConfig, fakeAuthConfig);
    verify(mockAzuEARServiceUtil, times(1)).testWrapAndUnwrapKey(fakeAuthConfig);
    verify(mockAzuEARServiceUtil, times(1)).createKey(fakeAuthConfig);
  }

  @Test
  public void testCreateKeyWithService() {
    // Creating the key vault key after a key vault has been validated
    // Using the key vault key, it creates and encrpyts the generated random universe key
    EncryptionAtRestConfig encryptionAtRestConfig = new EncryptionAtRestConfig();
    byte[] keyRef =
        mockAzuEARService.createKeyWithService(
            universe.getUniverseUUID(), configUUID, encryptionAtRestConfig);
    assertArrayEquals(keyRef, fakeWrappedUniverseKey);
    // Invoked once in createKeyWithService() and twice in setUp() for testing
    verify(mockAzuEARServiceUtil, times(3)).generateRandomBytes(numBytes);
    verify(mockAzuEARServiceUtil, times(1)).wrapKey(any(), any());
  }

  @Test
  public void testRotateKeyWithService() {
    // Generating a new universe key and using the existing key vault key to encrypt and store
    EncryptionAtRestConfig encryptionAtRestConfig = new EncryptionAtRestConfig();
    byte[] keyRef =
        mockAzuEARService.createKeyWithService(
            universe.getUniverseUUID(), configUUID, encryptionAtRestConfig);
    assertArrayEquals(keyRef, fakeWrappedUniverseKey);
    // Invoked once in createKeyWithService() and twice in setUp() for testing
    verify(mockAzuEARServiceUtil, times(3)).generateRandomBytes(numBytes);
    verify(mockAzuEARServiceUtil, times(1)).wrapKey(any(), any());
  }

  @Test
  public void testRetrieveKeyWithService() {
    // Decrypting the stored encrypted universe key known as keyRef
    EncryptionAtRestConfig encryptionAtRestConfig = new EncryptionAtRestConfig();
    byte[] keyRef = mockAzuEARService.retrieveKeyWithService(configUUID, fakeWrappedUniverseKey);
    assertArrayEquals(keyRef, fakeUniverseKey);
    verify(mockAzuEARServiceUtil, times(1)).unwrapKey(any(), any());
  }

  @Test
  public void testValidateRetrieveKeyWithService() {
    // Decrypting the stored encrypted universe key known as keyRef using a new auth config
    // Used for KMS edit operation
    EncryptionAtRestConfig encryptionAtRestConfig = new EncryptionAtRestConfig();
    byte[] keyRef = mockAzuEARService.retrieveKeyWithService(configUUID, fakeWrappedUniverseKey);
    assertArrayEquals(keyRef, fakeUniverseKey);
    verify(mockAzuEARServiceUtil, times(1)).unwrapKey(any(), any());
  }
}
