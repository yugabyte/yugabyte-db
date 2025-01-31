// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.models.migrations;

import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.EnumValue;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import jakarta.persistence.Table;
import java.util.List;
import java.util.UUID;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.Getter;
import lombok.NoArgsConstructor;
import lombok.Setter;

/** Snapshot View of ORM entities at the time migration V360 was added. */
public class V360 {

  @Entity
  @Getter
  @Setter
  public static class Users extends Model {

    public static final Finder<UUID, Users> find = new Finder<UUID, Users>(Users.class) {};

    @Id private UUID uuid;

    public static List<Users> getAll() {
      return find.query().where().findList();
    }
  }

  @Entity
  @Table(name = "principal")
  @Data
  @NoArgsConstructor
  @EqualsAndHashCode(callSuper = false)
  public static class Principal extends Model {

    @Id
    @Column(name = "uuid", nullable = false, unique = true)
    private UUID uuid;

    // At most one of these can be valid.
    @Column(name = "user_uuid")
    private UUID userUUID;

    @Column(name = "group_uuid")
    private UUID groupUUID;

    @Column(name = "type", nullable = false)
    private PrincipalType type;

    public enum PrincipalType {
      @EnumValue("USER")
      USER,

      @EnumValue("LDAP_GROUP")
      LDAP_GROUP,

      @EnumValue("OIDC_GROUP")
      OIDC_GROUP
    }

    public Principal(Users user) {
      uuid = user.getUuid();
      userUUID = uuid;
      groupUUID = null;
      type = PrincipalType.USER;
    }

    public static Principal get(UUID principalUUID) {
      Principal principal = find.query().where().eq("uuid", principalUUID).findOne();
      return principal;
    }

    public static final Finder<UUID, Principal> find =
        new Finder<UUID, Principal>(Principal.class) {};
  }
}
