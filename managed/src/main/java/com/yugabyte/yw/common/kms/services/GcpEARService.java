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
import com.yugabyte.yw.common.kms.algorithms.GcpAlgorithm;
import com.yugabyte.yw.common.kms.util.GcpEARServiceUtil;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.forms.EncryptionAtRestConfig;

/**
 * An implementation of EncryptionAtRestService to communicate with GCP KMS
 * https://cloud.google.com/security-key-management
 */
public class GcpEARService extends EncryptionAtRestService<GcpAlgorithm> {
  private GcpEARServiceUtil gcpEARServiceUtil;
  public static final int numBytes = 32;

  public GcpEARService() {
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
      UUID universeUUID, UUID configUUID, EncryptionAtRestConfig config) {
    this.gcpEARServiceUtil = getGcpEarServiceUtil();
    byte[] result = null;
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
      case DATA_KEY:
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
      UUID universeUUID, UUID configUUID, EncryptionAtRestConfig config) {
    this.gcpEARServiceUtil = getGcpEarServiceUtil();
    byte[] result = null;
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
  public byte[] retrieveKeyWithService(
      UUID universeUUID, UUID configUUID, byte[] keyRef, EncryptionAtRestConfig config) {
    this.gcpEARServiceUtil = getGcpEarServiceUtil();
    byte[] keyVal = null;
    try {
      switch (config.type) {
        case CMK:
          keyVal = keyRef;
          break;
        default:
        case DATA_KEY:
          // Decrypt the locally stored encrypted keyRef to get the universe key.
          ObjectNode authConfig = gcpEARServiceUtil.getAuthConfig(configUUID);
          keyVal = gcpEARServiceUtil.decryptBytes(authConfig, keyRef);
          if (keyVal == null) {
            LOG.warn("Could not retrieve key from key ref through GCP KMS");
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
    this.gcpEARServiceUtil = getGcpEarServiceUtil();
    byte[] keyVal = null;
    try {
      switch (config.type) {
        case CMK:
          keyVal = keyRef;
          break;
        default:
        case DATA_KEY:
          if (authConfig == null) {
            authConfig = gcpEARServiceUtil.getAuthConfig(configUUID);
          }
          // Decrypt the locally stored encrypted keyRef to get the universe key.
          keyVal = gcpEARServiceUtil.decryptBytes(authConfig, keyRef);
          if (keyVal == null) {
            LOG.warn("Could not retrieve key from key ref through GCP KMS");
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
