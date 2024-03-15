/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.kms;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableMap;
import com.google.common.net.HostAndPort;
import com.google.inject.Inject;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.kms.algorithms.SupportedAlgorithmInterface;
import com.yugabyte.yw.common.kms.services.EncryptionAtRestService;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil.BackupEntry;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.common.utils.FileUtils;
import com.yugabyte.yw.forms.EncryptionAtRestConfig;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.KmsHistory;
import com.yugabyte.yw.models.Universe;
import java.io.File;
import java.lang.reflect.Constructor;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Base64;
import java.util.Collections;
import java.util.List;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Singleton;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.YBClient;
import org.yb.util.Pair;
import play.libs.Json;

@Singleton
public class EncryptionAtRestManager {
  public static final Logger LOG = LoggerFactory.getLogger(EncryptionAtRestManager.class);
  private final RuntimeConfGetter confGetter;
  // How long to wait for universe key to be set in memory
  private static final int KEY_IN_MEMORY_TIMEOUT = 500;

  @Inject
  public EncryptionAtRestManager(RuntimeConfGetter confGetter) {
    this.confGetter = confGetter;
  }

  public enum RestoreKeyResult {
    RESTORE_SKIPPED,
    RESTORE_FAILED,
    RESTORE_SUCCEEDED
  }

  public <T extends EncryptionAtRestService<? extends SupportedAlgorithmInterface>>
      T getServiceInstance(String keyProvider) {
    return getServiceInstance(keyProvider, false);
  }

  public <T extends EncryptionAtRestService<? extends SupportedAlgorithmInterface>>
      T getServiceInstance(String keyProvider, boolean forceNewInstance) {
    KeyProvider serviceProvider = null;
    T serviceInstance;
    try {
      for (KeyProvider providerImpl : KeyProvider.values()) {
        if (providerImpl.name().equals(keyProvider)) {
          serviceProvider = providerImpl;
          break;
        }
      }
      if (serviceProvider == null) {
        final String errMsg =
            String.format("Encryption service provider %s is not supported", keyProvider);
        LOG.error(errMsg);
        throw new IllegalArgumentException(errMsg);
      }

      serviceInstance = serviceProvider.getServiceInstance();
      if (forceNewInstance || serviceInstance == null) {
        final Class<T> serviceClass = serviceProvider.getProviderService();

        if (serviceClass == null) {
          final String errMsg =
              String.format(
                  "Encryption service provider %s has not been implemented yet", keyProvider);
          LOG.error(errMsg);
          throw new IllegalArgumentException(errMsg);
        }

        final Constructor<T> serviceConstructor = EncryptionAtRestUtil.getConstructor(serviceClass);

        if (serviceConstructor == null) {
          final String errMsg =
              String.format(
                  "No suitable public constructor found for service provider %s", keyProvider);
          LOG.error(errMsg);
          throw new InstantiationException(errMsg);
        }

        // Not able to inject this at the lower level class such as AWSEarService.java bcs it is
        // instantiated separately. Hence we have to pass it down from here.
        serviceInstance = serviceConstructor.newInstance(confGetter);
        serviceProvider.setServiceInstance(serviceInstance);
      }
    } catch (Exception e) {
      final String errMsg =
          String.format(
              "Error occurred attempting to retrieve encryption key service for "
                  + "key provider %s",
              keyProvider);
      LOG.error(errMsg, e);
      serviceInstance = null;
    }
    return serviceInstance;
  }

