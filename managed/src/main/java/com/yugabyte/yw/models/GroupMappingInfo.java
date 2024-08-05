// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.models;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.rbac.Role;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.EnumValue;
import io.ebean.annotation.Transactional;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.Id;
import jakarta.persistence.Table;
import java.util.UUID;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.extern.slf4j.Slf4j;

@Entity
@Table(name = "groups_mapping_info")
@Data
@EqualsAndHashCode(callSuper = false)
@Slf4j
public class GroupMappingInfo extends Model {
  @Id
  @Column(name = "uuid", nullable = false, unique = true)
  private UUID groupUUID = UUID.randomUUID();

  @Column(name = "customer_uuid", nullable = false)
  private UUID customerUUID;

  // This will only be used when RBAC is off.
  @Column(name = "role_uuid", nullable = false)
  private UUID roleUUID;

  @Column(name = "identifier", nullable = false, unique = true)
  private String identifier = null;

  @Column(name = "type", nullable = false)
  private GroupType type;

  public enum GroupType {
    @EnumValue("LDAP")
    LDAP,

    @EnumValue("OIDC")
    OIDC;
  }

  public static GroupMappingInfo create(UUID customerUUID, String identifier, GroupType type) {
    // Assign ConnectOnly if no role is assigned
    UUID roleUUID = Role.get(customerUUID, "ConnectOnly").getRoleUUID();
    return create(customerUUID, roleUUID, identifier, type);
  }

  @Transactional
  public static GroupMappingInfo create(
      UUID customerUUID, UUID roleUUID, String identifier, GroupType type) {
    GroupMappingInfo entity = new GroupMappingInfo();
    entity.customerUUID = customerUUID;
    entity.identifier = identifier;
    entity.roleUUID = roleUUID;
    entity.type = type;
    entity.save();
    return entity;
  }

  /** Wrapper around save to make sure principal entity is created. */
  @Transactional
  @Override
  public void save() {
    super.save();
    Principal principal = Principal.get(this.groupUUID);
    if (principal == null) {
      log.info("Adding Principal entry for Group: " + this.identifier);
      new Principal(this).save();
    }
  }

  /** Wrapper around delete to make sure principal entity is deleted. */
  @Transactional
  @Override
  public boolean delete() {
    log.info("Deleting Principal entry for Group: " + this.identifier);
    Principal principal = Principal.getOrBadRequest(this.groupUUID);
    principal.delete();
    return super.delete();
  }

  public static GroupMappingInfo get(UUID groupUUID) {
    GroupMappingInfo info = find.query().where().eq("uuid", groupUUID).findOne();
    return info;
  }

  public static GroupMappingInfo getOrBadRequest(UUID groupUuid) {
    GroupMappingInfo info = get(groupUuid);
    if (info == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid Group UUID:" + groupUuid);
    }
    return info;
  }

  public static final Finder<UUID, GroupMappingInfo> find =
      new Finder<UUID, GroupMappingInfo>(GroupMappingInfo.class) {};
}
