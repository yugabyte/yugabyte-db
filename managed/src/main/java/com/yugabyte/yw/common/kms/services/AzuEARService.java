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

package com.yugabyte.yw.common.kms.services;

import java.util.UUID;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.kms.algorithms.AzuAlgorithm;
import com.yugabyte.yw.common.kms.util.AzuEARServiceUtil;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.forms.EncryptionAtRestConfig;

/**
 * An implementation of EncryptionAtRestService to communicate with AZU KMS
 * https://azure.microsoft.com/en-in/services/key-vault/
 */
public class AzuEARService extends EncryptionAtRestService<AzuAlgorithm> {
  private AzuEARServiceUtil azuEARServiceUtil;
  public static final int numBytes = 32;

  public AzuEARService() {
    super(KeyProvider.AZU);
  }

  public boolean validateKeyAlgorithmAndSize(ObjectNode authConfig) {
    // Checks if authConfig has valid key algorithm and key size as specified in AzuAlgorithm.java
    String keyAlgorithm =
        azuEARServiceUtil.getConfigFieldValue(
            authConfig, AzuEARServiceUtil.AZU_KEY_ALGORITHM_FIELDNAME);
    int keySize = azuEARServiceUtil.getConfigKeySize(authConfig);
    AzuAlgorithm azuAlgorithm = validateEncryptionAlgorithm(keyAlgorithm);
    if (azuAlgorithm == null) {
      return false;
    }
    return validateKeySize(keySize, azuAlgorithm);
  }

  public AzuEARServiceUtil getAzuEarServiceUtil() {
    return new AzuEARServiceUtil();
  }

  @Override
  protected AzuAlgorithm[] getSupportedAlgorithms() {
    return AzuAlgorithm.values();
  }

  @Override
  protected ObjectNode createAuthConfigWithService(UUID configUUID, ObjectNode config) {
    this.azuEARServiceUtil = getAzuEarServiceUtil();
    try {
      // Check if the key algorithm and key size are valid
      if (!validateKeyAlgorithmAndSize(config)) {
        LOG.error("Key algorithm or key size is invalid.");
        return null;
      }
      // Check if key vault is valid or not
      boolean validKeyVault = azuEARServiceUtil.checkKeyVaultisValid(config);
      LOG.info("AzuEARService-createAuthConfigWithService: Checked key vault is valid.");
      if (!validKeyVault) {
        String errMsg =
            String.format(
                "Key vault or the credentials are invalid. key vault url = '%s'",
                azuEARServiceUtil.getConfigFieldValue(
                    config, AzuEARServiceUtil.AZU_VAULT_URL_FIELDNAME));
        LOG.error(errMsg);
        throw new RuntimeException(errMsg);
      }

      // Check if key already exists in key vault
      // If key exists, it is validated before usage
      // Else, a new master key is created
      String keyName =
          azuEARServiceUtil.getConfigFieldValue(config, AzuEARServiceUtil.AZU_KEY_NAME_FIELDNAME);
      boolean checkKeyExists = azuEARServiceUtil.checkKeyExists(config, keyName);
      if (checkKeyExists) {
        if (azuEARServiceUtil.validateKeySettings(config, keyName)) {
          // Wrap and unwrap are 2 extra permissions that are required
          // when creating a universe key later
          azuEARServiceUtil.testWrapAndUnwrapKey(config);
          LOG.info("AZU KMS key exists and has valid settings");
          return config;
        } else {
          LOG.error("AZU KMS key exists, but has invalid settings");
          return null;
        }
      } else {
        LOG.info("AZU KMS key with given name doesn't already exist. Creating a new key...");
        azuEARServiceUtil.createKey(config);
        azuEARServiceUtil.testWrapAndUnwrapKey(config);
        return config;
      }
    } catch (Exception e) {
      final String errMsg =
          String.format(
              "Error attempting to validate key vault or create key in AZU KMS with config %s",
              configUUID.toString());
      LOG.error(errMsg, e);
      return null;
    }
  }

