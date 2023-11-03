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

package com.yugabyte.yw.common.kms.util.hashicorpvault;

import com.bettercloud.vault.VaultException;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil.KeyType;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Base64;
import java.util.HashMap;
import java.util.Map;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Provides functionality to perform operations on HC Vault "transit" Secret Engine The secret
 * engine is used to encrypt / decrypt universe keys
 */
public class VaultTransit extends VaultSecretEngineBase {
  public static final Logger LOG = LoggerFactory.getLogger(VaultTransit.class);

  /**
   * @param vault
   * @param mPath
   * @param kType
   */
  public VaultTransit(VaultAccessor vault, String mPath, KeyType kType) throws Exception {
    super(vault, KMSEngineType.TRANSIT, mPath, kType);
    LOG.debug("Calling VaultTransit");

    checkForPermissions();
  }

  @Override
  public void checkForPermissions() {
    final String path = buildPath(VaultOperations.KEYS, "");
    String returnVal = "";
    try {
      returnVal = vAccessor.listAt(path);
    } catch (Exception e) {
      LOG.warn("List operation at {} failed. Reason: {}", path, e.toString());
    }
    LOG.debug("checkForPermissions called with path: {} and Returns {}", path, returnVal);
  }

  @Override
  public boolean createNewKeyWithEngine(String engineKeyName) throws VaultException {
    LOG.info("createNewKeyWithEngine called with key: {}", engineKeyName);

    final String path = buildPath(VaultOperations.KEYS, engineKeyName);
    String result = "";

    // if key exists, do noting, otherwise create a new key
    try {
      result = vAccessor.readAt(path, "");
      LOG.debug("createNewKeyWithEngine readAt returned: {}", result);
    } catch (Exception e) {
      // exception occured, create key
      result = "";
    }

    if ("{}".equals(result) || "".equals(result)) {
      // create transit key at path <HC_VAULT_MOUNT_PATH>/keys/<key_name>
      final Map<String, Object> mapToWrite = new HashMap<>();
      LOG.debug("createNewKeyWithEngine new key creation {}", engineKeyName);
      vAccessor.writeAt(path, mapToWrite, "");
      vAccessor.readAt(path, "latest_version");
      return true;
    }
    LOG.debug("createNewKeyWithEngine new key creation skipped");
    return false;
  }

  @Override
  public boolean deleteKey(String engineKey) throws VaultException {
    LOG.info("VaultTransit.deleteKey called: {}", engineKey);

    try {
      final Map<String, Object> mapToWrite = new HashMap<>();
      mapToWrite.put("deletion_allowed", "true");
      String path = buildPath(VaultOperations.KEYS, engineKey);

      vAccessor.writeAt(path + "/config", mapToWrite, "");
      vAccessor.deleteAt(path);

    } catch (Exception e) {
      LOG.error("Exception occured with attempting to delete key", e.toString());
      throw e;
    }
    return true;
  }

  /**
   * @param engineKey - key name of the key used
   * @param data - plaintext bytes of data {UniverseKey}
   * @return encryptedString
   */
  @Override
  public byte[] encryptString(String engineKey, byte[] data) throws VaultException {
    LOG.debug("encryptString called with key: of size {}", data.length);

    final Map<String, Object> mapToWrite = new HashMap<>();
    mapToWrite.put("plaintext", Base64.getEncoder().encodeToString(data));

    // LOG.debug( "DO_NOT_PRINT::Vault Write @ {} for key: {}", path,
    //  Base64.getEncoder().encodeToString(data));

    final String path = buildPath(VaultOperations.ENCRYPT, engineKey);
    String outData = vAccessor.writeAt(path, mapToWrite, "ciphertext");

    LOG.debug("Vault Write @ {} for key Return: {}", path, outData);

    return outData.getBytes(StandardCharsets.UTF_8);
  }

  /**
   * @param engineKey - key name of the key used
   * @param data - data returned by encrypt function (in bytes) - stored ciphertext of {UniverseKey}
   * @return decryptedString - in plaintext (post-base64decode) {UniverseKey}
   */
  @Override
  public byte[] decryptString(String engineKey, byte[] data) throws VaultException {
    LOG.info("VaultTransit.decryptString called");

    final Map<String, Object> mapToWrite = new HashMap<>();
    mapToWrite.put("ciphertext", new String(data, StandardCharsets.UTF_8));

    final String path = buildPath(VaultOperations.DECRYPT, engineKey);
    // LOG.debug("DO_NOT_PRINT::Decrypting at path: {} and text: {}",
    //    path, new String(data, StandardCharsets.UTF_8));

    String outData = vAccessor.writeAt(path, mapToWrite, "plaintext");
    byte[] decryptedUniverseKey = Base64.getDecoder().decode(outData);
    /*
    String outKey = new String(Base64.getDecoder().decode(outData));
    LOG.debug(
        "DO_NOT_PRINT::Decryption: outData({}): {}, outKey({}):{}",
        outData.length(), outData,
        decryptedUniverseKey.length, outKey);
    */

    return decryptedUniverseKey;
  }

  /**
   * @param engineKey - key name of the key used
   * @param dataList - List of data returned by encrypt function (in bytes) - stored ciphertext of
   *     {UniverseKey} list
   * @return reWrapedStrings - Map of new ciphertext {UniverseKey}
   */
  public Map<byte[], byte[]> reWrapString(String engineKey, ArrayList<byte[]> dataList)
      throws VaultException {

    LOG.debug("reWrapString called, entries : {}", dataList.size());
    final String path = buildPath(VaultOperations.RERWAP, engineKey);

    final Map<String, Object> mapToWrite = new HashMap<>();
    Map<byte[], byte[]> resultMap = new HashMap<byte[], byte[]>();

    for (byte[] bs : dataList) {
      String input = new String(bs, StandardCharsets.UTF_8);
      // LOG.debug("DO_NOT_PRINT::Processing input key: {}", input);

      // O(n) calls to writeAt, can we optimize to 1 call? using com.bettercloud.vault.rest;
      mapToWrite.clear();
      mapToWrite.put("ciphertext", input);
      String output = vAccessor.writeAt(path, mapToWrite, "ciphertext");

      // LOG.debug("DO_NOT_PRINT::String after re-wrap is : ", output);
      resultMap.put(bs, output.getBytes());
    }

    return resultMap;
  }
}