  public byte[] reEncryptUniverseKey(
      UUID universeUUID,
      UUID oldKmsConfigUUID,
      byte[] oldEncryptedUniverseKey,
      UUID newKmsConfigUUID) {
    // Decrypt old encrypted universe key with old KMS config.
    EncryptionAtRestService<? extends SupportedAlgorithmInterface> oldKeyService =
        getServiceInstance(KmsConfig.getOrBadRequest(oldKmsConfigUUID).getKeyProvider().name());
    byte[] universeKey =
        oldKeyService.retrieveKey(universeUUID, oldKmsConfigUUID, oldEncryptedUniverseKey);

    // Encrypt the old universe key with the new KMS config.
    EncryptionAtRestService<? extends SupportedAlgorithmInterface> newKeyService =
        getServiceInstance(KmsConfig.getOrBadRequest(newKmsConfigUUID).getKeyProvider().name());
    byte[] newEncryptedUniverseKey =
        newKeyService.encryptKeyWithService(newKmsConfigUUID, universeKey);
    if (newEncryptedUniverseKey == null || newEncryptedUniverseKey.length == 0) {
      throw new RuntimeException(
          String.format(
              "Could not re-encrypt universe key from configUUID '%s' to configUUID '%s'.",
              oldKmsConfigUUID.toString(), newKmsConfigUUID.toString()));
    }
    return newEncryptedUniverseKey;
  }

  public void reEncryptActiveUniverseKeys(UUID universeUUID, UUID newKmsConfigUUID) {
    List<KmsHistory> allActiveUniverseKeys = EncryptionAtRestUtil.getAllUniverseKeys(universeUUID);
    // Reverse because the above function returns in desc order of timestamp.
    // We want to add the records into the db table in asc order of timestamp, as it was before.
    Collections.reverse(allActiveUniverseKeys);
    int newReEncryptionCount = KmsHistory.getLatestReEncryptionCount(universeUUID) + 1;
    List<KmsHistory> reEncryptedKmsHistoryList = new ArrayList<>();
    byte[] newKeyRefToBeActive = null;

    for (KmsHistory kmsHistory : allActiveUniverseKeys) {
      byte[] newEncryptedUniverseKey =
          reEncryptUniverseKey(
              universeUUID,
              kmsHistory.getConfigUuid(),
              Base64.getDecoder().decode(kmsHistory.getUuid().keyRef),
              newKmsConfigUUID);

      // If all goes well, add records to reEncryptedKmsHistoryList.
      reEncryptedKmsHistoryList.add(
          EncryptionAtRestUtil.createKmsHistory(
              universeUUID,
              newKmsConfigUUID,
              newEncryptedUniverseKey,
              newReEncryptionCount,
              kmsHistory.dbKeyId));
      LOG.info(
          "Re-encrypted 1 universe key from configUUID '{}' to configUUID '{}'.",
          kmsHistory.getConfigUuid(),
          newKmsConfigUUID);

      // Find the kmsHistory to be activated, with new re-encrypted key ref.
      if (kmsHistory.isActive()) {
        newKeyRefToBeActive = newEncryptedUniverseKey;
      }
    }
    // Save all the records in reEncryptedKmsHistoryList in a single transaction.
    KmsHistory.addAllKmsHistory(reEncryptedKmsHistoryList);

    // Activate the original universe key which was re-encrypted.
    EncryptionAtRestUtil.activateKeyRef(universeUUID, newKmsConfigUUID, newKeyRefToBeActive);
    LOG.info("Re-encrypted all active universe keys to new KMS config '{}'.", newKmsConfigUUID);
  }

  public <T extends EncryptionAtRestService<? extends SupportedAlgorithmInterface>>
      byte[] generateUniverseKey(
          UUID configUUID, UUID universeUUID, EncryptionAtRestConfig config) {
    T keyService;
    KmsConfig kmsConfig;
    byte[] universeKeyRef = null;
    try {
      kmsConfig = KmsConfig.getOrBadRequest(configUUID);
      keyService = getServiceInstance(kmsConfig.getKeyProvider().name());
      KmsHistory activeKmsHistory = EncryptionAtRestUtil.getActiveKey(universeUUID);
      if (EncryptionAtRestUtil.getNumUniverseKeys(universeUUID) == 0) {
        // Universe key creation when no universe key exists on the universe.
        LOG.info(
            String.format("Creating universe key for universe '%s'.", universeUUID.toString()));
        universeKeyRef = keyService.createKey(universeUUID, configUUID, config);
        if (universeKeyRef != null && universeKeyRef.length > 0) {
          EncryptionAtRestUtil.addKeyRef(universeUUID, configUUID, universeKeyRef);
        }
      } else if (configUUID != null && configUUID.equals(activeKmsHistory.getConfigUuid())) {
        // Universe key rotation if the given KMS config equals the active one.
        LOG.info(
            String.format("Rotating universe key for universe '%s'.", universeUUID.toString()));
        universeKeyRef = keyService.rotateKey(universeUUID, configUUID, config);
        if (universeKeyRef != null && universeKeyRef.length > 0) {
          EncryptionAtRestUtil.addKeyRef(universeUUID, configUUID, universeKeyRef);
        }
      }
    } catch (Exception e) {
      String errMsg =
          String.format(
              "Error attempting to generate universe key for universe %s", universeUUID.toString());
      LOG.error(errMsg, e);
    }
    return universeKeyRef;
  }

