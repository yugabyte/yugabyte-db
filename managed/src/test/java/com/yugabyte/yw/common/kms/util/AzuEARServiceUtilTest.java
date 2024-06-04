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

package com.yugabyte.yw.common.kms.util;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;

import com.azure.security.keyvault.keys.KeyClient;
import com.azure.security.keyvault.keys.cryptography.CryptographyClient;
import com.azure.security.keyvault.keys.cryptography.models.KeyWrapAlgorithm;
import com.azure.security.keyvault.keys.cryptography.models.UnwrapResult;
import com.azure.security.keyvault.keys.cryptography.models.WrapResult;
import com.azure.security.keyvault.keys.models.KeyOperation;
import com.azure.security.keyvault.keys.models.KeyProperties;
import com.azure.security.keyvault.keys.models.KeyVaultKey;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.kms.util.AzuEARServiceUtil.AzuKmsAuthConfigField;
import java.util.Arrays;
import java.util.List;
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
public class AzuEARServiceUtilTest extends FakeDBApplication {
  public static final Logger LOG = LoggerFactory.getLogger(AzuEARServiceUtil.class);

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
  public String authConfigAzuKeyAlgorithm = "fake-key-algorithm";
  public int authConfigAzuKeySize = 2048;
  public int numBytes = 32;
  public byte[] fakeUniverseKey;
  public byte[] fakeWrappedUniverseKey;

  @Spy public AzuEARServiceUtil mockAzuEARServiceUtil;
  @Mock public KeyClient mockKeyClient;
  @Mock public CryptographyClient mockCryptographyClient;
  @Mock public KeyVaultKey mockKeyVaultKey;

  @Before
  public void setUp() {
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
    WrapResult fakeWrapResult =
        new WrapResult(fakeWrappedUniverseKey, KeyWrapAlgorithm.RSA_OAEP, authConfigAzuKeyName);
    doReturn(fakeWrapResult)
        .when(mockCryptographyClient)
        .wrapKey(KeyWrapAlgorithm.RSA_OAEP, fakeUniverseKey);
    UnwrapResult fakeUnwrapResult =
        new UnwrapResult(fakeUniverseKey, KeyWrapAlgorithm.RSA_OAEP, authConfigAzuKeyName);
    doReturn(fakeUnwrapResult)
        .when(mockCryptographyClient)
        .unwrapKey(KeyWrapAlgorithm.RSA_OAEP, fakeWrappedUniverseKey);
    doReturn(mockCryptographyClient)
        .when(mockAzuEARServiceUtil)
        .getCryptographyClient(eq(fakeAuthConfig), any());
    KeyProperties fakeKeyProperties = new KeyProperties();
    fakeKeyProperties.setEnabled(true).setExpiresOn(null).setNotBefore(null);
    doReturn(fakeKeyProperties).when(mockKeyVaultKey).getProperties();
    List<KeyOperation> fakeKeyOperations =
        Arrays.asList(KeyOperation.WRAP_KEY, KeyOperation.UNWRAP_KEY);
    doReturn(fakeKeyOperations).when(mockKeyVaultKey).getKeyOperations();
  }

  @Test
  public void testGetConfigClientId() {
    String clientId =
        mockAzuEARServiceUtil.getConfigFieldValue(
            fakeAuthConfig, AzuKmsAuthConfigField.CLIENT_ID.fieldName);
    assertEquals(clientId, authConfigClientId);
  }

  @Test
  public void testGetConfigClientSecret() {
    String clientSecret =
        mockAzuEARServiceUtil.getConfigFieldValue(
            fakeAuthConfig, AzuKmsAuthConfigField.CLIENT_SECRET.fieldName);
    assertEquals(clientSecret, authConfigClientSecret);
  }

  @Test
  public void testGetConfigTenantId() {
    String tenantId =
        mockAzuEARServiceUtil.getConfigFieldValue(
            fakeAuthConfig, AzuKmsAuthConfigField.TENANT_ID.fieldName);
    assertEquals(tenantId, authConfigTenantId);
  }

