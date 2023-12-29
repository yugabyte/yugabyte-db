/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.kms.services;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.kms.algorithms.SupportedAlgorithmInterface;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil.BackupEntry;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.forms.EncryptionAtRestConfig;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.KmsHistory;
import com.yugabyte.yw.models.KmsHistoryId;
import java.io.IOException;
import java.util.Arrays;
import java.util.Base64;
import java.util.List;
import java.util.UUID;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

/**
 * An interface to be implemented for each encryption key provider service that YugaByte supports
 */
public abstract class EncryptionAtRestService<T extends SupportedAlgorithmInterface> {

  protected static final Logger LOG = LoggerFactory.getLogger(EncryptionAtRestService.class);

  protected KeyProvider keyProvider;

  protected abstract T[] getSupportedAlgorithms();

  public T validateEncryptionAlgorithm(String algorithm) {
    return Arrays.stream(getSupportedAlgorithms())
        .filter(algo -> algo.name().equals(algorithm))
        .findFirst()
        .orElse(null);
  }

  public boolean validateKeySize(int keySize, T algorithm) {
    return algorithm.getKeySizes().stream()
        .anyMatch(supportedKeySize -> supportedKeySize == keySize);
  }

  /**
   * Creates Universe key using KMS provider. If required first it creates CMK/EKE.
   *
   * @param universeUUID
   * @param configUUID
   * @param config
   * @return
   */
  protected abstract byte[] createKeyWithService(
      UUID universeUUID, UUID configUUID, EncryptionAtRestConfig config) throws IOException;

  public byte[] createKey(UUID universeUUID, UUID configUUID, EncryptionAtRestConfig config) {
    byte[] result = null;
    try {
      final byte[] existingEncryptionKey = retrieveKey(universeUUID, configUUID, config);
      if (existingEncryptionKey != null && existingEncryptionKey.length > 0) {
        final String errMsg =
            String.format(
                "Encryption key for universe %s already exists with provider %s",
                universeUUID.toString(), this.keyProvider.name());
        LOG.error(errMsg);
        throw new IllegalArgumentException(errMsg);
      }
      final byte[] ref = createKeyWithService(universeUUID, configUUID, config);
      if (ref == null || ref.length == 0) {
        final String errMsg = "createKeyWithService returned empty key ref";
        LOG.error(errMsg);
        throw new RuntimeException(errMsg);
      }
      result = ref;
    } catch (Exception e) {
      LOG.error("Error occurred attempting to create encryption key", e);
    }
    return result;
  }

  protected abstract byte[] rotateKeyWithService(
      UUID universeUUID, UUID configUUID, EncryptionAtRestConfig config) throws IOException;

  public byte[] rotateKey(UUID universeUUID, UUID configUUID, EncryptionAtRestConfig config) {
    byte[] result = null;
    try {
      final byte[] ref = rotateKeyWithService(universeUUID, configUUID, config);
      if (ref == null || ref.length == 0) {
        final String errMsg = "rotateKeyWithService returned empty key ref";
        LOG.error(errMsg);
        throw new RuntimeException(errMsg);
      }
      result = ref;
    } catch (Exception e) {
      LOG.error("Error occurred attempting to rotate encryption key", e);
    }

    return result;
  }

  public abstract byte[] retrieveKeyWithService(UUID configUUID, byte[] keyRef);

  public byte[] retrieveKey(UUID universeUUID, UUID configUUID, byte[] keyRef) {
    if (keyRef == null) {
      String errMsg =
          String.format(
              "Retrieve key could not find a key ref for universe %s...", universeUUID.toString());
      LOG.warn(errMsg);
      return null;
    }
    // Attempt to retrieve cached entry
    byte[] keyVal = EncryptionAtRestUtil.getUniverseKeyCacheEntry(universeUUID, keyRef);
    // Retrieve through KMS provider if no cache entry exists
    if (keyVal == null) {
      LOG.debug("Universe key cache entry empty. Retrieving key from service");
      keyVal = retrieveKeyWithService(configUUID, keyRef);
      // Update the cache entry
      if (keyVal != null) {
        EncryptionAtRestUtil.setUniverseKeyCacheEntry(universeUUID, keyRef, keyVal);
      } else {
        LOG.warn("Could not retrieve key from key ref for universe " + universeUUID);
      }
    }
    return keyVal;
  }