  public <T extends EncryptionAtRestService<? extends SupportedAlgorithmInterface>>
      byte[] getUniverseKey(UUID universeUUID, UUID configUUID, byte[] keyRef) {
    byte[] keyVal = null;
    T keyService;
    try {
      keyService = getServiceInstance(KmsConfig.get(configUUID).getKeyProvider().name());
      keyVal = keyService.retrieveKey(universeUUID, configUUID, keyRef);
    } catch (Exception e) {
      String errMsg =
          String.format(
              "Error attempting to retrieve the current universe key for universe %s "
                  + "with config %s",
              universeUUID.toString(), configUUID.toString());
      LOG.error(errMsg, e);
      throw new RuntimeException(errMsg, e);
    }
    return keyVal;
  }

  public void cleanupEncryptionAtRest(UUID customerUUID, UUID universeUUID) {
    // this calls for all configs for provider X universe, regardless of config actually used
    // behavior is handled in cleanup call
    KmsConfig.listKMSConfigs(customerUUID)
        .forEach(
            config ->
                getServiceInstance(config.getKeyProvider().name())
                    .cleanup(universeUUID, config.getConfigUUID()));
  }

  // Build up list of objects to backup
  public List<ObjectNode> getUniverseKeyRefsForBackup(UUID universeUUID) {
    return EncryptionAtRestUtil.getAllUniverseKeys(universeUUID).stream()
        .map(
            history -> {
              BackupEntry entry = null;
              try {
                KmsConfig config = KmsConfig.get(history.getConfigUuid());
                entry = getServiceInstance(config.getKeyProvider().name()).getBackupEntry(history);
              } catch (Exception e) {
                String errMsg =
                    String.format(
                        "Error backing up universe key %s for universe %s",
                        history.getUuid().keyRef, universeUUID.toString());
                LOG.error(errMsg, e);
              }
              return entry;
            })
        .filter(Objects::nonNull)
        .map(BackupEntry::toJson)
        .collect(Collectors.toList());
  }

  public void addUniverseKeyMasterKeyMetadata(
      ObjectNode backup,
      List<ObjectNode> universeKeyRefs,
      ArrayNode universeKeys,
      UUID universeUUID) {
    // Add all the universe key history.
    universeKeyRefs.forEach(universeKeys::add);
    // Add the master key metadata.
    Set<UUID> distinctKmsConfigUUIDs = KmsHistory.getDistinctKmsConfigUUIDs(universeUUID);
    if (distinctKmsConfigUUIDs.size() == 1) {
      KmsConfig kmsConfig = KmsConfig.get(distinctKmsConfigUUIDs.iterator().next());
      backup.set(
          "master_key_metadata",
          kmsConfig
              .getKeyProvider()
              .getServiceInstance()
              .getKeyMetadata(kmsConfig.getConfigUUID()));
    } else {
      LOG.debug(
          "Found {} master keys on universe '{}''. Not adding them to backup metadata: {}.",
          distinctKmsConfigUUIDs.size(),
          universeUUID,
          distinctKmsConfigUUIDs);
    }
  }

