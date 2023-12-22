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

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.kms.algorithms.HashicorpVaultAlgorithm;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.common.kms.util.HashicorpEARServiceUtil;
import com.yugabyte.yw.common.kms.util.HashicorpEARServiceUtil.VaultSecretEngineBuilder;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.common.kms.util.hashicorpvault.HashicorpVaultConfigParams;
import com.yugabyte.yw.common.kms.util.hashicorpvault.VaultSecretEngineBase;
import com.yugabyte.yw.forms.EncryptionAtRestConfig;
import com.yugabyte.yw.models.KmsConfig;
import java.util.List;
import java.util.UUID;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * An implementation of EncryptionAtRestService to communicate with Hashicorp Vault
 * https://www.vaultproject.io/docs/secrets
 */
public class HashicorpEARService extends EncryptionAtRestService<HashicorpVaultAlgorithm> {
  protected static final Logger LOG = LoggerFactory.getLogger(HashicorpEARService.class);
  private final RuntimeConfGetter confGetter;

  static final String algorithm = "AES";
  static final int keySize = 256;

  public HashicorpEARService(RuntimeConfGetter confGetter) {
    super(KeyProvider.HASHICORP);
    this.confGetter = confGetter;
  }

  @Override
  protected HashicorpVaultAlgorithm[] getSupportedAlgorithms() {
    return HashicorpVaultAlgorithm.values();
  }

  @Override
  protected ObjectNode createAuthConfigWithService(UUID configUUID, ObjectNode authConfig) {
    ObjectNode result = null;

    VaultSecretEngineBase engine = null;
    try {
      // creates vault accessor object and validates the token
      engine = HashicorpEARServiceUtil.getVaultSecretEngine(authConfig);
      List<Object> ttlInfo = engine.getTTL();
      result = authConfig;

      LOG.debug(
          "Updating HC_VAULT_TTL_EXPIRY for createAuthConfigWithService with {} and {}",
          ttlInfo.get(0),
          ttlInfo.get(1));
      result.put(HashicorpVaultConfigParams.HC_VAULT_TTL, (long) ttlInfo.get(0));
      result.put(HashicorpVaultConfigParams.HC_VAULT_TTL_EXPIRY, (long) ttlInfo.get(1));

    } catch (Exception e) {
      final String errMsg =
          String.format("Error while connecting to Vault for config %s", configUUID.toString());
      LOG.error(errMsg, e);
    }

    try {
      // Check if key with given name in the authConfig exists, else create a new one.
      HashicorpEARServiceUtil.createVaultKEK(configUUID, authConfig);
      HashicorpEARServiceUtil.testEncryptDecrypt(engine, algorithm, configUUID);
    } catch (Exception e) {
      LOG.error(
          "Error while trying to check/create a key in the vault for config UUID '{}'.",
          configUUID,
          e);
      return null;
    }
    LOG.info("Returning from createAuthConfigWithService");
    return result;
  }

  private byte[] generateUniverseDataKey(
      UUID configUUID, UUID universeUUID, String algorithm, int keySize) {
    byte[] result = null;
    LOG.info("generateUniverseDataKey called : {}, {}", algorithm, keySize);

    try {
      final ObjectNode validateResult = validateEncryptionKeyParams(algorithm, keySize);
      if (!validateResult.get("result").asBoolean()) {
        final String errMsg =
            String.format(
                "Invalid encryption key parameters detected for create/rotate data key"
                    + " operation in universe %s: %s",
                universeUUID, validateResult.get("errors").asText());
        LOG.error(errMsg);
        throw new IllegalArgumentException(errMsg);
      }

      final ObjectNode authConfig = getAuthConfig(configUUID);
      LOG.debug("generateUniverseDataKey about to call generateUniverseKey");
      result =
          HashicorpEARServiceUtil.generateUniverseKey(
              universeUUID, configUUID, algorithm, keySize, authConfig);
    } catch (Exception e) {
      String errMsg =
          String.format(
              "Error generating key for universe %s - %s",
              universeUUID.toString(), configUUID.toString());
      LOG.error(errMsg, e);
    }
    return result;
  }

  @Override
  protected byte[] createKeyWithService(
      UUID universeUUID, UUID configUUID, EncryptionAtRestConfig config) {
    LOG.info("createKeyWithService called : {}, {}", universeUUID, configUUID);

    try {
      final ObjectNode authConfig = getAuthConfig(configUUID);
      // currently we use only KEK property of vault transit engine (created only if required)
      HashicorpEARServiceUtil.createVaultKEK(configUUID, authConfig);
    } catch (Exception e) {
      final String errMsg = "Error occurred creating encryption key";
      LOG.error(errMsg, e);
      throw new RuntimeException(errMsg, e);
    }

    return generateUniverseDataKey(configUUID, universeUUID, algorithm, keySize);
  }