  @Test
  public void testGetConfigVaultUrl() {
    String vaultUrl =
        mockAzuEARServiceUtil.getConfigFieldValue(
            fakeAuthConfig, AzuKmsAuthConfigField.AZU_VAULT_URL.fieldName);
    assertEquals(vaultUrl, authConfigAzuVaultUrl);
  }

  @Test
  public void testGetConfigKeyName() {
    String keyName =
        mockAzuEARServiceUtil.getConfigFieldValue(
            fakeAuthConfig, AzuKmsAuthConfigField.AZU_KEY_NAME.fieldName);
    assertEquals(keyName, authConfigAzuKeyName);
  }

  @Test
  public void testGetConfigKeyAlgorithm() {
    String keyAlgorithm =
        mockAzuEARServiceUtil.getConfigFieldValue(
            fakeAuthConfig, AzuKmsAuthConfigField.AZU_KEY_ALGORITHM.fieldName);
    assertEquals(keyAlgorithm, authConfigAzuKeyAlgorithm);
  }

  @Test
  public void testGetConfigKeySize() {
    int keySize = mockAzuEARServiceUtil.getConfigKeySize(fakeAuthConfig);
    assertEquals(keySize, authConfigAzuKeySize);
  }

  @Test
  public void testCreateKey() {
    mockAzuEARServiceUtil.createKey(fakeAuthConfig);
    verify(mockKeyClient, times(1)).createRsaKey(any());
  }

  @Test
  public void testGetKey() {
    KeyVaultKey keyVaultKey = mockAzuEARServiceUtil.getKey(fakeAuthConfig);
    assertEquals(keyVaultKey, mockKeyVaultKey);
  }

  @Test
  public void testGenerateRandomBytes() {
    byte[] randomBytes = mockAzuEARServiceUtil.generateRandomBytes(numBytes);
    assertEquals(numBytes, randomBytes.length);
  }

  @Test
  public void testWrapKey() {
    byte[] wrappedKey = mockAzuEARServiceUtil.wrapKey(fakeAuthConfig, fakeUniverseKey);
    assertArrayEquals(wrappedKey, fakeWrappedUniverseKey);
  }

  @Test
  public void testUnwrapKey() {
    byte[] unwrappedKey = mockAzuEARServiceUtil.unwrapKey(fakeAuthConfig, fakeWrappedUniverseKey);
    assertArrayEquals(unwrappedKey, fakeUniverseKey);
  }

  @Test
  public void testCheckKeyVaultisValid() {
    boolean isKeyVaultValid = mockAzuEARServiceUtil.checkKeyVaultisValid(fakeAuthConfig);
    assertEquals(isKeyVaultValid, true);
  }

  @Test
  public void testCheckKeyExists() {
    boolean keyExists = mockAzuEARServiceUtil.checkKeyExists(fakeAuthConfig, authConfigAzuKeyName);
    assertEquals(keyExists, true);
  }

  @Test
  public void testCheckKeyVaultAndKeyExists() {
    mockAzuEARServiceUtil.checkKeyVaultAndKeyExists(fakeAuthConfig);
    verify(mockKeyClient, times(1)).getKey(any());
    verify(mockAzuEARServiceUtil, times(2)).getKeyClient(any());
  }

  @Test
  public void testValidateKeySettings() {
    boolean isKeyVaultKeyValid =
        mockAzuEARServiceUtil.validateKeySettings(fakeAuthConfig, authConfigAzuKeyName);
    assertEquals(isKeyVaultKeyValid, true);
  }

  @Test
  public void testCheckFieldsExist() {
    boolean checkFieldsExist = mockAzuEARServiceUtil.checkFieldsExist(fakeAuthConfig);
    assertEquals(checkFieldsExist, true);
  }

  @Test
  public void testValidateKMSProviderConfigFormData() throws Exception {
    mockAzuEARServiceUtil.validateKMSProviderConfigFormData(fakeAuthConfig);
    verify(mockAzuEARServiceUtil, times(1)).getKeyClient(fakeAuthConfig);
  }
}