  // Backup universe key metadata to file
  public void backupUniverseKeyHistory(UUID universeUUID, String storageLocation) throws Exception {
    ObjectNode backup = Json.newObject();
    ArrayNode universeKeys = backup.putArray("universe_keys");
    List<ObjectNode> universeKeyRefs = getUniverseKeyRefsForBackup(universeUUID);
    if (universeKeyRefs.size() > 0) {
      // Add the universe key details and master key details.
      addUniverseKeyMasterKeyMetadata(backup, universeKeyRefs, universeKeys, universeUUID);
      // Write the backup metadata object to file.
      ObjectMapper mapper = new ObjectMapper();
      String backupContent = mapper.writeValueAsString(backup);
      File backupKeysFile = EncryptionAtRestUtil.getUniverseBackupKeysFile(storageLocation);
      File backupKeysDir = backupKeysFile.getParentFile();

      // Skip writing key content to file since it already exists and has been written to.
      if (backupKeysFile.exists() && backupKeysFile.isFile()) {
        return;
      }

      if ((!backupKeysDir.isDirectory() && !backupKeysDir.mkdirs())
          || !backupKeysFile.createNewFile()) {
        throw new RuntimeException("Error creating backup encryption key file!");
      }

      FileUtils.writeStringToFile(backupKeysFile, backupContent);
    }
  }

  /**
   * Function to get universe keys history as ObjectNode, for use in YB-Controller extended args.
   *
   * @param universeUUID
   * @return ObjectNode consisting of universe key history.
   * @throws Exception
   */
  public ObjectNode backupUniverseKeyHistory(UUID universeUUID) {
    ObjectNode backup = Json.newObject();
    ArrayNode universeKeys = backup.putArray("universe_keys");
    List<ObjectNode> universeKeyRefs = getUniverseKeyRefsForBackup(universeUUID);
    if (universeKeyRefs.size() > 0) {
      // Add the universe key details and master key details.
      addUniverseKeyMasterKeyMetadata(backup, universeKeyRefs, universeKeys, universeUUID);
      return backup;
    }
    return null;
  }

  public void sendKeyToMasters(
      YBClientService ybService, UUID universeUUID, UUID kmsConfigUUID, byte[] keyRef) {
    Universe universe = Universe.getOrBadRequest(universeUUID);
    String hostPorts = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();
    YBClient client = null;
    String dbKeyId =
        EncryptionAtRestUtil.getKeyRefConfig(universeUUID, kmsConfigUUID, keyRef).dbKeyId;
    try {
      byte[] keyVal = getUniverseKey(universeUUID, kmsConfigUUID, keyRef);
      client = ybService.getClient(hostPorts, certificate);
      List<HostAndPort> masterAddrs =
          Arrays.stream(hostPorts.split(","))
              .map(addr -> HostAndPort.fromString(addr))
              .collect(Collectors.toList());
      for (HostAndPort hp : masterAddrs) {
        client.addUniverseKeys(ImmutableMap.of(dbKeyId, keyVal), hp);
        LOG.info(
            "Sent universe key to universe '{}' and DB node '{}' with key ID: '{}'.",
            universe.getUniverseUUID(),
            hp,
            dbKeyId);
      }
      for (HostAndPort hp : masterAddrs) {
        if (!client.waitForMasterHasUniverseKeyInMemory(KEY_IN_MEMORY_TIMEOUT, dbKeyId, hp)) {
          throw new RuntimeException(
              "Timeout occurred waiting for universe encryption key to be set in memory");
        }
      }

      // Since a universe key only gets written to the universe key registry during a
      // change encryption info request, we need to temporarily enable encryption with each
      // key to ensure it is written to the registry to be used to decrypt restored files
      client.enableEncryptionAtRestInMemory(dbKeyId);
      Pair<Boolean, String> isEncryptionEnabled = client.isEncryptionEnabled();
      if (!isEncryptionEnabled.getFirst() || !isEncryptionEnabled.getSecond().equals(dbKeyId)) {
        throw new RuntimeException("Master did not respond that key was enabled");
      }

      universe.incrementVersion();

      // Activate keyRef so that if the universe is not enabled,
      // the last keyRef will always be in-memory due to the setkey task
      // which will mean the cluster will always be able to decrypt the
      // universe key registry which we need to be the case.
      EncryptionAtRestUtil.activateKeyRef(universeUUID, kmsConfigUUID, keyRef);
    } catch (Exception e) {
      String errMsg = "Error sending universe key to master.";
      LOG.error(errMsg, e);
      throw new RuntimeException(errMsg, e);
    } finally {
      ybService.closeClient(client, hostPorts);
    }
  }