  @Override
  protected byte[] rotateKeyWithService(
      UUID universeUUID, UUID configUUID, EncryptionAtRestConfig config) {
    LOG.info("rotateKeyWithService called: {}, {}", universeUUID, configUUID);

    final byte[] currentKey = retrieveKey(universeUUID, configUUID, config);
    if (currentKey == null || currentKey.length == 0) {
      final String errMsg =
          String.format(
              "Universe encryption key for universe %s does not exist", universeUUID.toString());
      LOG.error(errMsg);
      throw new IllegalArgumentException(errMsg);
    }

    return generateUniverseDataKey(configUUID, universeUUID, algorithm, keySize);
  }

  @Override
  public byte[] validateRetrieveKeyWithService(
      UUID configUUID, byte[] keyRef, ObjectNode authConfig) {

    LOG.debug("validateRetrieveKeyWithService called on config UUID: '{}'", configUUID);

    byte[] keyVal = null;
    try {
      // keyRef is ciphertext
      keyVal = HashicorpEARServiceUtil.decryptUniverseKey(configUUID, keyRef, authConfig);
      if (keyVal == null) {
        LOG.warn("Could not retrieve key from key ref through KMS");
      }
    } catch (Exception e) {
      final String errMsg = "Error occurred while validating encryption key";
      LOG.error(errMsg, e);
      throw new RuntimeException(errMsg, e);
    }
    return keyVal;
  }

  void updateCurrentAuthConfigProperties(UUID configUUID, ObjectNode authConfig) {
    LOG.debug("updateCurrentAuthConfigProperties called for {}", configUUID.toString());
    try {
      KmsConfig config = KmsConfig.getOrBadRequest(configUUID);
      UUID customerUUID = config.getCustomerUUID();

      UpdateAuthConfigProperties(customerUUID, configUUID, authConfig);
    } catch (Exception e) {
      LOG.error("Unable to update TTL of token into KMSConfig, it will not reflect on UI", e);
    }
  }

  @Override
  public byte[] retrieveKeyWithService(UUID configUUID, byte[] keyRef) {
    LOG.debug("retrieveKeyWithService called on config UUID: '{}'", configUUID);

    try {
      final ObjectNode authConfig = getAuthConfig(configUUID);
      byte[] key = validateRetrieveKeyWithService(configUUID, keyRef, authConfig);
      updateCurrentAuthConfigProperties(configUUID, authConfig);
      return key;
    } catch (Exception e) {
      final String errMsg = "Error occurred while retrieving encryption key";
      LOG.error(errMsg, e);
      throw new RuntimeException(errMsg, e);
    }
  }

  @Override
  public byte[] encryptKeyWithService(UUID configUUID, byte[] universeKey) {
    byte[] encryptedUniverseKey = null;
    try {
      ObjectNode authConfig = EncryptionAtRestUtil.getAuthConfig(configUUID);
      encryptedUniverseKey =
          HashicorpEARServiceUtil.encryptUniverseKey(configUUID, authConfig, universeKey);
      if (encryptedUniverseKey == null) {
        throw new RuntimeException("Encrypted universe key is null.");
      }
    } catch (Exception e) {
      final String errMsg =
          String.format(
              "Error occurred encrypting universe key in Hashicorp KMS with config UUID '%s'.",
              configUUID);
      LOG.error(errMsg, e);
      throw new RuntimeException(errMsg, e);
    }
    return encryptedUniverseKey;
  }

  protected void cleanupWithService(UUID universeUUID, UUID configUUID) {
    LOG.info("cleanupWithService called: {}, {}", universeUUID, configUUID);
  }

  @Override
  public void refreshKmsWithService(UUID configUUID, ObjectNode authConfig) throws Exception {
    // Refresh TTL info in the authConfig object.
    VaultSecretEngineBase vaultSecretEngine =
        VaultSecretEngineBuilder.getVaultSecretEngine(authConfig);
    HashicorpEARServiceUtil.updateAuthConfigObj(configUUID, vaultSecretEngine, authConfig);
    final String engineKey = HashicorpEARServiceUtil.getVaultKeyForUniverse(authConfig);

    // Test encrypt and decrypt for Hashicorp KMS config with fake data
    HashicorpEARServiceUtil.testEncryptDecrypt(vaultSecretEngine, engineKey, configUUID);
  }

  @Override
  public ObjectNode getKeyMetadata(UUID configUUID) {
    ObjectNode authConfig = EncryptionAtRestUtil.getAuthConfig(configUUID);
    ObjectNode keyMetadata = new ObjectMapper().createObjectNode();
    // All the hashicorp metadata fields.
    List<String> metadataFields = HashicorpEARServiceUtil.getMetadataFields();

    // Add all the metadata fields.
    for (String fieldName : metadataFields) {
      if (authConfig.has(fieldName)) {
        keyMetadata.set(fieldName, authConfig.get(fieldName));
      }
    }
    // Add key_provider field.
    keyMetadata.put("key_provider", KeyProvider.HASHICORP.name());
    return keyMetadata;
  }
}
