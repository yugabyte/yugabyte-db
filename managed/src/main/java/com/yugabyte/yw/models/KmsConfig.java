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

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.Encrypted;
import io.ebean.annotation.JsonIgnore;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import java.util.UUID;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.Id;
import javax.persistence.Table;
import javax.persistence.UniqueConstraint;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.validation.Constraints;

@Entity
@Table(uniqueConstraints = @UniqueConstraint(columnNames = {"customer_uuid", "name"}))
@ApiModel(description = "KMS configuration")
public class KmsConfig extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(KmsConfig.class);

  public static final int SCHEMA_VERSION = 2;

  @Id
  @ApiModelProperty(value = "KMS config UUID", accessMode = READ_ONLY)
  public UUID configUUID;

  @Column(length = 100, nullable = false)
  @ApiModelProperty(value = "KMS config name", example = "kms config name")
  public String name;

  @Column(nullable = false)
  @ApiModelProperty(value = "Customer UUID", accessMode = READ_ONLY)
  public UUID customerUUID;

  @Column(length = 100, nullable = false)
  @ApiModelProperty(value = "KMS key provider")
  public KeyProvider keyProvider;

  @Constraints.Required
  @Column(nullable = false, columnDefinition = "TEXT")
  @DbJson
  @Encrypted
  @JsonIgnore
  @ApiModelProperty(value = "Auth config")
  public ObjectNode authConfig;

  @Constraints.Required
  @Column(nullable = false)
  @ApiModelProperty(value = "KMS configuration version")
  public int version;

  public static final Finder<UUID, KmsConfig> find =
      new Finder<UUID, KmsConfig>(KmsConfig.class) {};

  public static KmsConfig get(UUID configUUID) {
    if (configUUID == null) return null;
    return KmsConfig.find.query().where().idEq(configUUID).findOne();
  }

  public static KmsConfig createKMSConfig(
      UUID customerUUID, KeyProvider keyProvider, ObjectNode authConfig, String name) {
    KmsConfig kmsConfig = new KmsConfig();
    kmsConfig.keyProvider = keyProvider;
    kmsConfig.customerUUID = customerUUID;
    kmsConfig.authConfig = authConfig;
    kmsConfig.version = SCHEMA_VERSION;
    kmsConfig.name = name;
    kmsConfig.save();
    return kmsConfig;
  }

  public static ObjectNode getKMSAuthObj(UUID configUUID) {
    KmsConfig config = get(configUUID);
    if (config == null) return null;
    return config.authConfig;
  }

  public static List<KmsConfig> listKMSConfigs(UUID customerUUID) {
    return KmsConfig.find
        .query()
        .where()
        .eq("customer_UUID", customerUUID)
        .eq("version", SCHEMA_VERSION)
        .findList();
  }

  public static List<KmsConfig> listAllKMSConfigs() {
    return KmsConfig.find.query().where().eq("version", SCHEMA_VERSION).findList();
  }

  public static KmsConfig updateKMSConfig(UUID configUUID, ObjectNode updatedConfig) {
    KmsConfig existingConfig = get(configUUID);
    if (existingConfig == null) return null;
    existingConfig.authConfig = updatedConfig;
    existingConfig.save();
    return existingConfig;
  }
}
