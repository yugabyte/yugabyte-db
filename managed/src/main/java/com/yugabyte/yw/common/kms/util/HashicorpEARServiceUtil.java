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

package com.yugabyte.yw.common.kms.util;

import java.util.List;
import java.util.UUID;

import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;

import com.bettercloud.vault.VaultException;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil.KeyType;
import com.yugabyte.yw.common.kms.util.hashicorpvault.VaultAccessor;
import com.yugabyte.yw.common.kms.util.hashicorpvault.VaultSecretEngineBase;
import com.yugabyte.yw.common.kms.util.hashicorpvault.VaultSecretEngineBase.SecretEngineType;
import com.yugabyte.yw.common.kms.util.hashicorpvault.VaultTransit;
import com.yugabyte.yw.models.helpers.CommonUtils;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/** Helper to perform various HashicorpEARService operations. Hides vault internals from EAR. */
public class HashicorpEARServiceUtil {
  private static final Logger LOG = LoggerFactory.getLogger(HashicorpEARServiceUtil.class);

  public static final String HC_VAULT_TOKEN = "HC_VAULT_TOKEN";
  public static final String HC_VAULT_ADDRESS = "HC_VAULT_ADDRESS";
  public static final String HC_VAULT_ENGINE = "HC_VAULT_ENGINE";
  public static final String HC_VAULT_MOUNT_PATH = "HC_VAULT_MOUNT_PATH";
  public static final String HC_VAULT_TTL = "HC_VAULT_TTL";
  public static final String HC_VAULT_TTL_EXPIRY = "HC_VAULT_TTL_EXPIRY";

  /** Creates Secret Engine object with VaultAccessor. */
  private static class VaultSecretEngineBuilder {
    private static final Logger LOG = LoggerFactory.getLogger(VaultSecretEngineBuilder.class);

    private static VaultSecretEngineBase buildSecretEngine(
        VaultAccessor accesor, String vaultSE, String mountPath, KeyType keyType) throws Exception {
      LOG.info("VaultSecretEngineBase.buildSecretEngine called");
      SecretEngineType engineType = SecretEngineType.valueOf(vaultSE.toUpperCase());

      VaultSecretEngineBase returnVault = null;
      switch (engineType) {
        case TRANSIT:
          returnVault = new VaultTransit(accesor, mountPath, keyType);
          break;
        default:
          returnVault = null;
          break;
      }
      LOG.info("Returning Object {}", returnVault.toString());
      return returnVault;
    }

    public static VaultSecretEngineBase getVaultSecretEngine(ObjectNode authConfig)
        throws Exception {
      LOG.debug("getVaultSecretEngine called");

      String vaultAddr, vaultToken;
      String vaultSE, sePath;

      vaultAddr = authConfig.get(HC_VAULT_ADDRESS).asText();
      vaultToken = authConfig.get(HC_VAULT_TOKEN).asText();
      vaultSE = authConfig.get(HC_VAULT_ENGINE).asText();
      sePath = authConfig.get(HC_VAULT_MOUNT_PATH).asText();

      try {
        LOG.info(
            "Creating Vault with : {}, {}=>{}, {}",
            vaultAddr,
            vaultSE,
            sePath,
            CommonUtils.getMaskedValue(HC_VAULT_TOKEN, vaultToken));

        VaultAccessor hcVaultAccessor = VaultAccessor.buildVaultAccessor(vaultAddr, vaultToken);
        VaultSecretEngineBase engine =
            buildSecretEngine(hcVaultAccessor, vaultSE, sePath, KeyType.CMK);
        return engine;
      } catch (VaultException e) {
        LOG.error("Vault Exception with httpStatusCode: {}", e.getHttpStatusCode());
        throw new Exception(e);
      }
    }
  }

  public static VaultSecretEngineBase getVaultSecretEngine(ObjectNode authConfig) throws Exception {
    return VaultSecretEngineBuilder.getVaultSecretEngine(authConfig);
  }

  /**
   * Generates unique KEK key name for given combination of KMSconfigUUID and UniverseUUID
   *
   * @param universeUUID
   * @param configUUID
   * @return keyName as String
   */
  public static String getVaultKeyForUniverse(UUID universeUUID, UUID configUUID) {
    String keyName = "key";
    // generate keyname using => "'key_' + "UniverseUUID + '_' + CONFIG_UUID"
    keyName = "key_" + configUUID.toString() + "_" + universeUUID.toString();
    LOG.debug("getVaultKeyForUniverse returning {}", keyName);
    return keyName;
  }

