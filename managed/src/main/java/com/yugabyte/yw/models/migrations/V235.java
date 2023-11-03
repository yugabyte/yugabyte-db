/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.models.migrations;

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
import lombok.Getter;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;
import play.data.validation.Constraints;

/** Snapshot View of ORM entities at the time migration V235 was added. */
@Slf4j
public class V235 {
  @Entity
  @Table(uniqueConstraints = @UniqueConstraint(columnNames = {"customer_uuid", "name"}))
  @ApiModel(description = "KMS configuration")
  @Getter
  @Setter
  public static class KmsConfig extends Model {

    public static final int SCHEMA_VERSION = 2;
    public static final Finder<UUID, KmsConfig> find =
        new Finder<UUID, KmsConfig>(KmsConfig.class) {};

    @Id
    @ApiModelProperty(value = "KMS config UUID", accessMode = READ_ONLY)
    private UUID configUUID;

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

    public static List<KmsConfig> listAllKMSConfigs() {
      return KmsConfig.find.query().where().eq("version", SCHEMA_VERSION).findList();
    }
  }
}
