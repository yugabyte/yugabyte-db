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
import com.typesafe.config.Config;
import com.yugabyte.yw.common.AppConfigHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.Util.UniverseDetailSubset;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.inject.StaticInjectorHolder;
import com.yugabyte.yw.common.kms.algorithms.SupportedAlgorithmInterface;
import com.yugabyte.yw.common.kms.services.EncryptionAtRestService;
import com.yugabyte.yw.forms.EncryptionAtRestConfig;
import com.yugabyte.yw.forms.EncryptionAtRestConfig.OpType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.KmsHistory;
import com.yugabyte.yw.models.KmsHistoryId;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import io.ebean.annotation.EnumValue;
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
import play.libs.Json;

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
      // Gets all the constructors with the runtime config object as a paramter.
      if (constructor.getParameterCount() == 1
          && RuntimeConfGetter.class.equals(constructor.getParameterTypes()[0])) {
        serviceConstructor = (Constructor<T>) constructor;
        break;
      }
    }

    return serviceConstructor;
  }

  public static ObjectNode getAuthConfig(UUID configUUID) {
    KmsConfig config = KmsConfig.getOrBadRequest(configUUID);
    return config.getAuthConfig();
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
    return StaticInjectorHolder.injector()
        .instanceOf(EncryptionAtRestUniverseKeyCache.class)
        .getCacheEntry(universeUUID, keyRef);
  }

  public static void setUniverseKeyCacheEntry(UUID universeUUID, byte[] keyRef, byte[] keyVal) {
    LOG.debug(
        String.format(
            "Setting universe key cache entry for universe %s and keyRef %s",
            universeUUID.toString(), Base64.getEncoder().encodeToString(keyRef)));
    StaticInjectorHolder.injector()
        .instanceOf(EncryptionAtRestUniverseKeyCache.class)
        .setCacheEntry(universeUUID, keyRef, keyVal);
  }

  public static void removeUniverseKeyCacheEntry(UUID universeUUID) {
    LOG.debug(
        String.format(
            "Removing universe key cache entry for universe %s", universeUUID.toString()));
    StaticInjectorHolder.injector()
        .instanceOf(EncryptionAtRestUniverseKeyCache.class)
        .removeCacheEntry(universeUUID);
  }

  public static void addKeyRef(UUID universeUUID, UUID configUUID, byte[] keyRef) {
    KmsHistory.createKmsHistory(
        configUUID,
        universeUUID,
        KmsHistoryId.TargetType.UNIVERSE_KEY,
        Base64.getEncoder().encodeToString(keyRef),
        Base64.getEncoder().encodeToString(keyRef));
  }

  public static void addKeyRefAndKeyId(
      UUID universeUUID, UUID configUUID, byte[] keyRef, String dbKeyId) {
    KmsHistory.createKmsHistory(
        configUUID,
        universeUUID,
        KmsHistoryId.TargetType.UNIVERSE_KEY,
        Base64.getEncoder().encodeToString(keyRef),
        dbKeyId);
  }

  public static KmsHistory createKmsHistory(
      UUID universeUUID, UUID configUUID, byte[] keyRef, int reEncryptionCount, String dbKeyId) {
    return KmsHistory.createKmsHistory(
        configUUID,
        universeUUID,
        KmsHistoryId.TargetType.UNIVERSE_KEY,
        Base64.getEncoder().encodeToString(keyRef),
        reEncryptionCount,
        dbKeyId);
  }

  public static boolean keyRefExists(UUID universeUUID, byte[] keyRef) {
    return KmsHistory.entryExists(
        universeUUID,
        Base64.getEncoder().encodeToString(keyRef),
        KmsHistoryId.TargetType.UNIVERSE_KEY);
  }

  public static boolean keyRefExists(UUID universeUUID, String keyRef) {
    return KmsHistory.entryExists(universeUUID, keyRef, KmsHistoryId.TargetType.UNIVERSE_KEY);
  }

  public static boolean dbKeyIdExists(UUID universeUUID, String dbKeyId) {
    return KmsHistory.dbKeyIdExists(universeUUID, dbKeyId, KmsHistoryId.TargetType.UNIVERSE_KEY);
  }

  public static KmsHistory getLatestKmsHistoryWithDbKeyId(UUID universeUUID, String dbKeyId) {
    return KmsHistory.getLatestKmsHistoryWithDbKeyId(
        universeUUID, dbKeyId, KmsHistoryId.TargetType.UNIVERSE_KEY);
  }

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

  public static int getNumUniverseKeys(UUID universeUUID) {
    int numRotations = 0;
    try {
      List<KmsHistory> keyRotations =
          KmsHistory.getAllUniverseKeysWithActiveMasterKey(universeUUID);
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

  public static String getPlainTextUniverseKey(KmsHistory kmsHistory) {
    return getPlainTextUniverseKey(
        kmsHistory.getUuid().targetUuid, kmsHistory.getConfigUuid(), kmsHistory.getUuid().keyRef);
  }

  public static String getPlainTextUniverseKey(UUID universeUUID, UUID configUUID, String keyRef) {
    KmsConfig kmsConfig = KmsConfig.getOrBadRequest(configUUID);
    byte[] encryptedUniverseKey = Base64.getDecoder().decode(keyRef);
    byte[] plainTextUniverseKey =
        kmsConfig
            .getKeyProvider()
            .getServiceInstance()
            .retrieveKey(universeUUID, configUUID, encryptedUniverseKey);
    return Base64.getEncoder().encodeToString(plainTextUniverseKey);
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

  public static KmsHistory getKeyRefConfig(UUID targetUUID, UUID configUUID, byte[] keyRef) {
    return KmsHistory.getKeyRefConfig(
        targetUUID,
        configUUID,
        Base64.getEncoder().encodeToString(keyRef),
        KmsHistoryId.TargetType.UNIVERSE_KEY);
  }

  public static KmsHistory getKmsHistory(UUID targetUUID, byte[] keyRef) {
    return KmsHistory.getKmsHistory(
        targetUUID,
        Base64.getEncoder().encodeToString(keyRef),
        KmsHistoryId.TargetType.UNIVERSE_KEY);
  }

  public static List<KmsHistory> getAllUniverseKeys(UUID universeUUID) {
    return KmsHistory.getAllUniverseKeysWithActiveMasterKey(universeUUID);
  }

  public static File getUniverseBackupKeysFile(String storageLocation) {
    Config appConfig = StaticInjectorHolder.injector().instanceOf(Config.class);
    File backupKeysDir = new File(AppConfigHelper.getStoragePath(), "backupKeys");

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

  public static void updateUniverseEARState(
      UUID universeUUID, EncryptionAtRestConfig encryptionAtRestConfig) {
    UniverseUpdater updater =
        new UniverseUpdater() {
          @Override
          public void run(Universe universe) {
            LOG.info(
                "Setting EAR status to {} for universe {} in the universe details.",
                encryptionAtRestConfig.opType.name(),
                universe.getUniverseUUID());
            UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
            universeDetails.encryptionAtRestConfig = encryptionAtRestConfig;
            universeDetails.encryptionAtRestConfig.encryptionAtRestEnabled =
                encryptionAtRestConfig.opType.equals(OpType.ENABLE);
            // Add the correct kms config UUID to universe details.
            UUID universeDetailsKmsConfigUUID =
                universe.getUniverseDetails().encryptionAtRestConfig.kmsConfigUUID;
            KmsHistory lastActiveKmsHistory = getActiveKey(universeUUID);
            if (universeDetailsKmsConfigUUID != null) {
              LOG.info(
                  "Setting kmsConfigUUID {} for universe {} in the "
                      + "universe details from previous universe details.",
                  universeDetailsKmsConfigUUID,
                  universeUUID);
              universeDetails.encryptionAtRestConfig.kmsConfigUUID = universeDetailsKmsConfigUUID;
            } else if (lastActiveKmsHistory != null) {
              // This is a failsafe mechanism if by any chance if was not populated before due to
              // some error.
              LOG.info(
                  "Setting kmsConfigUUID {} for universe {} in the "
                      + "universe details from last active key.",
                  lastActiveKmsHistory.getConfigUuid(),
                  universeUUID);
              universeDetails.encryptionAtRestConfig.kmsConfigUUID =
                  lastActiveKmsHistory.getConfigUuid();
            }
            universe.setUniverseDetails(universeDetails);
            LOG.info(
                "Successfully set EAR status {} for universe {} in the universe details.",
                encryptionAtRestConfig.opType.name(),
                universeUUID);
          }
        };
    Universe.saveDetails(universeUUID, updater, false);
  }

  public static void updateUniverseKMSConfigIfNotExists(UUID universeUUID, UUID kmsConfigUUID) {
    Universe universe = Universe.getOrBadRequest(universeUUID);
    if (universe.getUniverseDetails().encryptionAtRestConfig.kmsConfigUUID == null) {
      UniverseUpdater updater =
          new UniverseUpdater() {
            @Override
            public void run(Universe universe) {
              UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
              LOG.info(
                  "Found KMS config UUID {} for universe {} in the universe details.",
                  universeDetails.encryptionAtRestConfig.kmsConfigUUID,
                  universe.getUniverseUUID());
              // Add the kms config UUID to universe details.
              universeDetails.encryptionAtRestConfig.kmsConfigUUID = kmsConfigUUID;
              universe.setUniverseDetails(universeDetails);
              LOG.info(
                  "Successfully set KMS config {} for universe {} in the universe details.",
                  kmsConfigUUID,
                  universeUUID);
            }
          };
      Universe.saveDetails(universeUUID, updater, false);
    }
  }

  public static class BackupEntry {
    public byte[] keyRef;
    public KeyProvider keyProvider;
    public String dbKeyId;

    public BackupEntry(byte[] keyRef, KeyProvider keyProvider, String dbKeyId) {
      this.keyRef = keyRef;
      this.keyProvider = keyProvider;
      this.dbKeyId = dbKeyId;
    }

    public ObjectNode toJson() {
      return Json.newObject()
          .put("key_ref", Base64.getEncoder().encodeToString(keyRef))
          .put("key_provider", keyProvider.name())
          .put("db_key_id", dbKeyId);
    }

    @Override
    public String toString() {
      return this.toJson().toString();
    }
  }
}