  @Override
  protected byte[] createKeyWithService(
      UUID universeUUID, UUID configUUID, EncryptionAtRestConfig config) {
    this.azuEARServiceUtil = getAzuEarServiceUtil();
    byte[] result = null;
    ObjectNode authConfig = azuEARServiceUtil.getAuthConfig(configUUID);
    // Ensure the key vault and master key exist
    azuEARServiceUtil.checkKeyVaultAndKeyExists(authConfig);
    switch (config.type) {
      case CMK:
        result = azuEARServiceUtil.getKey(authConfig).getKey().getId().getBytes();
        break;
      default:
      case DATA_KEY:
        // Generate random byte array and encrypt it.
        // Store the encrypted byte array locally in the db.
        byte[] keyBytes = azuEARServiceUtil.generateRandomBytes(numBytes);
        result = azuEARServiceUtil.wrapKey(authConfig, keyBytes);
        break;
    }
    return result;
  }

  @Override
  protected byte[] rotateKeyWithService(
      UUID universeUUID, UUID configUUID, EncryptionAtRestConfig config) {
    this.azuEARServiceUtil = getAzuEarServiceUtil();
    byte[] result = null;
    ObjectNode authConfig = azuEARServiceUtil.getAuthConfig(configUUID);
    // Ensure the key vault and master key exist
    azuEARServiceUtil.checkKeyVaultAndKeyExists(authConfig);
    // Generate random byte array and encrypt it.
    // Store the encrypted byte array locally in the db.
    byte[] keyBytes = azuEARServiceUtil.generateRandomBytes(numBytes);
    result = azuEARServiceUtil.wrapKey(authConfig, keyBytes);
    return result;
  }

  @Override
  public byte[] retrieveKeyWithService(
      UUID universeUUID, UUID configUUID, byte[] keyRef, EncryptionAtRestConfig config) {
    this.azuEARServiceUtil = getAzuEarServiceUtil();
    byte[] keyVal = null;
    try {
      switch (config.type) {
        case CMK:
          keyVal = keyRef;
          break;
        default:
        case DATA_KEY:
          // Check if the key vault exists and key with given name exists in the key vault
          ObjectNode authConfig = azuEARServiceUtil.getAuthConfig(configUUID);
          azuEARServiceUtil.checkKeyVaultAndKeyExists(authConfig);
          // Decrypt the locally stored encrypted byte array to give the universe key.
          keyVal = azuEARServiceUtil.unwrapKey(authConfig, keyRef);
          if (keyVal == null) {
            LOG.warn("Could not retrieve key from key ref through AZU KMS");
          }
          break;
      }
    } catch (Exception e) {
      final String errMsg = "Error occurred retrieving encryption key";
      LOG.error(errMsg, e);
      throw new RuntimeException(errMsg, e);
    }
    return keyVal;
  }

  @Override
  protected byte[] validateRetrieveKeyWithService(
      UUID universeUUID,
      UUID configUUID,
      byte[] keyRef,
      EncryptionAtRestConfig config,
      ObjectNode authConfig) {
    this.azuEARServiceUtil = getAzuEarServiceUtil();
    byte[] keyVal = null;
    try {
      switch (config.type) {
        case CMK:
          keyVal = keyRef;
          break;
        default:
        case DATA_KEY:
          // Check if the key vault exists and key with given name exists in the key vault
          azuEARServiceUtil.checkKeyVaultAndKeyExists(authConfig);
          // Decrypt the locally stored encrypted byte array to give the universe key.
          keyVal = azuEARServiceUtil.unwrapKey(authConfig, keyRef);
          if (keyVal == null) {
            LOG.warn("Could not retrieve key from key ref through AZU KMS");
          }
          break;
      }
    } catch (Exception e) {
      final String errMsg = "Error occurred retrieving encryption key";
      LOG.error(errMsg, e);
      throw new RuntimeException(errMsg, e);
    }
    return keyVal;
  }

  @Override
  protected void cleanupWithService(UUID universeUUID, UUID configUUID) {
    // Do nothing to KMS when deleting universe with EAR enabled
  }
}
