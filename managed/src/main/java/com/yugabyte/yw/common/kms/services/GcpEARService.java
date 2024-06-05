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

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.kms.algorithms.GcpAlgorithm;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.common.kms.util.GcpEARServiceUtil;
import com.yugabyte.yw.common.kms.util.GcpEARServiceUtil.GcpKmsAuthConfigField;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.forms.EncryptionAtRestConfig;
import com.yugabyte.yw.models.KmsConfig;
import java.io.IOException;
import java.util.List;
import java.util.UUID;

/**
 * An implementation of EncryptionAtRestService to communicate with GCP KMS
 * https://cloud.google.com/security-key-management
 */
public class GcpEARService extends EncryptionAtRestService<GcpAlgorithm> {
  private GcpEARServiceUtil gcpEARServiceUtil;
  public static final int numBytes = 32;

  public GcpEARService(RuntimeConfGetter confGetter) {
    super(KeyProvider.GCP);
  }

  public GcpEARServiceUtil getGcpEarServiceUtil() {
    return new GcpEARServiceUtil();
  }

  @Override
  protected GcpAlgorithm[] getSupportedAlgorithms() {
    return GcpAlgorithm.values();
  }

  @Override
  protected ObjectNode createAuthConfigWithService(UUID configUUID, ObjectNode config) {
    this.gcpEARServiceUtil = getGcpEarServiceUtil();
    try {
      // Validate the crypto key settings if it already exists
      String cryptoKeyRN = gcpEARServiceUtil.getCryptoKeyRN(config);
      if (gcpEARServiceUtil.checkCryptoKeyExists(config)
          && !gcpEARServiceUtil.validateCryptoKeySettings(config)) {
        LOG.info(
            "Crypto key with given name exists, but has invalid settings. "
                + "Cryptokey RN = "
                + cryptoKeyRN
                + ", configUUID = "
                + configUUID.toString());
        return null;
      }
      // Ensure the key ring exists or create a new one in GCP KMS with the key ring name in the
      // auth config
      gcpEARServiceUtil.checkOrCreateKeyRing(config);
      // Ensure the crypto key exists or create new one in GCP KMS with the crypto key name in the
      // auth config. This is the actual master key stored on GCP KMS only.
      // Used to wrap and unwrap the universe key that will be created later.
      gcpEARServiceUtil.checkOrCreateCryptoKey(config);

      // Sets the correct protection level for key that already exists in GCP KMS.
      UUID customerUUID = KmsConfig.getOrBadRequest(configUUID).getCustomerUUID();
      UpdateAuthConfigProperties(customerUUID, configUUID, config);
    } catch (Exception e) {
      final String errMsg =
          String.format(
              "Error attempting to create Key Ring or Crypto Key in GCP KMS with config %s",
              configUUID.toString());
      LOG.error(errMsg, e);
      return null;
    }
    return config;
  }

  @Override
  protected byte[] createKeyWithService(
      UUID universeUUID, UUID configUUID, EncryptionAtRestConfig config) throws IOException {
    this.gcpEARServiceUtil = getGcpEarServiceUtil();
    byte[] result;
    ObjectNode authConfig = gcpEARServiceUtil.getAuthConfig(configUUID);
    // Ensure the key ring exists from GCP KMS
    String keyRingRN = gcpEARServiceUtil.getKeyRingRN(authConfig);
    if (!gcpEARServiceUtil.checkKeyRingExists(authConfig)) {
      String errMsg = "Key Ring does not exist: " + keyRingRN;
      LOG.error(errMsg);
      throw new RuntimeException(errMsg);
    }
    // Ensure the crypto key exists in GCP KMS
    String cryptoKeyRN = gcpEARServiceUtil.getCryptoKeyRN(authConfig);
    if (!gcpEARServiceUtil.checkCryptoKeyExists(authConfig)) {
      String errMsg = "Crypto Key does not exist: " + cryptoKeyRN;
      LOG.error(errMsg);
      throw new RuntimeException(errMsg);
    }
    switch (config.type) {
      case CMK:
        result = gcpEARServiceUtil.getCryptoKey(authConfig).getName().getBytes();
        break;
      default:
        // Generate random byte array and encrypt it.
        // Store the encrypted byte array locally in the db.
        byte[] keyBytes = gcpEARServiceUtil.generateRandomBytes(authConfig, numBytes);
        result = gcpEARServiceUtil.encryptBytes(authConfig, keyBytes);
        break;
    }
    return result;
  }

