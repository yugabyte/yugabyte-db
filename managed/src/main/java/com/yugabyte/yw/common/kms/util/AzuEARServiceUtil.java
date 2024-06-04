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

import com.azure.core.credential.TokenCredential;
import com.azure.core.exception.ResourceNotFoundException;
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
import com.yugabyte.yw.cloud.azu.AZUCloudImpl;
import java.security.NoSuchAlgorithmException;
import java.security.SecureRandom;
import java.time.OffsetDateTime;
import java.util.Arrays;
import java.util.List;
import java.util.Random;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public class AzuEARServiceUtil {

  // All fields in Azure KMS authConfig object sent from UI
  public enum AzuKmsAuthConfigField {
    CLIENT_ID("CLIENT_ID", true, false),
    CLIENT_SECRET("CLIENT_SECRET", true, false),
    TENANT_ID("TENANT_ID", true, false),
    AZU_VAULT_URL("AZU_VAULT_URL", false, true),
    AZU_KEY_NAME("AZU_KEY_NAME", false, true),
    AZU_KEY_ALGORITHM("AZU_KEY_ALGORITHM", false, false),
    AZU_KEY_SIZE("AZU_KEY_SIZE", false, false);

    public final String fieldName;
    public final boolean isEditable;
    public final boolean isMetadata;

    AzuKmsAuthConfigField(String fieldName, boolean isEditable, boolean isMetadata) {
      this.fieldName = fieldName;
      this.isEditable = isEditable;
      this.isMetadata = isMetadata;
    }

    public static List<String> getEditableFields() {
      return Arrays.asList(values()).stream()
          .filter(configField -> configField.isEditable)
          .map(configField -> configField.fieldName)
          .collect(Collectors.toList());
    }

    public static List<String> getNonEditableFields() {
      return Arrays.asList(values()).stream()
          .filter(configField -> !configField.isEditable)
          .map(configField -> configField.fieldName)
          .collect(Collectors.toList());
    }

    public static List<String> getMetadataFields() {
      return Arrays.asList(values()).stream()
          .filter(configField -> configField.isMetadata)
          .map(configField -> configField.fieldName)
          .collect(Collectors.toList());
    }
  }

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
    String clientId = getConfigFieldValue(authConfig, AzuKmsAuthConfigField.CLIENT_ID.fieldName);
    String tenantId = getConfigFieldValue(authConfig, AzuKmsAuthConfigField.TENANT_ID.fieldName);
    String clientSecret =
        getConfigFieldValue(authConfig, AzuKmsAuthConfigField.CLIENT_SECRET.fieldName);

    if (StringUtils.isEmpty(clientId) || StringUtils.isEmpty(tenantId)) {
      String errMsg = "Cannot get credentials. clientId or tenantId is null/empty.";
      log.error(errMsg);
      throw new RuntimeException(errMsg);
    }

    return AZUCloudImpl.getCredsOrFallbackToDefault(clientId, clientSecret, tenantId);
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
        .vaultUrl(getConfigFieldValue(authConfig, AzuKmsAuthConfigField.AZU_VAULT_URL.fieldName))
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
  public String getKeyId(ObjectNode authConfig, String keyVersion) {
    KeyClient keyClient = getKeyClient(authConfig);
    String keyName = getConfigFieldValue(authConfig, AzuKmsAuthConfigField.AZU_KEY_NAME.fieldName);
    String keyId = keyClient.getKey(keyName, keyVersion).getId();
    return keyId;
  }

  /**
   * Creates the cryptography client that is used to perform wrap and unwrap operations on the key
   *
   * @param authConfig the config object containing client id, secret, tenant id, vault url, key
   *     name, etc.
   * @return an AZU KMS crypto client object
   */
  public CryptographyClient getCryptographyClient(ObjectNode authConfig, String keyVersion) {
    return new CryptographyClientBuilder()
        .credential(getCredentials(authConfig))
        .keyIdentifier(getKeyId(authConfig, keyVersion))
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
    if (authConfig.has(AzuKmsAuthConfigField.AZU_KEY_SIZE.fieldName)) {
      keySize = authConfig.path(AzuKmsAuthConfigField.AZU_KEY_SIZE.fieldName).asInt();
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
    String keyName = getConfigFieldValue(authConfig, AzuKmsAuthConfigField.AZU_KEY_NAME.fieldName);
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
    String keyName = getConfigFieldValue(authConfig, AzuKmsAuthConfigField.AZU_KEY_NAME.fieldName);
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
    CryptographyClient cryptographyClient = getCryptographyClient(authConfig, null);
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
    CryptographyClient cryptographyClient = getCryptographyClient(authConfig, null);
    byte[] keyBytes = null;
    try {
      // First try to decrypt using primary key version
      UnwrapResult unwrapResult = cryptographyClient.unwrapKey(KeyWrapAlgorithm.RSA_OAEP, keyRef);
      keyBytes = unwrapResult.getKey();
      return keyBytes;
    } catch (Exception E) {
      // Else try to decrypt against all key versions for that key name
      String keyName =
          getConfigFieldValue(authConfig, AzuKmsAuthConfigField.AZU_KEY_NAME.fieldName);
      log.debug(
          "Could not decrypt/unwrap using primary key version of key name '{}'. "
              + "Trying other key versions.",
          keyName);
      KeyClient keyClient = getKeyClient(authConfig);
      for (KeyProperties keyProperties : keyClient.listPropertiesOfKeyVersions(keyName)) {
        cryptographyClient = getCryptographyClient(authConfig, keyProperties.getVersion());
        try {
          UnwrapResult unwrapResult =
              cryptographyClient.unwrapKey(KeyWrapAlgorithm.RSA_OAEP, keyRef);
          keyBytes = unwrapResult.getKey();
          log.info(
              "Successfully decrypted using azure key name: '{}' and key version: '{}'.",
              keyProperties.getName(),
              keyProperties.getVersion());
          return keyBytes;
        } catch (Exception e) {
          log.debug(
              "Found multiple azure key versions. "
                  + "Failed to decrypt using key name: '{}' and key version: '{}'.",
              keyProperties.getName(),
              keyProperties.getVersion());
        }
      }
    }
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
    byte[] fakeKeyBytes = new byte[32];
    new Random().nextBytes(fakeKeyBytes);
    byte[] wrappedKey = wrapKey(authConfig, fakeKeyBytes);
    byte[] unwrappedKey = unwrapKey(authConfig, wrappedKey);
    if (!Arrays.equals(fakeKeyBytes, unwrappedKey)) {
      String errMsg = "Wrap and unwrap operations gave different key in AZU KMS.";
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
              + getConfigFieldValue(authConfig, AzuKmsAuthConfigField.AZU_VAULT_URL.fieldName));
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
              keyName,
              getConfigFieldValue(authConfig, AzuKmsAuthConfigField.AZU_VAULT_URL.fieldName));
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
              getConfigFieldValue(authConfig, AzuKmsAuthConfigField.AZU_VAULT_URL.fieldName));
      log.error(errMsg);
      throw new RuntimeException(errMsg);
    }
    // Ensure the key with given name exists in the key vault
    String keyName = getConfigFieldValue(authConfig, AzuKmsAuthConfigField.AZU_KEY_NAME.fieldName);
    if (!checkKeyExists(authConfig, keyName)) {
      String errMsg =
          String.format(
              "Key does not exist in the key vault with key name = '%s' and key vault url = '%s'",
              keyName,
              getConfigFieldValue(authConfig, AzuKmsAuthConfigField.AZU_VAULT_URL.fieldName));
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
            AzuKmsAuthConfigField.CLIENT_ID.fieldName,
            AzuKmsAuthConfigField.TENANT_ID.fieldName,
            AzuKmsAuthConfigField.AZU_VAULT_URL.fieldName,
            AzuKmsAuthConfigField.AZU_KEY_NAME.fieldName,
            AzuKmsAuthConfigField.AZU_KEY_ALGORITHM.fieldName,
            AzuKmsAuthConfigField.AZU_KEY_SIZE.fieldName);
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
          "Invalid CLIENT_ID, TENANT_ID, AZU_VAULT_URL, AZU_KEY_NAME, AZU_KEY_ALGORITHM, or"
              + " AZU_KEY_SIZE");
    }

    KeyClient keyClient = getKeyClient(formData);
    if (keyClient == null) {
      log.warn("validateKMSProviderConfigFormData: Got AZU KMS KeyClient = null");
      throw new Exception("Invalid AZU KMS parameters");
    }
  }
}