  /**
   * When configuration is accessed first time, the vault key(EKE) does not exists, this creates the
   * key (KEK) KEK - Key Encryption Key
   *
   * @param universeUUID
   * @param configUUID
   * @param authConfig
   * @return
   * @throws Exception
   */
  public static String createVaultKEK(UUID universeUUID, UUID configUUID, ObjectNode authConfig)
      throws Exception {

    String keyName = getVaultKeyForUniverse(universeUUID, configUUID);
    VaultSecretEngineBase vaultSecretEngine =
        VaultSecretEngineBuilder.getVaultSecretEngine(authConfig);
    vaultSecretEngine.createNewKeyWithEngine(keyName);

    return keyName;
  }
  /** Deletes Vault key, this operation cannot be reverted, call only when cluster is deleted. */
  public static boolean deleteVaultKey(UUID universeUUID, UUID configUUID, ObjectNode authConfig)
      throws Exception {

    String keyName = getVaultKeyForUniverse(universeUUID, configUUID);
    VaultSecretEngineBase vaultSecretEngine =
        VaultSecretEngineBuilder.getVaultSecretEngine(authConfig);
    vaultSecretEngine.deleteKey(keyName);

    return true;
  }

  /** extracts ttl and updates in authConfig, made public for testing purpose */
  public static void updateAuthConfigObj(
      UUID universeUUID, UUID configUUID, VaultSecretEngineBase engine, ObjectNode authConfig) {
    LOG.debug(
        "updateAuthConfigObj called for {} - {}", universeUUID.toString(), configUUID.toString());

    try {
      long existingTTL = -1, existingTTLExpiry = -1;
      try {
        existingTTL = Long.valueOf(authConfig.get(HashicorpEARServiceUtil.HC_VAULT_TTL).asText());
        existingTTLExpiry =
            Long.valueOf(authConfig.get(HashicorpEARServiceUtil.HC_VAULT_TTL_EXPIRY).asText());
      } catch (Exception e) {
        LOG.debug("Error fetching the vaules, updating ttl anyways");
      }

      List<Object> ttlInfo = engine.getTTL();
      if ((long) ttlInfo.get(0) == existingTTL && existingTTL == 0) {
        LOG.debug("Token never expires");
        return;
      }
      if ((long) ttlInfo.get(1) == existingTTLExpiry) {
        LOG.debug("Token properties are not changed");
        return;
      }
      LOG.debug(
          "Updating HC_VAULT_TTL_EXPIRY for Decrypt with {} and {}",
          ttlInfo.get(0),
          ttlInfo.get(1));

      authConfig.put(HashicorpEARServiceUtil.HC_VAULT_TTL, (long) ttlInfo.get(0));
      authConfig.put(HashicorpEARServiceUtil.HC_VAULT_TTL_EXPIRY, (long) ttlInfo.get(1));
    } catch (Exception e) {
      LOG.error("Unable to update TTL of token into authConfig, it will not reflect on UI", e);
    }
  }

  /**
   * Retrieve universe key from vault in the form of plaintext
   *
   * @param universeUUID
   * @param configUUID
   * @param encryptedUniverseKey
   * @param authConfig
   * @return key in plaintext
   * @throws Exception
   */
  public static byte[] decryptUniverseKey(
      UUID universeUUID, UUID configUUID, byte[] encryptedUniverseKey, ObjectNode authConfig)
      throws Exception {

    LOG.debug("decryptUniverseKey called  : {} - {}", universeUUID, configUUID);
    if (encryptedUniverseKey == null) return null;

    try {
      final String engineKey = getVaultKeyForUniverse(universeUUID, configUUID);
      VaultSecretEngineBase vaultSecretEngine =
          VaultSecretEngineBuilder.getVaultSecretEngine(authConfig);
      updateAuthConfigObj(universeUUID, configUUID, vaultSecretEngine, authConfig);

      return vaultSecretEngine.decryptString(engineKey, encryptedUniverseKey);
    } catch (VaultException e) {
      LOG.error("Vault Exception with httpStatusCode: {}", e.getHttpStatusCode());
      throw new Exception(e);
    }
  }

  /**
   * Generate universe key using vault for provided combination
   *
   * @param universeUUID
   * @param configUUID
   * @param algorithm
   * @param keySize
   * @param authConfig
   * @return encrypted universe key
   * @throws Exception
   */
  public static byte[] generateUniverseKey(
      UUID universeUUID, UUID configUUID, String algorithm, int keySize, ObjectNode authConfig)
      throws Exception {

    LOG.debug("generateUniverseKey called : {}, {}", algorithm, keySize);

    // we can also use transit/random/<keySize> to generate random key
    // but would require call to hashicorp and the return data is secret, hence not using it.

    KeyGenerator keyGen = KeyGenerator.getInstance(algorithm);
    keyGen.init(keySize);
    SecretKey secretKey = keyGen.generateKey();
    byte[] keyBytes = secretKey.getEncoded();

    LOG.debug("Generated key: of size {}", keyBytes.length);
    try {
      final String engineKey = getVaultKeyForUniverse(universeUUID, configUUID);
      VaultSecretEngineBase hcVaultSecretEngine =
          VaultSecretEngineBuilder.getVaultSecretEngine(authConfig);

      byte[] encryptedKeyBytes = hcVaultSecretEngine.encryptString(engineKey, keyBytes);
      return encryptedKeyBytes;
    } catch (VaultException e) {
      LOG.error("Vault Exception with httpStatusCode: {}", e.getHttpStatusCode());
      throw new Exception(e);
    }
  }
}