  @Override
  protected byte[] rotateKeyWithService(
      UUID universeUUID, UUID configUUID, EncryptionAtRestConfig config) throws IOException {
    this.gcpEARServiceUtil = getGcpEarServiceUtil();
    byte[] result;
    ObjectNode authConfig = gcpEARServiceUtil.getAuthConfig(configUUID);
    // Ensure the key ring exists from GCP KMS to universe UUID
    String keyRingRN = gcpEARServiceUtil.getKeyRingRN(authConfig);
    if (!gcpEARServiceUtil.checkKeyRingExists(authConfig)) {
      String errMsg = "Key Ring does not exist: " + keyRingRN;
      LOG.error(errMsg);
      throw new RuntimeException(errMsg);
    }
    // Ensure the crypto key exists in GCP KMS
    String cryptoKeyRN = gcpEARServiceUtil.getCryptoKeyRN(authConfig);
    if (!gcpEARServiceUtil.checkCryptoKeyExists(authConfig)) {
      String errMsg = "Crypto Key does not exist: " + cryptoKeyRN;
      LOG.error(errMsg);
      throw new RuntimeException(errMsg);
    }
    // Generate random byte array and encrypt it.
    // Store the encrypted byte array locally in the db.
    byte[] keyBytes = gcpEARServiceUtil.generateRandomBytes(authConfig, numBytes);
    result = gcpEARServiceUtil.encryptBytes(authConfig, keyBytes);
    return result;
  }

  @Override
  public byte[] retrieveKeyWithService(UUID configUUID, byte[] keyRef) {
    this.gcpEARServiceUtil = getGcpEarServiceUtil();
    byte[] keyVal;
    try {
      // Decrypt the locally stored encrypted keyRef to get the universe key.
      ObjectNode authConfig = gcpEARServiceUtil.getAuthConfig(configUUID);
      keyVal = gcpEARServiceUtil.decryptBytes(authConfig, keyRef);
      if (keyVal == null) {
        LOG.warn("Could not retrieve key from key ref through GCP KMS");
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
      UUID configUUID, byte[] keyRef, ObjectNode authConfig) {
    this.gcpEARServiceUtil = getGcpEarServiceUtil();
    byte[] keyVal;
    try {
      if (authConfig == null) {
        authConfig = gcpEARServiceUtil.getAuthConfig(configUUID);
      }
      // Decrypt the locally stored encrypted keyRef to get the universe key.
      keyVal = gcpEARServiceUtil.decryptBytes(authConfig, keyRef);
      if (keyVal == null) {
        LOG.warn("Could not retrieve key from key ref through GCP KMS");
      }
    } catch (Exception e) {
      final String errMsg = "Error occurred retrieving encryption key";
      LOG.error(errMsg, e);
      throw new RuntimeException(errMsg, e);
    }
    return keyVal;
  }

  @Override
  public byte[] encryptKeyWithService(UUID configUUID, byte[] universeKey) {
    this.gcpEARServiceUtil = getGcpEarServiceUtil();
    byte[] encryptedUniverseKey;
    try {
      ObjectNode authConfig = getAuthConfig(configUUID);
      encryptedUniverseKey = gcpEARServiceUtil.encryptBytes(authConfig, universeKey);
      if (encryptedUniverseKey == null) {
        throw new RuntimeException("Encrypted universe key is null.");
      }
    } catch (Exception e) {
      final String errMsg =
          String.format(
              "Error occurred encrypting universe key in GCP KMS with config UUID '%s'.",
              configUUID);
      LOG.error(errMsg, e);
      throw new RuntimeException(errMsg, e);
    }
    return encryptedUniverseKey;
  }

  @Override
  protected void cleanupWithService(UUID universeUUID, UUID configUUID) {
    // Do nothing to KMS when deleting universe with EAR enabled
  }

  @Override
  public void refreshKmsWithService(UUID configUUID, ObjectNode authConfig) throws Exception {
    this.gcpEARServiceUtil = getGcpEarServiceUtil();
    gcpEARServiceUtil.validateKMSProviderConfigFormData(authConfig);

    if (!gcpEARServiceUtil.validateCryptoKeySettings(authConfig)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format("Key does not have valid settings in GCP KMS config '%s'.", configUUID));
    }
    gcpEARServiceUtil.testWrapAndUnwrapKey(authConfig);
  }

  @Override
  public ObjectNode getKeyMetadata(UUID configUUID) {
    // Get all the auth config fields marked as metadata.
    List<String> gcpKmsMetadataFields = GcpKmsAuthConfigField.getMetadataFields();
    ObjectNode authConfig = EncryptionAtRestUtil.getAuthConfig(configUUID);
    ObjectNode keyMetadata = new ObjectMapper().createObjectNode();

    for (String fieldName : gcpKmsMetadataFields) {
      if (authConfig.has(fieldName)) {
        keyMetadata.set(fieldName, authConfig.get(fieldName));
      }
    }
    // Add the GCP project ID to the key metadata as well.
    // This is useful info to the user.
    if (authConfig.has(GcpKmsAuthConfigField.GCP_CONFIG.fieldName)
        && authConfig.get(GcpKmsAuthConfigField.GCP_CONFIG.fieldName).has("project_id")) {
      keyMetadata.set(
          "project_id",
          authConfig.get(GcpKmsAuthConfigField.GCP_CONFIG.fieldName).get("project_id"));
    }
    // Add key_provider field.
    keyMetadata.put("key_provider", KeyProvider.GCP.name());
    return keyMetadata;
  }
}