  public byte[] retrieveKey(UUID universeUUID, UUID configUUID, EncryptionAtRestConfig config) {
    byte[] key = null;
    KmsHistory activeKey = EncryptionAtRestUtil.getLatestConfigKey(universeUUID, configUUID);
    if (activeKey != null) {
      key =
          retrieveKey(
              universeUUID, configUUID, Base64.getDecoder().decode(activeKey.getUuid().keyRef));
    }

    return key;
  }

  public abstract byte[] encryptKeyWithService(UUID configUUID, byte[] universeKey);

  /**
   * Verifies if the config UUID can decrypt the given key ref (encrypted universe key).
   *
   * @param configUUID the KMS config UUID.
   * @param keyRef the encrypted universe key.
   * @return true if it can be decrypted, else false.
   */
  public boolean verifyKmsConfigAndKeyRef(UUID universeUUID, UUID configUUID, byte[] keyRef) {
    try {
      return retrieveKey(universeUUID, configUUID, keyRef) != null;
    } catch (Exception e) {
      LOG.error(e.getMessage(), e);
      // Throws an error when decrypting wrong encrypted text,
      // because the key ref stores the master key metadata (managed by the KMS provider).
      // This means it is the wrong KMS config to decrypt with - return false.
      return false;
    }
  }

  protected abstract byte[] validateRetrieveKeyWithService(
      UUID configUUID, byte[] keyRef, ObjectNode authConfig);

  public byte[] validateConfigForUpdate(
      UUID universeUUID,
      UUID configUUID,
      byte[] keyRef,
      EncryptionAtRestConfig config,
      ObjectNode authConfig) {
    if (keyRef == null) {
      String errMsg =
          String.format(
              "Retrieve key could not find a key ref for universe %s...", universeUUID.toString());
      LOG.warn(errMsg);
      return null;
    }
    // LOG.debug("DO_NOT_PRINT::config dictionary is : {}", authConfig.toString());
    return validateRetrieveKeyWithService(configUUID, keyRef, authConfig);
  }

  protected void cleanupWithService(UUID universeUUID, UUID configUUID) {}

  public void cleanup(UUID universeUUID, UUID configUUID) {
    int keyCount = EncryptionAtRestUtil.removeKeyRotationHistory(universeUUID, configUUID);
    if (keyCount != 0) {
      cleanupWithService(universeUUID, configUUID);
    }
  }

  protected EncryptionAtRestService(KeyProvider keyProvider) {
    this.keyProvider = keyProvider;
  }

  protected ObjectNode validateEncryptionKeyParams(String algorithm, int keySize) {
    final T encryptionAlgorithm = validateEncryptionAlgorithm(algorithm);
    ObjectNode result = Json.newObject().put("result", false);
    if (encryptionAlgorithm == null) {
      final String errMsg =
          String.format(
              "Requested encryption algorithm \"%s\" is not currently supported", algorithm);
      LOG.error(errMsg);
      result.put("errors", errMsg);
    } else if (!validateKeySize(keySize, encryptionAlgorithm)) {
      final String errMsg =
          String.format(
              "Requested key size %d bits is not supported by requested encryption "
                  + "algorithm \"%s\"",
              keySize, algorithm);
      LOG.error(errMsg);
      result.put("errors", errMsg);
    } else {
      result.put("result", true);
    }
    return result;
  }

  protected ObjectNode createAuthConfigWithService(UUID configUUID, ObjectNode config) {
    return config;
  }

