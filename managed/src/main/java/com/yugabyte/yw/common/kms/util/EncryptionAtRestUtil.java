/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.kms.util;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.Util.UniverseDetailSubset;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.kms.algorithms.SupportedAlgorithmInterface;
import com.yugabyte.yw.common.kms.services.EncryptionAtRestService;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.KmsHistory;
import com.yugabyte.yw.models.KmsHistoryId;
import com.yugabyte.yw.models.Universe;
import java.io.File;
import java.lang.reflect.Constructor;
import java.util.Arrays;
import java.util.Base64;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.security.crypto.encrypt.Encryptors;
import org.springframework.security.crypto.encrypt.TextEncryptor;
import play.api.Play;
import play.libs.Json;
import io.ebean.annotation.EnumValue;

public class EncryptionAtRestUtil {
  protected static final Logger LOG = LoggerFactory.getLogger(EncryptionAtRestUtil.class);

  public enum KeyType {
    @EnumValue("CMK")
    CMK,
    @EnumValue("DATA_KEY")
    DATA_KEY;
  }

  private static final String BACKUP_KEYS_FILE_NAME = "backup_keys.json";

  // Retrieve the constructor from an EncryptionAtRestService implementation that takes no args
  public static <T extends EncryptionAtRestService<? extends SupportedAlgorithmInterface>>
      Constructor<T> getConstructor(Class<T> serviceClass) {
    Constructor<T> serviceConstructor = null;
    for (Constructor<?> constructor : serviceClass.getConstructors()) {
      if (constructor.getParameterCount() == 0) {
        serviceConstructor = (Constructor<T>) constructor;
        break;
      }
    }

    return serviceConstructor;
  }

  public static ObjectNode getAuthConfig(UUID configUUID) {
    KmsConfig config = KmsConfig.get(configUUID);
    return config.authConfig;
  }

  public static <N extends JsonNode> ObjectNode maskConfigData(
      UUID customerUUID, N config, KeyProvider keyProvider) {
    try {
      final ObjectMapper mapper = new ObjectMapper();
      final String salt = generateSalt(customerUUID, keyProvider);

      final TextEncryptor encryptor = Encryptors.delux(customerUUID.toString(), salt);
      final String encryptedConfig = encryptor.encrypt(mapper.writeValueAsString(config));
      return Json.newObject().put("encrypted", encryptedConfig);
    } catch (Exception e) {
      final String errMsg =
          String.format(
              "Could not encrypt %s KMS configuration for customer %s",
              keyProvider.name(), customerUUID.toString());
      LOG.error(errMsg, e);
      return null;
    }
  }

  public static JsonNode unmaskConfigData(
      UUID customerUUID, ObjectNode config, KeyProvider keyProvider) {
    if (config == null) return null;
    try {
      final ObjectMapper mapper = new ObjectMapper();
      final String encryptedConfig = config.get("encrypted").asText();
      final String salt = generateSalt(customerUUID, keyProvider);
      final TextEncryptor encryptor = Encryptors.delux(customerUUID.toString(), salt);
      final String decryptedConfig = encryptor.decrypt(encryptedConfig);
      return mapper.readValue(decryptedConfig, JsonNode.class);
    } catch (Exception e) {
      final String errMsg =
          String.format(
              "Could not decrypt %s KMS configuration for customer %s",
              keyProvider.name(), customerUUID.toString());
      LOG.error(errMsg, e);
      return null;
    }
  }

  public static String generateSalt(UUID customerUUID, KeyProvider keyProvider) {

    // fixed generateSalt to remove negative sign as string "HASHICORP" generates -ve integer.
    final String kpValue = String.valueOf(keyProvider.name().hashCode());
    final String saltBase = "%s%s";
    final String salt =
        String.format(saltBase, customerUUID.toString().replace("-", ""), kpValue.replace("-", ""));
    return salt.length() % 2 == 0 ? salt : salt + "0";
  }

  public static byte[] getUniverseKeyCacheEntry(UUID universeUUID, byte[] keyRef) {
    LOG.debug(
        String.format(
            "Retrieving universe key cache entry for universe %s and keyRef %s",
            universeUUID.toString(), Base64.getEncoder().encodeToString(keyRef)));
    return Play.current()
        .injector()
        .instanceOf(EncryptionAtRestUniverseKeyCache.class)
        .getCacheEntry(universeUUID, keyRef);
  }

  public static void setUniverseKeyCacheEntry(UUID universeUUID, byte[] keyRef, byte[] keyVal) {
    LOG.debug(
        String.format(
            "Setting universe key cache entry for universe %s and keyRef %s",
            universeUUID.toString(), Base64.getEncoder().encodeToString(keyRef)));
    Play.current()
        .injector()
        .instanceOf(EncryptionAtRestUniverseKeyCache.class)
        .setCacheEntry(universeUUID, keyRef, keyVal);
  }

  public static void removeUniverseKeyCacheEntry(UUID universeUUID) {
    LOG.debug(
        String.format(
            "Removing universe key cache entry for universe %s", universeUUID.toString()));
    Play.current()
        .injector()
        .instanceOf(EncryptionAtRestUniverseKeyCache.class)
        .removeCacheEntry(universeUUID);
  }

