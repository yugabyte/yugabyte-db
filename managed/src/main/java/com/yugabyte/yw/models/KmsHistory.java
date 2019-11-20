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

import com.avaje.ebean.*;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.validation.Constraints;

import javax.persistence.Temporal;
import javax.persistence.TemporalType;
import javax.persistence.Column;
import javax.persistence.Entity;
import java.util.Date;
import java.util.List;
import java.util.UUID;

import javax.persistence.Transient;
import javax.persistence.IdClass;
import javax.persistence.AttributeOverride;
import javax.persistence.AttributeOverrides;
import javax.persistence.EmbeddedId;

@Entity
@IdClass(KmsHistoryId.class)
public class KmsHistory extends Model {
    public static final Logger LOG = LoggerFactory.getLogger(KmsHistory.class);

    public static final int SCHEMA_VERSION = 1;

    @EmbeddedId
    @AttributeOverrides({
            @AttributeOverride( name = "configUUID", column = @Column(name = "config_uuid") ),
            @AttributeOverride( name = "targetUUID", column = @Column(name = "target_uuid") ),
            @AttributeOverride( name = "type", column = @Column(name = "type"))
    })
    public KmsHistoryId uuid;

    @Transient
    public UUID configUuid;

    @Transient
    public UUID targetUuid;

    @Transient
    public KmsHistoryId.TargetType type;

    @Constraints.Required
    @Temporal(TemporalType.TIMESTAMP)
    @Column(nullable = false)
    public Date timestamp;

    @Constraints.Required
    @Column(nullable = false)
    public int version;

    @Constraints.Required
    @Column(nullable = false)
    public String keyRef;

    public static final Find<KmsHistoryId, KmsHistory> find = new Find<KmsHistoryId, KmsHistory>(){};

    public static KmsHistory createKmsHistory(
            UUID configUUID,
            UUID targetUUID,
            KmsHistoryId.TargetType targetType,
            String keyRef
    ) {
        KmsHistory keyHistory = new KmsHistory();
        keyHistory.uuid = new KmsHistoryId(configUUID, targetUUID, targetType);
        keyHistory.keyRef = keyRef;
        keyHistory.timestamp = new Date();
        keyHistory.version = SCHEMA_VERSION;
        keyHistory.save();
        return keyHistory;
    }

    public static List<KmsHistory> getAllConfigTargetKeyRefs(
            UUID configUUID,
            UUID targetUUID,
            KmsHistoryId.TargetType type
    ) {
        return KmsHistory.find.where()
                .eq("config_uuid", configUUID)
                .eq("target_uuid", targetUUID)
                .eq("type", type)
                .findList();
    }

    public static List<KmsHistory> getAllTargetKeyRefs(
            UUID targetUUID,
            KmsHistoryId.TargetType type
    ) {
        return KmsHistory.find.where()
                .eq("target_uuid", targetUUID)
                .eq("type", type)
                .findList();
    }

    public static KmsHistory getCurrentConfig(UUID targetUUID, KmsHistoryId.TargetType type) {
        String latestConfigUUIDQuery = "SELECT config_uuid FROM kms_history" +
                " WHERE timestamp IN (" +
                "SELECT MAX(timestamp) AS timestamp" +
                " FROM kms_history " +
                " WHERE type = :type" +
                " AND target_uuid = :targetUUID" +
                ")";
        RawSql rawSql = RawSqlBuilder.parse(latestConfigUUIDQuery).create();
        Query<KmsHistory> query = Ebean.find(KmsHistory.class);
        query.setRawSql(rawSql);
        query.setParameter("type", type);
        query.setParameter("targetUUID", targetUUID);
        return query.findUnique();
    }

    public static KmsHistory getKeyRefConfig(
            UUID targetUUID,
            String keyRef,
            KmsHistoryId.TargetType type
    ) {
        String keyRefConfigQuery = "SELECT config_uuid FROM kms_history" +
                " WHERE type = :type" +
                " AND target_uuid = :targetUUID" +
                " AND key_ref = :keyRef";
        RawSql rawSql = RawSqlBuilder.parse(keyRefConfigQuery).create();
        Query<KmsHistory> query = Ebean.find(KmsHistory.class);
        query.setRawSql(rawSql);
        query.setParameter("type", type);
        query.setParameter("targetUUID", targetUUID);
        query.setParameter("keyRef", keyRef);
        return query.findUnique();
    }

    public static KmsHistory getCurrentConfigKeyRef(
            UUID configUUID,
            UUID targetUUID,
            KmsHistoryId.TargetType type
    ) {
        String latestKeyQuery = "SELECT key_ref FROM kms_history" +
                " WHERE timestamp IN (" +
                "SELECT MAX(timestamp) AS timestamp" +
                " FROM kms_history " +
                " WHERE config_uuid = :configUUID" +
                " AND type = :type" +
                " AND target_uuid = :targetUUID" +
                ")";
        RawSql rawSql = RawSqlBuilder.parse(latestKeyQuery).create();
        Query<KmsHistory> query = Ebean.find(KmsHistory.class);
        query.setRawSql(rawSql);
        query.setParameter("configUUID", configUUID);
        query.setParameter("type", type);
        query.setParameter("targetUUID", targetUUID);
        return query.findUnique();
    }

    public static KmsHistory getCurrentKeyRef(
            UUID targetUUID,
            KmsHistoryId.TargetType type
    ) {
        String latestKeyQuery = "SELECT key_ref FROM kms_history" +
                " WHERE timestamp IN (" +
                "SELECT MAX(timestamp) AS timestamp" +
                " FROM kms_history " +
                " WHERE type = :type" +
                " AND target_uuid = :targetUUID" +
                ")";
        RawSql rawSql = RawSqlBuilder.parse(latestKeyQuery).create();
        Query<KmsHistory> query = Ebean.find(KmsHistory.class);
        query.setRawSql(rawSql);
        query.setParameter("type", type);
        query.setParameter("targetUUID", targetUUID);
        return query.findUnique();
    }

    public static void deleteKeyRef(KmsHistory keyHistory) { keyHistory.delete(); }

    public static void deleteAllTargetKeyRefs(
            UUID targetUUID,
            KmsHistoryId.TargetType type
    ) { getAllTargetKeyRefs(targetUUID, type).forEach(KmsHistory::deleteKeyRef); }

    public static void deleteAllConfigTargetKeyRefs(
            UUID configUUID,
            UUID targetUUID,
            KmsHistoryId.TargetType type
    ) { getAllConfigTargetKeyRefs(configUUID, targetUUID, type).forEach(KmsHistory::deleteKeyRef); }

    public static boolean configHasHistory(UUID configUUID, KmsHistoryId.TargetType type) {
        return KmsHistory.find.where()
                .eq("config_uuid", configUUID)
                .eq("type", type)
                .findList().size() != 0;
    }
}
