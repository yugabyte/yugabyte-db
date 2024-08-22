// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.models.migrations;

import static play.mvc.Http.Status.NOT_FOUND;

import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.helpers.PermissionDetails;
import com.yugabyte.yw.models.rbac.ResourceGroup;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbArray;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.EnumValue;
import io.ebean.annotation.Transactional;
import io.ebean.annotation.WhenCreated;
import io.ebean.annotation.WhenModified;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.EnumType;
import jakarta.persistence.Enumerated;
import jakarta.persistence.Id;
import jakarta.persistence.JoinColumn;
import jakarta.persistence.ManyToOne;
import jakarta.persistence.Table;
import jakarta.persistence.Transient;
import java.nio.charset.StandardCharsets;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.Getter;
import lombok.NoArgsConstructor;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;

/**
 * Snapshot View of ORM entities at the time migration V364 was added. These are the entities
 * necessary to migrate the existing group mappings to new table.
 */
public class V365 {

  @Entity
  public static class LdapDnToYbaRole extends Model {

    @Id
    @Column(nullable = false, unique = true)
    public UUID uuid = UUID.randomUUID();

    @Column(nullable = false)
    public UUID customerUUID;

    @Column(nullable = false, unique = true)
    public String distinguishedName = null;

    @Column(nullable = false)
    public Users.Role ybaRole;

    public static final Finder<UUID, LdapDnToYbaRole> find =
        new Finder<UUID, LdapDnToYbaRole>(LdapDnToYbaRole.class) {};
  }

  @Entity
  @Table(name = "oidc_group_to_yba_roles")
  @Data
  public static class OidcGroupToYbaRoles extends Model {
    @Id
    @Column(name = "uuid", nullable = false, unique = true)
    public UUID uuid = UUID.randomUUID();

    @Column(name = "customer_uuid", nullable = false)
    public UUID customerUUID;

    @Column(name = "group_name", nullable = false, unique = true)
    public String groupName = null;

    @Column(name = "yba_roles", nullable = false)
    @DbArray
    public List<UUID> ybaRoles;

    public static final Finder<UUID, OidcGroupToYbaRoles> find =
        new Finder<UUID, OidcGroupToYbaRoles>(OidcGroupToYbaRoles.class) {};
  }

  @Entity
  @Table(name = "principal")
  @Data
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

    public Principal(GroupMappingInfo info) {
      uuid = info.getGroupUUID();
      userUUID = null;
      groupUUID = uuid;
      if (info.getType().equals(GroupMappingInfo.GroupType.LDAP)) {
        type = PrincipalType.LDAP_GROUP;
      } else {
        type = PrincipalType.OIDC_GROUP;
      }
    }

    public static Principal get(UUID principalUUID) {
      Principal principal = find.query().where().eq("uuid", principalUUID).findOne();
      return principal;
    }

