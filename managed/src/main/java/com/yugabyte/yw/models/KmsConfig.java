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
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.PlatformServiceException;
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
import lombok.Getter;
import lombok.Setter;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.validation.Constraints;

@Entity
@Table(uniqueConstraints = @UniqueConstraint(columnNames = {"customer_uuid", "name"}))
@ApiModel(description = "KMS configuration")
@Getter
@Setter
public class KmsConfig extends Model {
  public static final Logger LOG = LoggerFactory.getLogger(KmsConfig.class);

  public static final int SCHEMA_VERSION = 2;

  @Id
  @ApiModelProperty(value = "KMS config UUID", accessMode = READ_ONLY)
  private UUID configUUID;

  @Column(length = 100, nullable = false)
  @ApiModelProperty(value = "KMS config name", example = "kms config name")
  private String name;

  @Column(nullable = false)
  @ApiModelProperty(value = "Customer UUID", accessMode = READ_ONLY)
  private UUID customerUUID;

  @Column(length = 100, nullable = false)
  @ApiModelProperty(value = "KMS key provider")
  private KeyProvider keyProvider;

  @Constraints.Required
  @Column(nullable = false, columnDefinition = "TEXT")
  @DbJson
  @Encrypted
  @JsonIgnore
  @ApiModelProperty(value = "Auth config")
  private ObjectNode authConfig;

  @Constraints.Required
  @Column(nullable = false)
  @ApiModelProperty(value = "KMS configuration version")
  private int version;

  public static final Finder<UUID, KmsConfig> find =
      new Finder<UUID, KmsConfig>(KmsConfig.class) {};

  public static KmsConfig get(UUID configUUID) {
    if (configUUID == null) return null;
    return KmsConfig.find.query().where().idEq(configUUID).findOne();
  }

  /**
   * Good for chained function calls.
   *
   * @param configUUID the kms config UUID.
   * @return the KMS config object.
   */
  public static KmsConfig getOrBadRequest(UUID configUUID) {
    KmsConfig kmsConfig = KmsConfig.get(configUUID);
    if (kmsConfig == null) {
      throw new PlatformServiceException(BAD_REQUEST, "KMS config not found: " + kmsConfig);
    }
    return kmsConfig;
  }

  public static KmsConfig getOrBadRequest(UUID customerUUID, UUID configUUID) {
    KmsConfig kmsConfig =
        find.query().where().idEq(configUUID).eq("customer_uuid", customerUUID).findOne();
    if (kmsConfig == null) {
      throw new PlatformServiceException(BAD_REQUEST, "KMS config not found: " + kmsConfig);
    }
    return kmsConfig;
  }

  public static KmsConfig createKMSConfig(
      UUID customerUUID, KeyProvider keyProvider, ObjectNode authConfig, String name) {
    KmsConfig kmsConfig = new KmsConfig();
    kmsConfig.setKeyProvider(keyProvider);
    kmsConfig.setCustomerUUID(customerUUID);
    kmsConfig.setAuthConfig(authConfig);
    kmsConfig.setVersion(SCHEMA_VERSION);
    kmsConfig.setName(name);
    kmsConfig.save();
    return kmsConfig;
  }

  public static ObjectNode getKMSAuthObj(UUID configUUID) {
    KmsConfig config = get(configUUID);
    if (config == null) return null;
    return config.getAuthConfig();
  }

  public static List<KmsConfig> listKMSConfigs(UUID customerUUID) {
    return KmsConfig.find
        .query()
        .where()
        .eq("customer_UUID", customerUUID)
        .eq("version", SCHEMA_VERSION)
        .findList();
  }

  public static List<KmsConfig> listKMSProviderConfigs(KeyProvider keyProvider) {
    return KmsConfig.find
        .query()
        .where()
        .eq("key_provider", keyProvider)
        .eq("version", SCHEMA_VERSION)
        .findList();
  }

  public static List<KmsConfig> listAllKMSConfigs() {
    return KmsConfig.find.query().where().eq("version", SCHEMA_VERSION).findList();
  }

  public static KmsConfig updateKMSConfig(UUID configUUID, ObjectNode updatedConfig) {
    KmsConfig existingConfig = get(configUUID);
    if (existingConfig == null) return null;
    existingConfig.setAuthConfig(updatedConfig);
    existingConfig.save();
    return existingConfig;
  }
}
