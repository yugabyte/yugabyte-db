/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/
 * POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.kms.util;

import java.security.NoSuchAlgorithmException;
import java.security.SecureRandom;
import java.time.OffsetDateTime;
import java.util.Arrays;
import java.util.List;
import java.util.Random;
import java.util.UUID;
import org.apache.commons.lang3.StringUtils;
import com.azure.core.credential.TokenCredential;
import com.azure.core.exception.ResourceNotFoundException;
import com.azure.identity.ClientSecretCredential;
import com.azure.identity.ClientSecretCredentialBuilder;
import com.azure.security.keyvault.keys.KeyClient;
import com.azure.security.keyvault.keys.KeyClientBuilder;
import com.azure.security.keyvault.keys.cryptography.CryptographyClient;
import com.azure.security.keyvault.keys.cryptography.CryptographyClientBuilder;
import com.azure.security.keyvault.keys.cryptography.models.KeyWrapAlgorithm;
import com.azure.security.keyvault.keys.cryptography.models.UnwrapResult;
import com.azure.security.keyvault.keys.cryptography.models.WrapResult;
import com.azure.security.keyvault.keys.models.CreateRsaKeyOptions;
import com.azure.security.keyvault.keys.models.KeyOperation;
import com.azure.security.keyvault.keys.models.KeyProperties;
import com.azure.security.keyvault.keys.models.KeyVaultKey;
import com.fasterxml.jackson.databind.node.ObjectNode;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class AzuEARServiceUtil {

  public static final String CLIENT_ID_FIELDNAME = "CLIENT_ID";
  public static final String CLIENT_SECRET_FIELDNAME = "CLIENT_SECRET";
  public static final String TENANT_ID_FIELDNAME = "TENANT_ID";
  public static final String AZU_VAULT_URL_FIELDNAME = "AZU_VAULT_URL";
  public static final String AZU_KEY_NAME_FIELDNAME = "AZU_KEY_NAME";
  public static final String AZU_KEY_ALGORITHM_FIELDNAME = "AZU_KEY_ALGORITHM";
  public static final String AZU_KEY_SIZE_FIELDNAME = "AZU_KEY_SIZE";

  public ObjectNode getAuthConfig(UUID configUUID) {
    return EncryptionAtRestUtil.getAuthConfig(configUUID);
  }

  /**
   * Creates the token credentials object used for authenticating with valid credentials
   *
   * @param authConfig the config object containing client id, secret, tenant id, vault url, key
   *     name, etc.
   * @return the token credentials object
   */
  public TokenCredential getCredentials(ObjectNode authConfig) {
    String clientId = getConfigFieldValue(authConfig, CLIENT_ID_FIELDNAME);
    String clientSecret = getConfigFieldValue(authConfig, CLIENT_SECRET_FIELDNAME);
    String tenantId = getConfigFieldValue(authConfig, TENANT_ID_FIELDNAME);

    if (clientId == null || clientSecret == null || tenantId == null) {
      String errMsg = "Cannot get credentials. clientId, clientSecret, or tenantId is null.";
      log.error(errMsg);
      throw new RuntimeException(errMsg);
    }

    ClientSecretCredential clientSecretCredential =
        new ClientSecretCredentialBuilder()
            .clientId(clientId)
            .clientSecret(clientSecret)
            .tenantId(tenantId)
            .build();

    return clientSecretCredential;
  }

  /**
   * Creates the AZU KMS key client with the creds given, including additional optional parameters.
   *
   * @param authConfig the config object containing client id, secret, tenant id, vault url, key
   *     name, etc.
   * @return an AZU KMS key client object
   */
  public KeyClient getKeyClient(ObjectNode authConfig) {
    return new KeyClientBuilder()
        .vaultUrl(getConfigFieldValue(authConfig, AZU_VAULT_URL_FIELDNAME))
        .credential(getCredentials(authConfig))
        .buildClient();
  }

  /**
   * Gets the key id of the key with the key name stored in the config object
   *
   * @param authConfig the config object containing client id, secret, tenant id, vault url, key
   *     name, etc.
   * @return the long form key identifier
   */
  public String getKeyId(ObjectNode authConfig) {
    KeyClient keyClient = getKeyClient(authConfig);
    String keyName = getConfigFieldValue(authConfig, AZU_KEY_NAME_FIELDNAME);
    String keyId = keyClient.getKey(keyName).getId();
    return keyId;
  }

  /**
   * Creates the cryptography client that is used to perform wrap and unwrap operations on the key
   *
   * @param authConfig the config object containing client id, secret, tenant id, vault url, key
   *     name, etc.
   * @return an AZU KMS crypto client object
   */
  public CryptographyClient getCryptographyClient(ObjectNode authConfig) {
    return new CryptographyClientBuilder()
        .credential(getCredentials(authConfig))
        .keyIdentifier(getKeyId(authConfig))
        .buildClient();
  }

  /**
   * Gets the required field value from the config object
   *
   * @param authConfig the config object containing client id, secret, tenant id, vault url, key
   *     name, etc.
   * @param fieldName the field name whose value is required from the config object.
   * @return the field value
   */
  public String getConfigFieldValue(ObjectNode authConfig, String fieldName) {
    String fieldValue = "";
    if (authConfig.has(fieldName)) {
      fieldValue = authConfig.path(fieldName).asText();
    } else {
      log.warn(
          String.format(
              "Could not get %s from AZU authConfig. %s not found.", fieldName, fieldName));
      return null;
    }
    return fieldValue;
  }

  /**
   * Gets the AZU KMS key size from the config object
   *
   * @param authConfig the config object containing client id, secret, tenant id, vault url, key
   *     name, etc.
   * @return the key size stored
   */
  public Integer getConfigKeySize(ObjectNode authConfig) {
    Integer keySize = null;
    if (authConfig.has(AZU_KEY_SIZE_FIELDNAME)) {
      keySize = authConfig.path(AZU_KEY_SIZE_FIELDNAME).asInt();
    } else {
      log.warn("Could not get AZU config key size. 'AZU_KEY_SIZE' not found.");
      return null;
    }
    return keySize;
  }

  /**
   * Creates a key in the key vault given by the config object with settings accordingly. Key must
   * not already exist.
   *
   * @param authConfig the config object containing client id, secret, tenant id, vault url, key
   *     name, etc.
   */
  public void createKey(ObjectNode authConfig) {
    KeyClient keyClient = getKeyClient(authConfig);
    String keyName = getConfigFieldValue(authConfig, AZU_KEY_NAME_FIELDNAME);
    CreateRsaKeyOptions createRsaKeyOptions = new CreateRsaKeyOptions(keyName);
    createRsaKeyOptions =
        createRsaKeyOptions
            .setEnabled(true)
            .setKeySize(getConfigKeySize(authConfig))
            .setExpiresOn(null)
            .setNotBefore(null);
    keyClient.createRsaKey(createRsaKeyOptions);
    return;
  }

  /**
   * Gets the key vault key object from the config object. Key must exist already
   *
   * @param authConfig the config object containing client id, secret, tenant id, vault url, key
   *     name, etc.
   * @return the key vault key object
   */
  public KeyVaultKey getKey(ObjectNode authConfig) {
    KeyClient keyClient = getKeyClient(authConfig);
    String keyName = getConfigFieldValue(authConfig, AZU_KEY_NAME_FIELDNAME);
    KeyVaultKey keyVaultKey = keyClient.getKey(keyName);
    return keyVaultKey;
  }

  /**
   * Generates a secure random sequence of bytes. This is used to create the universe key. This will
   * be encrypted by the master key and ciphertext is stored on local db.
   *
   * @param numBytes the number of bytes to be generated
   * @return the randomly generated byte array
   */
  public byte[] generateRandomBytes(int numBytes) {
    byte[] randomBytes = new byte[numBytes];
    try {
      SecureRandom.getInstanceStrong().nextBytes(randomBytes);
    } catch (NoSuchAlgorithmException e) {
      log.warn("Could not generate AZU random bytes, no such algorithm.");
      return null;
    }
    return randomBytes;
  }

  /**
   * Wraps / encrypts a byte array with the key vault key. The key name is stored in the config
   * object. Each universe with the same KMS config uses the same master key (key vault key), but
   * different universe keys that are generated explicitly.
   *
   * @param authConfig the config object containing client id, secret, tenant id, vault url, key
   *     name, etc.
   * @param keyBytes the plaintext universe key to be wrapped / encrypted
   * @return the wrapped universe key
   */
  public byte[] wrapKey(ObjectNode authConfig, byte[] keyBytes) {
    CryptographyClient cryptographyClient = getCryptographyClient(authConfig);
    WrapResult wrapResult = cryptographyClient.wrapKey(KeyWrapAlgorithm.RSA_OAEP, keyBytes);
    byte[] wrappedKeyBytes = wrapResult.getEncryptedKey();
    return wrappedKeyBytes;
  }

  /**
   * Unwraps / decrypts a byte array with the key vault key. The key name is stored in the config
   * object.
   *
   * @param authConfig the config object containing client id, secret, tenant id, vault url, key
   *     name, etc.
   * @param keyRef the wrapped universe key
   * @return the unwrapped universe key
   */
  public byte[] unwrapKey(ObjectNode authConfig, byte[] keyRef) {
    CryptographyClient cryptographyClient = getCryptographyClient(authConfig);
    UnwrapResult unwrapResult = cryptographyClient.unwrapKey(KeyWrapAlgorithm.RSA_OAEP, keyRef);
    byte[] keyBytes = unwrapResult.getKey();
    return keyBytes;
  }

  /**
   * Tests both the wrap and unwrap operations with fake data. Mostly used for testing the
   * permissions.
   *
   * @param authConfig the config object containing client id, secret, tenant id, vault url, key
   *     name, etc.
   * @throws RuntimeException if wrap and unwrap operations don't give same output as original
   */
  public void testWrapAndUnwrapKey(ObjectNode authConfig) throws RuntimeException {
    byte[] fakeKeyBytes = new byte[20];
    new Random().nextBytes(fakeKeyBytes);
    byte[] wrappedKey = wrapKey(authConfig, fakeKeyBytes);
    byte[] unwrappedKey = unwrapKey(authConfig, wrappedKey);
    if (!Arrays.equals(fakeKeyBytes, unwrappedKey)) {
      String errMsg = "Wrap and unwrap operations gave different key than the original fake key.";
      log.error(errMsg);
      throw new RuntimeException(errMsg);
    }
  }

  /**
   * Checks if the key vault is valid and verifies the vault url.
   *
   * @param authConfig the config object containing client id, secret, tenant id, vault url, key
   *     name, etc.
   * @return true if the key vault is valid, else false
   */
  public boolean checkKeyVaultisValid(ObjectNode authConfig) {
    try {
      KeyClient keyClient = getKeyClient(authConfig);
      if (keyClient != null) {
        return true;
      }
    } catch (Exception e) {
      log.error(
          "Key vault or credentials are invalid for config with key vault url = "
              + getConfigFieldValue(authConfig, AZU_VAULT_URL_FIELDNAME));
      return false;
    }
    return false;
  }

  /**
   * Checks if a key with given key name exists in the key vault from the config object
   *
   * @param authConfig the config object containing client id, secret, tenant id, vault url, key
   *     name, etc.
   * @param keyName the key name to check
   * @return true if the key exists, else false
   */
  public boolean checkKeyExists(ObjectNode authConfig, String keyName) {
    KeyClient keyClient = getKeyClient(authConfig);
    try {
      KeyVaultKey keyVaultKey = keyClient.getKey(keyName);
      if (keyVaultKey == null) {
        return false;
      }
    } catch (ResourceNotFoundException e) {
      String msg =
          String.format(
              "Key does not exist in the key vault with key name = '%s' and key vault url = '%s'",
              keyName, getConfigFieldValue(authConfig, AZU_VAULT_URL_FIELDNAME));
      log.error(msg);
      return false;
    }
    return true;
  }

  /**
   * Checks in combination if both the key vault and the key in the config object exist on the AZU
   * KMS
   *
   * @param authConfig the config object containing client id, secret, tenant id, vault url, key
   *     name, etc.
   * @throws RuntimeException if either the key vault or the key doesn't exist
   */
  public void checkKeyVaultAndKeyExists(ObjectNode authConfig) throws RuntimeException {
    // Ensure the key vault exists in AZU KMS
    if (!checkKeyVaultisValid(authConfig)) {
      String errMsg =
          String.format(
              "Key vault or the credentials are invalid for key vault url = '%s'",
              getConfigFieldValue(authConfig, AZU_VAULT_URL_FIELDNAME));
      log.error(errMsg);
      throw new RuntimeException(errMsg);
    }
    // Ensure the key with given name exists in the key vault
    String keyName = getConfigFieldValue(authConfig, AZU_KEY_NAME_FIELDNAME);
    if (!checkKeyExists(authConfig, keyName)) {
      String errMsg =
          String.format(
              "Key does not exist in the key vault with key name = '%s' and key vault url = '%s'",
              keyName, getConfigFieldValue(authConfig, AZU_VAULT_URL_FIELDNAME));
      log.error(errMsg);
      throw new RuntimeException(errMsg);
    }
  }

  /**
   * Checks all the settings of a given key if they are valid. Validates the following: 1. Key
   * exists 2. Key is enabled 3. Key doesn't have an expiry date 4. Key either doesn't have an
   * activation date or the activation date has passed 5. Current key version has wrap and unwrap
   * permitted operations
   *
   * @param authConfig the config object containing client id, secret, tenant id, vault url, key
   *     name, etc.
   * @param keyName the key name to check
   * @return true if all the settings are valid, else false
   */
  public boolean validateKeySettings(ObjectNode authConfig, String keyName) {
    KeyVaultKey keyVaultKey = getKey(authConfig);
    KeyProperties keyProperties = keyVaultKey.getProperties();
    if (!keyProperties.isEnabled()) {
      log.error("Azure key is not enabled. Please enable it. keyName = " + keyName);
      return false;
    }
    if (keyProperties.getExpiresOn() != null) {
      log.error(
          "Key has an expiration date: "
              + keyProperties.getExpiresOn().toString()
              + ". Please disable expiration. keyName = "
              + keyName);
      return false;
    }
    // getNotBefore is the key activation date
    if (keyProperties.getNotBefore() != null
        && keyProperties.getNotBefore().isAfter(OffsetDateTime.now())) {
      log.error(
          "Key has an invalid activation date: "
              + keyProperties.getNotBefore().toString()
              + ". Please disable it or set activation date "
              + "to a time before creating a KMS config. keyName = "
              + keyName);
      return false;
    }
    List<KeyOperation> keyOperationsList = keyVaultKey.getKeyOperations();
    if (!keyOperationsList.contains(KeyOperation.WRAP_KEY)
        || !keyOperationsList.contains(KeyOperation.UNWRAP_KEY)) {
      log.error(
          "Current key version should have atleast "
              + "'WRAP_KEY' and 'UNWRAP_KEY' as permitted operations. keyName = "
              + keyName);
      return false;
    }
    return true;
  }

  /**
   * Checks if all the required fields are present in the config object
   *
   * @param formData the config object containing client id, secret, tenant id, vault url, key name,
   *     etc.
   * @return true if all the fields are present, else false
   */
  public boolean checkFieldsExist(ObjectNode formData) {
    List<String> fieldsList =
        Arrays.asList(
            CLIENT_ID_FIELDNAME,
            CLIENT_SECRET_FIELDNAME,
            TENANT_ID_FIELDNAME,
            AZU_VAULT_URL_FIELDNAME,
            AZU_KEY_NAME_FIELDNAME,
            AZU_KEY_ALGORITHM_FIELDNAME,
            AZU_KEY_SIZE_FIELDNAME);
    for (String fieldKey : fieldsList) {
      if (!formData.has(fieldKey) || StringUtils.isBlank(formData.path(fieldKey).toString())) {
        return false;
      }
    }
    return true;
  }

  /**
   * Wrapper function that checks for validity of the input config object form data
   *
   * @param formData the config object containing client id, secret, tenant id, vault url, key name,
   *     etc.
   * @throws Exception if some required fields are missing or are invalid parameters
   */
  public void validateKMSProviderConfigFormData(ObjectNode formData) throws Exception {
    if (!checkFieldsExist(formData)) {
      throw new Exception(
          "Invalid CLIENT_ID, CLIENT_SECRET, TENANT_ID, "
              + "AZU_VAULT_URL, AZU_KEY_NAME, AZU_KEY_ALGORITHM, or AZU_KEY_SIZE");
    }

    KeyClient keyClient = getKeyClient(formData);
    if (keyClient == null) {
      log.warn("validateKMSProviderConfigFormData: Got AZU KMS KeyClient = null");
      throw new Exception("Invalid AZU KMS parameters");
    }
  }
}