    public static final Finder<UUID, Principal> find =
        new Finder<UUID, Principal>(Principal.class) {};
  }

  @Entity
  @Table(name = "groups_mapping_info")
  @Data
  @Slf4j
  public static class GroupMappingInfo extends Model {
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

    public static GroupMappingInfo get(UUID groupUUID) {
      GroupMappingInfo info = find.query().where().eq("uuid", groupUUID).findOne();
      return info;
    }

    public static final Finder<UUID, GroupMappingInfo> find =
        new Finder<UUID, GroupMappingInfo>(GroupMappingInfo.class) {};
  }

  @Entity
  @AllArgsConstructor
  @NoArgsConstructor
  public static class RoleBinding extends Model {

    @Id
    @Column(name = "uuid", nullable = false, unique = true)
    private UUID uuid;

    @ManyToOne
    @JoinColumn(name = "principal_uuid", referencedColumnName = "uuid")
    private Principal principal;

    @Transient private Users user;

    @Transient private GroupMappingInfo groupInfo;

    /**
     * This shows whether the role binding is system generated or user generated. System generated
     * role bindings are usually for the LDAP group users. Custom role bindings are when the user
     * sets the role bindings usually through the UI.
     */
    public enum RoleBindingType {
      @EnumValue("System")
      System,

      @EnumValue("Custom")
      Custom
    }

    @Enumerated(EnumType.STRING)
    @Column(name = "type", nullable = false)
    private RoleBindingType type;

    @ManyToOne
    @JoinColumn(name = "role_uuid", referencedColumnName = "role_uuid")
    private Role role;

    @Column(name = "create_time", nullable = false)
    @WhenCreated
    private Date createTime;

    @Column(name = "update_time", nullable = false)
    @WhenModified
    private Date updateTime;

    @Column(name = "resource_group", columnDefinition = "TEXT")
    @DbJson
    private ResourceGroup resourceGroup;

    public static final Finder<UUID, RoleBinding> find =
        new Finder<UUID, RoleBinding>(RoleBinding.class) {};

    public static RoleBinding create(
        GroupMappingInfo info,
        RoleBindingType roleBindingType,
        Role role,
        ResourceGroup resourceGroup) {

      Principal principal = Principal.find.byId(info.getGroupUUID());
      RoleBinding roleBinding =
          new RoleBinding(
              UUID.randomUUID(),
              principal,
              null,
              info,
              roleBindingType,
              role,
              new Date(),
              new Date(),
              resourceGroup);
      roleBinding.save();
      return roleBinding;
    }
  }

  @Entity
  public static class RuntimeConfigEntry extends Model {
    public static final Finder<UUID, RuntimeConfigEntry> find =
        new Finder<UUID, RuntimeConfigEntry>(RuntimeConfigEntry.class) {};

    private byte[] value;

    public String getValue() {
      return new String(this.value, StandardCharsets.UTF_8);
    }
  }

  @Entity
  @Getter
  @AllArgsConstructor
  @NoArgsConstructor
  public static class Role extends Model {

    @Id
    @Column(name = "role_uuid", nullable = false)
    private UUID roleUUID;

    @Column(name = "customer_uuid", nullable = false)
    private UUID customerUUID;

    @Column(name = "name", nullable = false)
    private String name;

    @Column(name = "description", nullable = false)
    private String description;

    @Column(name = "created_on", nullable = false)
    @WhenCreated
    private Date createdOn;

    @Setter
    @Column(name = "updated_on", nullable = false)
    @WhenModified
    private Date updatedOn;

    /**
     * This shows whether the role is a built-in role or user created role. System roles are the
     * SuperAdmin, Admin, BackupAdmin, ReadOnly, ConnectOnly. Custom roles are the user created
     * roles with custom permissions.
     */
    public enum RoleType {
      @EnumValue("System")
      System,

      @EnumValue("Custom")
      Custom
    }

    @Enumerated(EnumType.STRING)
    @Column(name = "role_type", nullable = false)
    private RoleType roleType;

    @Setter
    @DbJson
    @Column(name = "permission_details")
    private PermissionDetails permissionDetails;

    public static final Finder<UUID, Role> find = new Finder<UUID, Role>(Role.class) {};

    public static Role get(UUID customerUUID, UUID roleUUID) {
      return find.query()
          .where()
          .eq("customer_uuid", customerUUID)
          .eq("role_uuid", roleUUID)
          .findOne();
    }

    public static Role getOrBadRequest(UUID customerUUID, String name) {
      Role role = get(customerUUID, name);
      if (role == null) {
        throw new PlatformServiceException(
            NOT_FOUND,
            String.format("Invalid role name '%s' for customer '%s'.", name, customerUUID));
      }
      return role;
    }

    public static Role get(UUID customerUUID, String name) {
      return find.query().where().eq("customer_uuid", customerUUID).eq("name", name).findOne();
    }
  }

  @Entity
  @Getter
  @Setter
  public static class Users extends Model {

    public static final Finder<UUID, Users> find = new Finder<UUID, Users>(Users.class) {};

    @Id private UUID uuid;

    public static List<Users> getAll() {
      return find.query().where().findList();
    }

    public enum Role {
      @EnumValue("ConnectOnly")
      ConnectOnly,

      @EnumValue("ReadOnly")
      ReadOnly,

      @EnumValue("BackupAdmin")
      BackupAdmin,

      @EnumValue("Admin")
      Admin,

      @EnumValue("SuperAdmin")
      SuperAdmin;
    }
  }
}