  public void restoreSingleUniverseKey(
      YBClientService ybService, UUID universeUUID, UUID kmsConfigUUID, JsonNode backupEntry) {
    final byte[] universeKeyRef = Base64.getDecoder().decode(backupEntry.get("key_ref").asText());

    if (universeKeyRef != null) {
      String validDbKeyId;
      if (backupEntry.has("db_key_id")) {
        // For new backups after this change.
        validDbKeyId = backupEntry.get("db_key_id").asText();
      } else {
        // For old backups without db_key_id in the metadata.
        validDbKeyId = Base64.getEncoder().encodeToString(universeKeyRef);
      }

      // Get the service account object to verify 2 cases ahead.
      String keyProviderString = KmsConfig.getOrBadRequest(kmsConfigUUID).getKeyProvider().name();
      EncryptionAtRestService<? extends SupportedAlgorithmInterface> keyService =
          getServiceInstance(keyProviderString);
      UUID validKmsConfigUUID;

      if (keyService.verifyKmsConfigAndKeyRef(universeUUID, kmsConfigUUID, universeKeyRef)) {
        // Case 1: When the given KMS config UUID matches the key ref.
        validKmsConfigUUID = kmsConfigUUID;
      } else {
        // Case 2: When the given KMS config UUID does not match the key ref.
        // Iterate through all KMS configs and see which can decrypt the key ref.
        validKmsConfigUUID =
            findKmsConfigFromKeyRefOrNull(
                universeUUID,
                KeyProvider.valueOf(backupEntry.get("key_provider").asText()),
                universeKeyRef);
        if (validKmsConfigUUID == null) {
          // Case 3: When none of the KMS configs on the platform are able to decrypt the key ref.
          throw new PlatformServiceException(
              INTERNAL_SERVER_ERROR,
              String.format(
                  "Failed to restore backup to universe UUID '%s'. "
                      + "Tried all KMS configs on platform.",
                  universeUUID));
        }
      }

      // Get the valid and active KMS history.
      KmsHistory validKmsHistory;
      KmsHistory activeKmsHistory = EncryptionAtRestUtil.getActiveKey(universeUUID);

      // Case when EAR was disabled on target universe.
      if (activeKmsHistory == null) {
        keyService.restoreBackupEntry(
            universeUUID, validKmsConfigUUID, universeKeyRef, validDbKeyId);
        validKmsHistory =
            EncryptionAtRestUtil.getKeyRefConfig(universeUUID, validKmsConfigUUID, universeKeyRef);
      } else {
        // Case when EAR was ever enabled before.
        // Re-encrypt the key_ref with the active KMS config on the target universe.
        UUID activeKmsConfigUUID = activeKmsHistory.getConfigUuid();
        byte[] newEncryptedUniverseKey =
            reEncryptUniverseKey(
                universeUUID, validKmsConfigUUID, universeKeyRef, activeKmsConfigUUID);
        if (EncryptionAtRestUtil.dbKeyIdExists(universeUUID, validDbKeyId)) {
          validKmsHistory =
              EncryptionAtRestUtil.getLatestKmsHistoryWithDbKeyId(universeUUID, validDbKeyId);
        } else {
          keyService.restoreBackupEntry(
              universeUUID, activeKmsConfigUUID, newEncryptedUniverseKey, validDbKeyId);
          validKmsHistory =
              EncryptionAtRestUtil.getKeyRefConfig(
                  universeUUID, activeKmsConfigUUID, newEncryptedUniverseKey);
        }
      }

      LOG.info(
          "Given KMS config = '{}'. KMS config '{}' works to restore key ref to universe '{}'.",
          kmsConfigUUID,
          validKmsConfigUUID,
          universeUUID);
      sendKeyToMasters(
          ybService,
          validKmsHistory.getUuid().targetUuid,
          validKmsHistory.getConfigUuid(),
          Base64.getDecoder().decode(validKmsHistory.getUuid().keyRef));
    }
  }

