// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.models;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.GroupMappingInfo.GroupType;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.Cache;
import io.ebean.annotation.EnumValue;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import jakarta.persistence.Table;
import java.util.UUID;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.NoArgsConstructor;
import lombok.ToString;

@Entity
@Table(name = "principal")
@Data
@NoArgsConstructor
@EqualsAndHashCode(callSuper = false)
@ToString
@Cache
public class Principal extends Model {
  // A Principal is an entity which is allowed to have role bindings.
  // Currently it can either be a group or an user.

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

  public Principal(GroupMappingInfo info) {
    uuid = info.getGroupUUID();
    userUUID = null;
    groupUUID = uuid;
    if (info.getType().equals(GroupType.LDAP)) {
      type = PrincipalType.LDAP_GROUP;
    } else {
      type = PrincipalType.OIDC_GROUP;
    }
  }

  @JsonIgnore
  public UUID getCustomerUUID() {
    if (this.type.equals(PrincipalType.USER)) {
      return Users.getOrBadRequest(this.uuid).getCustomerUUID();
    }
    return GroupMappingInfo.getOrBadRequest(this.uuid).getCustomerUUID();
  }

  public static Principal get(UUID principalUUID) {
    Principal principal = find.byId(principalUUID);
    return principal;
  }

  public static Principal getOrBadRequest(UUID principalUuid) {
    Principal principal = get(principalUuid);
    if (principal == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid Principal UUID:" + principalUuid);
    }
    return principal;
  }

  public static final Finder<UUID, Principal> find =
      new Finder<UUID, Principal>(Principal.class) {};
}