  public static void addKeyRef(UUID universeUUID, UUID configUUID, byte[] keyRef) {
    KmsHistory.createKmsHistory(
        configUUID,
        universeUUID,
        KmsHistoryId.TargetType.UNIVERSE_KEY,
        Base64.getEncoder().encodeToString(keyRef));
  }

  public static boolean keyRefExists(UUID universeUUID, byte[] keyRef) {
    return KmsHistory.entryExists(
        universeUUID,
        Base64.getEncoder().encodeToString(keyRef),
        KmsHistoryId.TargetType.UNIVERSE_KEY);
  }

  @Deprecated
  public static KmsHistory getActiveKey(UUID universeUUID) {
    KmsHistory activeHistory = null;
    try {
      activeHistory =
          KmsHistory.getActiveHistory(universeUUID, KmsHistoryId.TargetType.UNIVERSE_KEY);
    } catch (Exception e) {
      final String errMsg = "Could not get key ref";
      LOG.error(errMsg, e);
    }
    return activeHistory;
  }

  public static KmsHistory getLatestConfigKey(UUID universeUUID, UUID configUUID) {
    KmsHistory latestHistory = null;
    try {
      latestHistory =
          KmsHistory.getLatestConfigHistory(
              universeUUID, configUUID, KmsHistoryId.TargetType.UNIVERSE_KEY);
    } catch (Exception e) {
      final String errMsg = "Could not get key ref";
      LOG.error(errMsg, e);
    }
    return latestHistory;
  }

  public static int removeKeyRotationHistory(UUID universeUUID, UUID configUUID) {
    // Remove key ref history for the universe
    int keyCount =
        KmsHistory.deleteAllConfigTargetKeyRefs(
            configUUID, universeUUID, KmsHistoryId.TargetType.UNIVERSE_KEY);
    // Remove in-memory key ref -> key val cache entry, if it exists
    EncryptionAtRestUtil.removeUniverseKeyCacheEntry(universeUUID);
    return keyCount;
  }

  public static boolean configInUse(UUID configUUID) {
    return KmsHistory.configHasHistory(configUUID, KmsHistoryId.TargetType.UNIVERSE_KEY);
  }

  public static List<UniverseDetailSubset> getUniverses(UUID configUUID) {
    Set<Universe> universes =
        KmsHistory.getUniverses(configUUID, KmsHistoryId.TargetType.UNIVERSE_KEY);
    return Util.getUniverseDetails(universes);
  }

  public static int getNumKeyRotations(UUID universeUUID) {
    return getNumKeyRotations(universeUUID, null);
  }

  public static int getNumKeyRotations(UUID universeUUID, UUID configUUID) {
    int numRotations = 0;

    try {
      List<KmsHistory> keyRotations =
          configUUID == null
              ? KmsHistory.getAllTargetKeyRefs(universeUUID, KmsHistoryId.TargetType.UNIVERSE_KEY)
              : KmsHistory.getAllConfigTargetKeyRefs(
                  configUUID, universeUUID, KmsHistoryId.TargetType.UNIVERSE_KEY);
      numRotations = keyRotations.size();
    } catch (Exception e) {
      String errMsg =
          String.format(
              "Error attempting to retrieve the number of key rotations " + "universe %s",
              universeUUID.toString());
      LOG.error(errMsg, e);
    }
    return numRotations;
  }

  public static void activateKeyRef(UUID universeUUID, UUID configUUID, byte[] keyRef) {
    KmsHistory.activateKeyRef(
        universeUUID,
        configUUID,
        KmsHistoryId.TargetType.UNIVERSE_KEY,
        Base64.getEncoder().encodeToString(keyRef));
  }

  public static KmsHistory getActiveKeyOrBadRequest(UUID universeUUID) {
    KmsHistory activeKey = getActiveKey(universeUUID);
    if (activeKey == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Could not retrieve ActiveKey");
    }
    return activeKey;
  }

  public static List<KmsHistory> getAllUniverseKeys(UUID universeUUID) {
    return KmsHistory.getAllTargetKeyRefs(universeUUID, KmsHistoryId.TargetType.UNIVERSE_KEY);
  }

  public static File getUniverseBackupKeysFile(String storageLocation) {
    play.Configuration appConfig = Play.current().injector().instanceOf(play.Configuration.class);
    File backupKeysDir = new File(appConfig.getString("yb.storage.path"), "backupKeys");

    String[] dirParts = storageLocation.split("/");

    File storageLocationDir =
        new File(
            backupKeysDir.getAbsoluteFile(),
            String.join(
                "/",
                Arrays.asList(
                    Arrays.copyOfRange(dirParts, dirParts.length - 3, dirParts.length - 1))));

    return new File(storageLocationDir.getAbsolutePath(), BACKUP_KEYS_FILE_NAME);
  }

  public static class BackupEntry {
    public byte[] keyRef;

    public KeyProvider keyProvider;

    public BackupEntry(byte[] keyRef, KeyProvider keyProvider) {
      this.keyRef = keyRef;
      this.keyProvider = keyProvider;
    }

    public ObjectNode toJson() {
      return Json.newObject()
          .put("key_ref", Base64.getEncoder().encodeToString(keyRef))
          .put("key_provider", keyProvider.name());
    }

    @Override
    public String toString() {
      return this.toJson().toString();
    }
  }
}
