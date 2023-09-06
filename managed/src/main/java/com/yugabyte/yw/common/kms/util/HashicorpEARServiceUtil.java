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

import static play.mvc.Http.Status.BAD_REQUEST;

import com.bettercloud.vault.VaultException;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil.KeyType;
import com.yugabyte.yw.common.kms.util.hashicorpvault.HashicorpVaultConfigParams;
import com.yugabyte.yw.common.kms.util.hashicorpvault.VaultAccessor;
import com.yugabyte.yw.common.kms.util.hashicorpvault.VaultSecretEngineBase;
import com.yugabyte.yw.common.kms.util.hashicorpvault.VaultSecretEngineBase.KMSEngineType;
import com.yugabyte.yw.common.kms.util.hashicorpvault.VaultTransit;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.helpers.CommonUtils;
import java.security.SecureRandom;
import java.util.Arrays;
import java.util.List;
import java.util.UUID;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/** Helper to perform various HashicorpEARService operations. Hides vault internals from EAR. */
public class HashicorpEARServiceUtil {
  private static final Logger LOG = LoggerFactory.getLogger(HashicorpEARServiceUtil.class);

  public static final String HC_VAULT_EKE_NAME = "key_yugabyte";

  /** Creates Secret Engine object with VaultAccessor. */
  public static class VaultSecretEngineBuilder {
    private static final Logger LOG = LoggerFactory.getLogger(VaultSecretEngineBuilder.class);

    private static VaultSecretEngineBase buildSecretEngine(
        VaultAccessor accesor, String vaultSE, String mountPath, KeyType keyType) throws Exception {
      LOG.info("VaultSecretEngineBase.buildSecretEngine called");
      KMSEngineType engineType = KMSEngineType.valueOf(vaultSE.toUpperCase());

      VaultSecretEngineBase returnVault = null;
      switch (engineType) {
        case TRANSIT:
          returnVault = new VaultTransit(accesor, mountPath, keyType);
          break;
        default:
          break;
      }
      LOG.info("Returning Object {}", returnVault);
      return returnVault;
    }

