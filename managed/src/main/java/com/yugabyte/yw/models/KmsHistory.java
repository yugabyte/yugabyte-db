/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.models;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;

import com.fasterxml.jackson.annotation.JsonIgnore;
import io.ebean.DB;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.SqlUpdate;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Date;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import javax.persistence.Column;
import javax.persistence.EmbeddedId;
import javax.persistence.Entity;
import javax.persistence.Temporal;
import javax.persistence.TemporalType;
import lombok.Getter;
import lombok.Setter;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.validation.Constraints;
import play.libs.Json;

@Entity
// @IdClass(KmsHistoryId.class)
@ApiModel(description = "KMS history")
@Getter
@Setter
public class KmsHistory extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(KmsHistory.class);

  public static final int SCHEMA_VERSION = 1;

  @EmbeddedId
  @ApiModelProperty(value = "KMS history UUID", accessMode = READ_ONLY)
  private KmsHistoryId uuid;

  @Constraints.Required
  @Temporal(TemporalType.TIMESTAMP)
  @Column(nullable = false)
  @ApiModelProperty(value = "Timestamp of KMS history", accessMode = READ_ONLY)
  private Date timestamp;

  @Constraints.Required
  @Column(nullable = false)
  @ApiModelProperty(value = "Version of KMS history", accessMode = READ_ONLY)
  private int version;

  @Constraints.Required
  @Column(nullable = false)
  @ApiModelProperty(value = "KMS configuration UUID", accessMode = READ_ONLY)
  private UUID configUuid;

  @Constraints.Required
  @Column(nullable = false)
  @ApiModelProperty(value = "True if the KMS is active", accessMode = READ_ONLY)
  private boolean active;

  @Constraints.Required
  @Column(name = "db_key_id")
  @ApiModelProperty(
      value = "The key ref of the initial re-encrypted universe key",
      accessMode = READ_ONLY)
  public String dbKeyId;

  public static final Finder<KmsHistoryId, KmsHistory> find =
      new Finder<KmsHistoryId, KmsHistory>(KmsHistory.class) {};

  public static int getLatestReEncryptionCount(UUID targetUUID) {
    // If there is no universe with universe key, return 0.
    int latestReEncryptionCount =
        KmsHistory.find
            .query()
            .where()
            .eq("target_uuid", targetUUID)
            .eq("type", KmsHistoryId.TargetType.UNIVERSE_KEY)
            .findList()
            .stream()
            .mapToInt(kmsHistory -> kmsHistory.uuid.getReEncryptionCount())
            .max()
            .orElse(0);
    return latestReEncryptionCount;
  }

  public static KmsHistory createKmsHistory(
      UUID configUUID,
      UUID targetUUID,
      KmsHistoryId.TargetType targetType,
      String keyRef,
      String dbKeyId) {
    return createKmsHistory(
        configUUID,
        targetUUID,
        targetType,
        keyRef,
        getLatestReEncryptionCount(targetUUID),
        dbKeyId,
        true);
  }

  public static KmsHistory createKmsHistory(
      UUID configUUID,
      UUID targetUUID,
      KmsHistoryId.TargetType targetType,
      String keyRef,
      int reEncryptionCount,
      String dbKeyId) {
    return createKmsHistory(
        configUUID, targetUUID, targetType, keyRef, reEncryptionCount, dbKeyId, false);
  }

  public static KmsHistory createKmsHistory(
      UUID configUUID,
      UUID targetUUID,
      KmsHistoryId.TargetType targetType,
      String keyRef,
      int reEncryptionCount,
      String dbKeyId,
      Boolean saveToDb) {
    KmsHistory keyHistory = new KmsHistory();
    keyHistory.uuid = new KmsHistoryId(keyRef, targetUUID, targetType, reEncryptionCount);
    keyHistory.setTimestamp(new Date());
    keyHistory.setVersion(SCHEMA_VERSION);
    keyHistory.setActive(false);
    keyHistory.setConfigUuid(configUUID);
    keyHistory.dbKeyId = dbKeyId;
    if (saveToDb) {
      keyHistory.save();
    }
    return keyHistory;
  }

  public static void addAllKmsHistory(List<KmsHistory> kmsHistoryList) {
    DB.beginTransaction();
    try {
      DB.saveAll(kmsHistoryList);
      DB.commitTransaction();
    } finally {
      DB.endTransaction();
    }
  }

  @JsonIgnore
  public static void updateKeyRefStatus(
      UUID targetUUID,
      UUID confidUUID,
      KmsHistoryId.TargetType targetType,
      String keyRef,
      boolean active) {
    String sql =
        "UPDATE kms_history"
            + " SET active = ?"
            + " WHERE target_uuid = ?"
            + " AND config_uuid = ?"
            + " AND type = ?"
            + " AND key_ref = ?";
    SqlUpdate update =
        DB.sqlUpdate(sql)
            .setParameter(1, active)
            .setParameter(2, targetUUID)
            .setParameter(3, confidUUID)
            .setParameter(4, targetType)
            .setParameter(5, keyRef);
    int rows = update.execute();
    LOG.debug(
        String.format("setKeyRefStatus kms_history: Updating active status for %d rows", rows));
  }

  public static void activateKeyRef(
      UUID targetUUID, UUID configUUID, KmsHistoryId.TargetType targetType, String keyRef) {
    DB.beginTransaction();
    try {
      KmsHistory currentlyActiveKeyRef = KmsHistory.getActiveHistory(targetUUID, targetType);
      if (currentlyActiveKeyRef != null) {
        updateKeyRefStatus(
            targetUUID,
            currentlyActiveKeyRef.getConfigUuid(),
            targetType,
            currentlyActiveKeyRef.getUuid().keyRef,
            false);
      }
      KmsHistory toBeActiveKeyRef =
          KmsHistory.getKeyRefConfig(targetUUID, configUUID, keyRef, targetType);
      if (toBeActiveKeyRef != null) {
        updateKeyRefStatus(targetUUID, toBeActiveKeyRef.getConfigUuid(), targetType, keyRef, true);
      }
      DB.commitTransaction();
    } finally {
      DB.endTransaction();
    }
  }

  public static List<KmsHistory> getAllConfigTargetKeyRefs(
      UUID configUUID, UUID targetUUID, KmsHistoryId.TargetType type) {
    return KmsHistory.find
        .query()
        .where()
        .eq("config_uuid", configUUID)
        .eq("target_uuid", targetUUID)
        .eq("type", type)
        .orderBy()
        .desc("timestamp")
        .findList();
  }

  public static List<KmsHistory> getAllTargetKeyRefs(
      UUID targetUUID, KmsHistoryId.TargetType type) {
    return KmsHistory.find
        .query()
        .where()
        .eq("target_uuid", targetUUID)
        .eq("type", type)
        .orderBy()
        .desc("timestamp")
        .findList();
  }

  public static List<KmsHistory> getAllUniverseKeysWithActiveMasterKey(UUID targetUUID) {
    // Get the latest reEncryptionCount value. This represents the most active master key.
    int latestReEncryptionCount = getLatestReEncryptionCount(targetUUID);
    return KmsHistory.find
        .query()
        .where()
        .eq("target_uuid", targetUUID)
        .eq("type", KmsHistoryId.TargetType.UNIVERSE_KEY)
        .eq("re_encryption_count", latestReEncryptionCount)
        .orderBy()
        .desc("timestamp")
        .findList();
  }

  public static KmsHistory getKeyRefConfig(
      UUID targetUUID, UUID configUUID, String keyRef, KmsHistoryId.TargetType type) {
    return KmsHistory.find
        .query()
        .where()
        .idEq(new KmsHistoryId(keyRef, targetUUID, type, getLatestReEncryptionCount(targetUUID)))
        .eq("config_uuid", configUUID)
        .eq("type", type)
        .findOne();
  }

  public static KmsHistory getKmsHistory(
      UUID targetUUID, String keyRef, KmsHistoryId.TargetType type) {
    return KmsHistory.find
        .query()
        .where()
        .idEq(new KmsHistoryId(keyRef, targetUUID, type, getLatestReEncryptionCount(targetUUID)))
        .findOne();
  }

  public static KmsHistory getActiveHistory(UUID targetUUID, KmsHistoryId.TargetType type) {
    return KmsHistory.find
        .query()
        .where()
        .eq("target_uuid", targetUUID)
        .eq("type", type)
        .eq("active", true)
        .findOne();
  }

  public static List<KmsHistory> getAllActiveHistory(KmsHistoryId.TargetType type) {
    return KmsHistory.find.query().where().eq("type", type).eq("active", true).findList();
  }

  public static boolean entryExists(UUID targetUUID, String keyRef, KmsHistoryId.TargetType type) {
    return KmsHistory.find
        .query()
        .where()
        .idEq(new KmsHistoryId(keyRef, targetUUID, type, getLatestReEncryptionCount(targetUUID)))
        .exists();
  }

  public static boolean dbKeyIdExists(
      UUID targetUUID, String dbKeyId, KmsHistoryId.TargetType type) {
    return KmsHistory.find
        .query()
        .where()
        .eq("target_uuid", targetUUID)
        .eq("db_key_id", dbKeyId)
        .eq("type", type)
        .exists();
  }

  public static KmsHistory getLatestKmsHistoryWithDbKeyId(
      UUID targetUUID, String dbKeyId, KmsHistoryId.TargetType type) {
    return KmsHistory.find
        .query()
        .where()
        .eq("target_uuid", targetUUID)
        .eq("db_key_id", dbKeyId)
        .eq("type", type)
        .eq("re_encryption_count", getLatestReEncryptionCount(targetUUID))
        .findOne();
  }

  public static KmsHistory getLatestConfigHistory(
      UUID targetUUID, UUID configUUID, KmsHistoryId.TargetType type) {
    KmsHistory latestConfigHistory = null;
    List<KmsHistory> configKeyHistory =
        KmsHistory.getAllConfigTargetKeyRefs(configUUID, targetUUID, type);
    if (configKeyHistory.size() > 0) {
      latestConfigHistory = configKeyHistory.get(0);
    }
    return latestConfigHistory;
  }

  public static void deleteKeyRef(KmsHistory keyHistory) {
    keyHistory.delete();
  }

  public static void deleteAllTargetKeyRefs(UUID targetUUID, KmsHistoryId.TargetType type) {
    getAllTargetKeyRefs(targetUUID, type).forEach(KmsHistory::deleteKeyRef);
  }

  public static int deleteAllConfigTargetKeyRefs(
      UUID configUUID, UUID targetUUID, KmsHistoryId.TargetType type) {
    List<KmsHistory> keyList = getAllConfigTargetKeyRefs(configUUID, targetUUID, type);
    int count = keyList.size();
    keyList.forEach(KmsHistory::deleteKeyRef);
    return count;
  }

  public static boolean configHasHistory(UUID configUUID, KmsHistoryId.TargetType type) {
    return KmsHistory.find
            .query()
            .where()
            .eq("config_uuid", configUUID)
            .eq("type", type)
            .findList()
            .size()
        != 0;
  }

  public static Set<Universe> getUniverses(UUID configUUID, KmsHistoryId.TargetType type) {
    Set<UUID> universeUUIDs = new HashSet<>();
    KmsHistory.find
        .query()
        .where()
        .eq("config_uuid", configUUID)
        .eq("type", type)
        .findList()
        .forEach(n -> universeUUIDs.add(n.getUuid().targetUuid));
    return Universe.getAllPresent(universeUUIDs);
  }

  public static Set<UUID> getDistinctKmsConfigUUIDs(UUID targetUUID) {
    Set<UUID> KmsConfigUUIDs = new HashSet<>();
    KmsHistory.find
        .query()
        .where()
        .eq("target_uuid", targetUUID)
        .eq("type", KmsHistoryId.TargetType.UNIVERSE_KEY)
        .findList()
        .forEach(kh -> KmsConfigUUIDs.add(kh.getConfigUuid()));
    return KmsConfigUUIDs;
  }

  public KmsConfig getAssociatedKmsConfig() {
    return KmsConfig.getOrBadRequest(this.configUuid);
  }

  @Override
  public String toString() {
    return Json.newObject()
        .put("uuid", getUuid().toString())
        .put("config_uuid", getConfigUuid().toString())
        .put("timestamp", getTimestamp().toString())
        .put("version", getVersion())
        .put("active", isActive())
        .put("re_encryption_count", uuid.reEncryptionCount)
        .toString();
  }
}
