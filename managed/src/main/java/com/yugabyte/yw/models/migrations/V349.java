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

import com.fasterxml.jackson.annotation.JsonIgnore;
import io.ebean.Finder;
import io.ebean.Model;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import java.util.List;
import java.util.UUID;
import lombok.Getter;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/** Snapshot View of ORM entities at the time migration V349 was added. */
@Slf4j
public class V349 {
  @Slf4j
  @Entity
  @ApiModel(description = "A user associated with a customer")
  @Getter
  @Setter
  public static class Users extends Model {
    public static final Logger LOG = LoggerFactory.getLogger(Users.class);

    public static final Finder<UUID, Users> find = new Finder<UUID, Users>(Users.class) {};

    @Id
    @ApiModelProperty(value = "User UUID", accessMode = READ_ONLY)
    private UUID uuid;

    @ApiModelProperty(value = "Customer UUID", accessMode = READ_ONLY)
    private UUID customerUUID;

    @JsonIgnore private String apiToken;

    public static List<Users> getAll(UUID customerUUID) {
      return find.query().where().eq("customer_uuid", customerUUID).findList();
    }

    public static List<Users> getAll() {
      return find.query().where().findList();
    }
  }
}