    public static VaultSecretEngineBase getVaultSecretEngine(ObjectNode authConfig)
        throws Exception {
      LOG.debug("getVaultSecretEngine called");

      String vaultAddr, vaultToken;
      String vaultSE, sePath;
      String vaultRoleID, vaultSecretID;

      vaultToken =
          authConfig.get(HashicorpVaultConfigParams.HC_VAULT_TOKEN) == null
              ? ""
              : authConfig.get(HashicorpVaultConfigParams.HC_VAULT_TOKEN).asText();
      vaultRoleID =
          authConfig.get(HashicorpVaultConfigParams.HC_VAULT_ROLE_ID) == null
              ? ""
              : authConfig.get(HashicorpVaultConfigParams.HC_VAULT_ROLE_ID).asText();
      vaultSecretID =
          authConfig.get(HashicorpVaultConfigParams.HC_VAULT_SECRET_ID) == null
              ? ""
              : authConfig.get(HashicorpVaultConfigParams.HC_VAULT_SECRET_ID).asText();

      if (vaultToken.isBlank() && (vaultRoleID.isBlank() || vaultSecretID.isBlank())) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "Found no valid vault auth details. "
                + "Please provide either token or appRole authentication.");
      } else if (!vaultToken.isBlank() && (!vaultRoleID.isBlank() && !vaultSecretID.isBlank())) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Please provide one of token or appRole authentication.");
      }

      vaultAddr = authConfig.get(HashicorpVaultConfigParams.HC_VAULT_ADDRESS).asText();
      vaultSE = authConfig.get(HashicorpVaultConfigParams.HC_VAULT_ENGINE).asText();
      sePath = authConfig.get(HashicorpVaultConfigParams.HC_VAULT_MOUNT_PATH).asText();

      try {
        VaultAccessor hcVaultAccessor;
        if (vaultToken.isBlank()) {
          LOG.info(
              "Creating Vault with : {}, {}=>{}, {}, {}",
              vaultAddr,
              vaultSE,
              sePath,
              CommonUtils.getMaskedValue(HashicorpVaultConfigParams.HC_VAULT_ROLE_ID, vaultRoleID),
              CommonUtils.getMaskedValue(
                  HashicorpVaultConfigParams.HC_VAULT_SECRET_ID, vaultSecretID));

          String vaultAuthNamespace =
              authConfig.get(HashicorpVaultConfigParams.HC_VAULT_AUTH_NAMESPACE) != null
                  ? authConfig.get(HashicorpVaultConfigParams.HC_VAULT_AUTH_NAMESPACE).asText()
                  : "";

          if (!vaultAuthNamespace.isBlank()) {
            // check is vaultAuthNamespace is prefix of the mount path
            // if true, remove the vaultAuthNamespace substring
            // if not, throw error
            if (sePath.startsWith(vaultAuthNamespace)) {
              // +1 is to remove the / after the auth namespace
              if (!vaultAuthNamespace.endsWith("/")) {
                sePath = sePath.substring(vaultAuthNamespace.length() + 1);
              } else {
                sePath = sePath.substring(vaultAuthNamespace.length());
              }
            } else {
              throw new PlatformServiceException(
                  BAD_REQUEST,
                  "Transit engine mount path should be present in the same or child namespaces");
            }
          }

          hcVaultAccessor =
              VaultAccessor.buildVaultAccessorFromAppRole(
                  vaultAddr, vaultAuthNamespace, vaultRoleID, vaultSecretID);

        } else {
          LOG.info(
              "Creating Vault with : {}, {}=>{}, {}",
              vaultAddr,
              vaultSE,
              sePath,
              CommonUtils.getMaskedValue(HashicorpVaultConfigParams.HC_VAULT_TOKEN, vaultToken));
          hcVaultAccessor = VaultAccessor.buildVaultAccessor(vaultAddr, vaultToken);
        }

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
   * Earlier we used a hardcoded key name of "key_yugabyte" when creating a hashicorp KMS config.
   * But there was a migration V235__HashicorpVaultAddKeyName which adds this key name to the
   * authConfig object in the kms_config table. Now we let the user create a key with a custom key
   * name at the time of creating a KMS config if specified, else we create with the default name
   * "key_yugabyte".
   *
   * @param authConfig the auth config object.
   * @return keyName as String.
   */
  public static String getVaultKeyForUniverse(ObjectNode authConfig) {
    String keyName = "";
    if (authConfig.has(HashicorpVaultConfigParams.HC_VAULT_KEY_NAME)) {
      // Ran the migration V235__HashicorpVaultAddKeyName to add this param to the authConfig object
      // in every existing HC vault KMS config.
      // Now every HC vault KMS config should have the key name in the authConfig.
      keyName = authConfig.path(HashicorpVaultConfigParams.HC_VAULT_KEY_NAME).asText();
    } else {
      // Was hardcoded to this before running the migration mentioned above.
      // This case is triggered when creating a KMS config without passing key name.
      keyName = HC_VAULT_EKE_NAME;
      ObjectNode updatedAuthConfig = authConfig;
      updatedAuthConfig.put(
          HashicorpVaultConfigParams.HC_VAULT_KEY_NAME, HashicorpEARServiceUtil.HC_VAULT_EKE_NAME);
    }
    LOG.debug("getVaultKeyForUniverse returning {}", keyName);
    return keyName;
  }

  /**
   * When configuration is accessed first time, the vault key(EKE) does not exists, this creates the
   * key (KEK) KEK - Key Encryption Key
   *
   * @param authConfig
   * @return
   * @throws Exception
   */
  public static String createVaultKEK(UUID configUUID, ObjectNode authConfig) throws Exception {
    String keyName = getVaultKeyForUniverse(authConfig);
    VaultSecretEngineBase vaultSecretEngine =
        VaultSecretEngineBuilder.getVaultSecretEngine(authConfig);
    vaultSecretEngine.createNewKeyWithEngine(keyName);
    return keyName;
  }

  /**
   * Deletes Vault key, this operation cannot be reverted. Used only for TESTING, do not call in
   * production code.
   */
  public static boolean deleteVaultKey(UUID universeUUID, UUID configUUID, ObjectNode authConfig)
      throws Exception {

    String keyName = getVaultKeyForUniverse(authConfig);
    VaultSecretEngineBase vaultSecretEngine =
        VaultSecretEngineBuilder.getVaultSecretEngine(authConfig);
    vaultSecretEngine.deleteKey(keyName);

    return true;
  }

  /** extracts ttl and updates in authConfig, made public for testing purpose */
  public static void updateAuthConfigObj(
      UUID configUUID, VaultSecretEngineBase engine, ObjectNode authConfig) {
    LOG.debug("updateAuthConfigObj called on KMS config '{}'", configUUID.toString());

    try {
      long existingTTL = -1, existingTTLExpiry = -1;
      try {
        existingTTL =
            Long.valueOf(authConfig.get(HashicorpVaultConfigParams.HC_VAULT_TTL).asText());
        existingTTLExpiry =
            Long.valueOf(authConfig.get(HashicorpVaultConfigParams.HC_VAULT_TTL_EXPIRY).asText());
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
          "Updating HC_VAULT_TTL_EXPIRY for KMS config {} with {} and {}",
          configUUID,
          ttlInfo.get(0),
          ttlInfo.get(1));

      authConfig.put(HashicorpVaultConfigParams.HC_VAULT_TTL, (long) ttlInfo.get(0));
      authConfig.put(HashicorpVaultConfigParams.HC_VAULT_TTL_EXPIRY, (long) ttlInfo.get(1));
      KmsConfig result = KmsConfig.updateKMSConfig(configUUID, authConfig);
      if (result != null) {
        LOG.debug(
            "Successfully updated HC_VAULT_TTL_EXPIRY for KMS config {} with {} and {}",
            configUUID,
            ttlInfo.get(0),
            ttlInfo.get(1));
      }
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
      UUID configUUID, byte[] encryptedUniverseKey, ObjectNode authConfig) throws Exception {

    LOG.debug("decryptUniverseKey called on config UUID : '{}'", configUUID);
    if (encryptedUniverseKey == null) return null;

    try {
      final String engineKey = getVaultKeyForUniverse(authConfig);
      VaultSecretEngineBase vaultSecretEngine =
          VaultSecretEngineBuilder.getVaultSecretEngine(authConfig);
      updateAuthConfigObj(configUUID, vaultSecretEngine, authConfig);

      return vaultSecretEngine.decryptString(engineKey, encryptedUniverseKey);
    } catch (VaultException e) {
      LOG.error("Vault Exception with httpStatusCode: {}", e.getHttpStatusCode());
      throw new Exception(e);
    }
  }

  public static byte[] encryptUniverseKey(
      UUID configUUID, ObjectNode authConfig, byte[] universeKey) throws Exception {
    try {
      final String engineKey = getVaultKeyForUniverse(authConfig);
      VaultSecretEngineBase vaultSecretEngine =
          VaultSecretEngineBuilder.getVaultSecretEngine(authConfig);
      updateAuthConfigObj(configUUID, vaultSecretEngine, authConfig);

      return vaultSecretEngine.encryptString(engineKey, universeKey);
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
      byte[] encryptedKeyBytes = encryptUniverseKey(configUUID, authConfig, keyBytes);
      return encryptedKeyBytes;
    } catch (VaultException e) {
      LOG.error("Vault Exception with httpStatusCode: {}", e.getHttpStatusCode());
      throw new Exception(e);
    }
  }

  public static void refreshServiceUtil(UUID configUUID, ObjectNode authConfig) throws Exception {
    VaultSecretEngineBase vaultSecretEngine =
        VaultSecretEngineBuilder.getVaultSecretEngine(authConfig);
    updateAuthConfigObj(configUUID, vaultSecretEngine, authConfig);
  }

  public static List<String> getMetadataFields() {
    return Arrays.asList(
        HashicorpVaultConfigParams.HC_VAULT_ADDRESS,
        HashicorpVaultConfigParams.HC_VAULT_ENGINE,
        HashicorpVaultConfigParams.HC_VAULT_MOUNT_PATH,
        HashicorpVaultConfigParams.HC_VAULT_KEY_NAME);
  }

  // Test encrypt and decrypt for Hashicorp KMS config with fake data
  public static void testEncryptDecrypt(
      VaultSecretEngineBase vaultSecretEngine, String engineKey, UUID configUUID) {
    // Test if able to encrypt.
    LOG.info("testEncryptDecrypt called for connfigUUID {}", configUUID);
    try {
      byte[] randomUniverseKey = new byte[32];
      SecureRandom.getInstanceStrong().nextBytes(randomUniverseKey);
      LOG.info("Testing encrypt");
      byte[] randomEncryptedBytes = vaultSecretEngine.encryptString(engineKey, randomUniverseKey);
      // Test if able to decrypt.
      LOG.info("Testing decrypt");
      byte[] decryptedBytes = vaultSecretEngine.decryptString(engineKey, randomEncryptedBytes);

      if (!Arrays.equals(decryptedBytes, randomUniverseKey)) {
        throw new RuntimeException(
            String.format(
                "Could not get decrypted bytes in Hashicorp KMS config '%s'.", configUUID));
      }
    } catch (Exception e) {
      final String errMsg =
          String.format(
              "Error occurred in testEncryptDecrypt key in Hashicorp KMS with config UUID '%s'.",
              configUUID);
      LOG.error(errMsg, e);
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }
  }
}
