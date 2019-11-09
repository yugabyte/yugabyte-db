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

import com.avaje.ebean.Model;
import com.avaje.ebean.annotation.DbJson;
import com.avaje.ebean.annotation.JsonIgnore;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.api.Play;
import play.data.validation.Constraints;
import play.libs.Json;

import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import java.util.Arrays;
import java.util.List;
import java.util.UUID;

@Entity
public class KmsConfig extends Model {
    public static final Logger LOG = LoggerFactory.getLogger(KmsConfig.class);

    @Id
    public UUID configUUID;

    @Column(nullable = false)
    public UUID customerUUID;

    @Column(length=100, nullable = false)
    public KeyProvider keyProvider;

    @Constraints.Required
    @Column(nullable = false, columnDefinition = "TEXT")
    @DbJson
    @JsonIgnore
    public JsonNode authConfig;

    @Constraints.Required
    @Column(nullable = false)
    public int version;

    public static final Find<UUID, KmsConfig> find = new Find<UUID, KmsConfig>(){};

    public static List<KmsConfig> getAll(UUID customerUUID) {
        return KmsConfig.find.where()
                .eq("customer_uuid", customerUUID)
                .findList();
    }

    public static KmsConfig get(UUID customerUUID, UUID configUUID) {
        return KmsConfig.find.where()
                .eq("customer_uuid", customerUUID).idEq(configUUID)
                .findUnique();
    }

    public static KmsConfig createKMSConfig(
            UUID customerUUID,
            KeyProvider keyProvider,
            ObjectNode authConfig
    ) {
        KmsConfig kmsConfig = new KmsConfig();
        kmsConfig.keyProvider = keyProvider;
        kmsConfig.customerUUID = customerUUID;
        kmsConfig.authConfig = authConfig;
        kmsConfig.version = 1;
        kmsConfig.save();
        return kmsConfig;
    }

    public static KmsConfig getKMSConfig(UUID customerUUID, KeyProvider keyProvider) {
        return KmsConfig.find.where()
                .eq("customer_uuid", customerUUID)
                .eq("key_provider", keyProvider)
                .findUnique();
    }

    public static ObjectNode getKMSAuthObj(UUID customerUUID, KeyProvider keyProvider) {
        KmsConfig config = getKMSConfig(customerUUID, keyProvider);
        if (config == null) return null;
        return (ObjectNode) config.authConfig.deepCopy();
    }

    public static List<KmsConfig> listKMSConfigs(UUID customerUUID) {
        return KmsConfig.find.where()
                .eq("customer_uuid", customerUUID)
                .findList();
    }

    public static KmsConfig updateKMSAuthObj(
            UUID customerUUID,
            KeyProvider keyProvider,
            ObjectNode newAuth
    ) {
        KmsConfig config = getKMSConfig(customerUUID, keyProvider);
        if (config == null) return null;
        config.authConfig = newAuth;
        config.save();
        return config;
    }
}
