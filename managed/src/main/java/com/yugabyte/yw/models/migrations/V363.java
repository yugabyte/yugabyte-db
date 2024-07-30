// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.migrations;

import com.fasterxml.jackson.annotation.JsonProperty;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import jakarta.persistence.Table;
import java.util.Date;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.NoArgsConstructor;

/** Snapshot View of ORM entities at the time migration V363 was added. */
public class V363 {

  @Entity
  @Table(name = "role")
  @EqualsAndHashCode(callSuper = false)
  @Data
  public static class Role extends Model {
    public static final Finder<UUID, Role> find = new Finder<UUID, Role>(Role.class) {};

    @Id private UUID roleUUID;

    private UUID customerUUID;

    private Date updatedOn;

    @DbJson private PermissionDetails permissionDetails;

    public static List<Role> getCustomRoles() {
      return find.query().where().eq("role_type", "Custom").findList();
    }
  }

  @Data
  @EqualsAndHashCode(callSuper = false)
  public static class PermissionDetails {

    private Set<Permission> permissionList;
  }

  @Data
  @AllArgsConstructor
  @NoArgsConstructor
  public static class Permission {

    @JsonProperty("resourceType")
    private ResourceType resourceType;

    @JsonProperty("action")
    private Action action;
  }

  public enum ResourceType {
    UNIVERSE,
    ROLE,
    USER,
    OTHER
  }

  public enum Action {
    CREATE,
    READ,
    UPDATE,
    DELETE,
    PAUSE_RESUME,
    BACKUP_RESTORE,
    UPDATE_ROLE_BINDINGS,
    UPDATE_PROFILE,
    SUPER_ADMIN_ACTIONS,
    XCLUSTER
  }
}