  // Restore universe keys from metadata file
  public RestoreKeyResult restoreUniverseKeyHistory(
      YBClientService ybService, UUID universeUUID, UUID kmsConfigUUID, String storageLocation) {
    RestoreKeyResult result = RestoreKeyResult.RESTORE_FAILED;
    try {
      File backupKeysFile = EncryptionAtRestUtil.getUniverseBackupKeysFile(storageLocation);
      File backupKeysDir = backupKeysFile.getParentFile();

      // Nothing to do if no key metadata file exists
      if (!backupKeysDir.isDirectory() || !backupKeysDir.exists() || !backupKeysFile.exists()) {
        result = RestoreKeyResult.RESTORE_SKIPPED;
        return result;
      }

      byte[] backupContents = Files.readAllBytes(backupKeysFile.toPath());

      // Nothing to do if the key metadata file is empty
      if (backupContents.length == 0) {
        result = RestoreKeyResult.RESTORE_SKIPPED;
        return result;
      }

      ObjectMapper mapper = new ObjectMapper();
      JsonNode backup = mapper.readTree(backupContents);
      JsonNode universeKeys = backup.get("universe_keys");
      if (universeKeys != null && universeKeys.isArray()) {

        // Cleanup encrypted key metadata file since it is no longer needed
        backupKeysFile.delete();
        result = restoreUniverseKeyHistory(ybService, universeUUID, kmsConfigUUID, universeKeys);
      }
    } catch (Exception e) {
      String errMsg = "Error occurred restoring universe key history.";
      LOG.error(errMsg, e);
      throw new RuntimeException(errMsg, e);
    }

    return result;
  }

  public RestoreKeyResult restoreUniverseKeyHistory(
      YBClientService ybService, UUID universeUUID, UUID kmsConfigUUID, JsonNode universeKeys) {
    RestoreKeyResult result = RestoreKeyResult.RESTORE_FAILED;
    try {
      if (universeKeys != null && universeKeys.isArray()) {
        // Have to traverse arraynode in reverse, i.e. ascending order of keys created.
        // Reverse because during backup, we save it in desc order (KmsHistory.getAllTargetKeyRefs)
        // Found no other elegant solution without changing too much code.
        for (int i = universeKeys.size() - 1; i >= 0; --i) {
          restoreSingleUniverseKey(ybService, universeUUID, kmsConfigUUID, universeKeys.get(i));
        }
        LOG.info("Restore universe keys succeeded!");
        result = RestoreKeyResult.RESTORE_SUCCEEDED;
      }
    } catch (Exception e) {
      String errMsg = "Error occurred restoring universe key history.";
      LOG.error(errMsg, e);
      throw new RuntimeException(errMsg, e);
    }
    return result;
  }

  /**
   * Find the KMS config UUID that can decrypt a given key ref (encrypted universe key). Iterates
   * through all KMS configs on platform to verify.
   *
   * @param keyProvider the KMS provider.
   * @param keyRef the encrypted universe key.
   * @return the matching KMS config UUID if any, else null.
   */
  public UUID findKmsConfigFromKeyRefOrNull(
      UUID universeUUID, KeyProvider keyProvider, byte[] keyRef) {
    UUID kmsConfigUUID = null;
    List<KmsConfig> allKmsConfigs = KmsConfig.listKMSProviderConfigs(keyProvider);
    EncryptionAtRestService<? extends SupportedAlgorithmInterface> keyService =
        getServiceInstance(keyProvider.name());
    for (KmsConfig kmsConfig : allKmsConfigs) {
      if (keyService.verifyKmsConfigAndKeyRef(universeUUID, kmsConfig.getConfigUUID(), keyRef)) {
        kmsConfigUUID = kmsConfig.getConfigUUID();
        break;
      }
    }
    return kmsConfigUUID;
  }
}