  public KmsConfig createAuthConfig(UUID customerUUID, String configName, ObjectNode config) {
    KmsConfig result =
        KmsConfig.createKMSConfig(customerUUID, this.keyProvider, config, configName);
    UUID configUUID = result.getConfigUUID();
    ObjectNode existingConfig = getAuthConfig(configUUID);
    ObjectNode updatedConfig = createAuthConfigWithService(configUUID, existingConfig);
    if (updatedConfig != null) {
      KmsConfig.updateKMSConfig(configUUID, updatedConfig);
    } else {
      result.delete();
      result = null;
    }

    // LOG.debug("DO_NOT_PRINT::createAuthConfig returning: {}", result);
    return result;
  }

  public KmsConfig updateAuthConfig(UUID configUUID, ObjectNode config) {
    KmsConfig.updateKMSConfig(configUUID, config);
    ObjectNode existingConfig = getAuthConfig(configUUID);
    ObjectNode updatedConfig = createAuthConfigWithService(configUUID, existingConfig);

    if (updatedConfig != null) {
      return KmsConfig.updateKMSConfig(configUUID, updatedConfig);
    } else {
      return null;
    }
  }

  public ObjectNode getAuthConfig(UUID configUUID) {
    return EncryptionAtRestUtil.getAuthConfig(configUUID);
  }

  public boolean UpdateAuthConfigProperties(
      UUID customerUUID, UUID configUUID, ObjectNode updatedConfig) {
    LOG.debug(
        "Called UpdateAuthConfigProperties for {} - {} ",
        customerUUID.toString(),
        configUUID.toString());
    if (updatedConfig == null) return false;
    KmsConfig result = KmsConfig.updateKMSConfig(configUUID, updatedConfig);
    return result != null;
  }

  public List<KmsHistory> getKeyRotationHistory(UUID configUUID, UUID universeUUID) {
    List<KmsHistory> rotationHistory =
        KmsHistory.getAllConfigTargetKeyRefs(
            configUUID, universeUUID, KmsHistoryId.TargetType.UNIVERSE_KEY);
    if (rotationHistory.isEmpty()) {
      LOG.warn(
          String.format("No rotation history exists for universe %s", universeUUID.toString()));
    }
    return rotationHistory;
  }

  public void deleteKMSConfig(UUID configUUID) {
    if (!EncryptionAtRestUtil.configInUse(configUUID)) {
      final KmsConfig config = getKMSConfig(configUUID);
      if (config != null) config.delete();
    } else
      throw new IllegalArgumentException(
          String.format(
              "Cannot delete %s KMS Configuration %s since at least 1 universe"
                  + " exists using encryptionAtRest with this KMS Configuration",
              this.keyProvider.name(), configUUID.toString()));
  }

  public KmsConfig getKMSConfig(UUID configUUID) {
    return KmsConfig.get(configUUID);
  }

  public BackupEntry getBackupEntry(KmsHistory history) {
    return new BackupEntry(
        Base64.getDecoder().decode(history.getUuid().keyRef), this.keyProvider, history.dbKeyId);
  }

  // Add backed up keyRefs to the kms_history table for universeUUID and configUUID
  public void restoreBackupEntry(
      UUID universeUUID, UUID configUUID, byte[] keyRef, String dbKeyId) {
    if (!EncryptionAtRestUtil.dbKeyIdExists(universeUUID, dbKeyId)) {
      EncryptionAtRestUtil.addKeyRefAndKeyId(universeUUID, configUUID, keyRef, dbKeyId);
    }
  }

  public abstract void refreshKmsWithService(UUID configUUID, ObjectNode authConfig)
      throws Exception;

  public void refreshKms(UUID configUUID) {
    LOG.debug("Starting refresh {} KMS with KMS config '{}'.", this.keyProvider.name(), configUUID);
    try {
      ObjectNode authConfig = getAuthConfig(configUUID);
      refreshKmsWithService(configUUID, authConfig);
      LOG.info(
          "Refreshed {} KMS successfully with config '{}'.", this.keyProvider.name(), configUUID);
    } catch (Exception e) {
      final String errMsg =
          String.format(
              "Error occurred while refreshing %s KMS config '%s'.",
              this.keyProvider.name(), configUUID);
      LOG.error(errMsg, e);
      throw new RuntimeException(errMsg, e);
    }
  }

  public abstract ObjectNode getKeyMetadata(UUID configUUID);
}
