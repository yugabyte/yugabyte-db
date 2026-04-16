package com.yugabyte.yw.models.migrations;

import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.EnumValue;
import io.ebean.annotation.Transactional;
import io.ebean.annotation.WhenCreated;
import io.ebean.annotation.WhenModified;
import io.swagger.annotations.ApiModelProperty;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.EnumType;
import jakarta.persistence.Enumerated;
import jakarta.persistence.Id;
import jakarta.persistence.JoinColumn;
import jakarta.persistence.ManyToOne;
import jakarta.persistence.Table;
import jakarta.persistence.Transient;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.Getter;
import lombok.NoArgsConstructor;
import lombok.Setter;
import lombok.ToString;
import lombok.extern.slf4j.Slf4j;

public class V383 {

  @Data
  @AllArgsConstructor
  @NoArgsConstructor
  public static class PermissionInfo {

    @AllArgsConstructor
    public enum ResourceType {
      UNIVERSE("rbac/available_resource_permissions/universeResourcePermissions.json"),
      ROLE("rbac/available_resource_permissions/roleResourcePermissions.json"),
      USER("rbac/available_resource_permissions/userResourcePermissions.json"),
      OTHER("rbac/available_resource_permissions/otherResourcePermissions.json");

      @Getter public String permissionFilePath;

      public Set<UUID> getAllResourcesUUID(UUID customerUUID) {
        Customer customer = Customer.get(customerUUID);
        Set<UUID> allResourcesUUID = new HashSet<>();
        switch (this) {
          case UNIVERSE:
            allResourcesUUID = Universe.getAllUUIDs(customer);
            break;
          case ROLE:
            allResourcesUUID =
                Role.getAll(customerUUID).stream()
                    .map(Role::getRoleUUID)
                    .collect(Collectors.toSet());
            break;
          case USER:
            allResourcesUUID =
                Users.getAll(customerUUID).stream().map(Users::getUuid).collect(Collectors.toSet());
            break;
          case OTHER:
            // Nothing to check explicitly for OTHER resources.
            break;
          default:
            break;
        }
        return allResourcesUUID;
      }
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
      XCLUSTER,
      DEBUG
    }

    public ResourceType resourceType;

    public Action action;

    // Human readable name for the permission. Used for UI.
    public String name;

    public String description;

    // Some permissions like UNIVERSE.CREATE are not applicable on particular resources, but are a
    // generic permission. In such cases, "permissionValidOnResource" will be false.
    public boolean permissionValidOnResource;

    public HashSet<Permission> prerequisitePermissions;
  }

  @Data
  @AllArgsConstructor
  @NoArgsConstructor
  public static class PermissionDetails {
    @ApiModelProperty(value = "Set of permissions")
    Set<Permission> permissionList;
  }

  @Data
  @AllArgsConstructor
  @NoArgsConstructor
  public static class Permission {
    public PermissionInfo.ResourceType resourceType;

    public PermissionInfo.Action action;
  }

  @Entity
  @Getter
  @Table(name = "customer")
  public static class Customer extends Model {
    public static final Finder<UUID, Customer> find = new Finder<UUID, Customer>(Customer.class) {};

    // A globally unique UUID for the customer.
    @Column(nullable = false, unique = true)
    private UUID uuid = UUID.randomUUID();

    @Id public Long id;

    public static Customer get(UUID customerUUID) {
      return find.query().where().eq("uuid", customerUUID).findOne();
    }

    public static List<Customer> getAll() {
      return find.query().findList();
    }
  }

  @Entity
  @Getter
  @Setter
  @Table(name = "users")
  public static class Users extends Model {

    public static final Finder<UUID, Users> find = new Finder<UUID, Users>(Users.class) {};

    @Id private UUID uuid;

    private UUID customerUUID;
    private String email;

    public static List<Users> getAll() {
      return find.query().where().findList();
    }

    public static Users get(UUID userUUID) {
      return find.query().where().eq("uuid", userUUID).findOne();
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

    // The role of the user.
    @ApiModelProperty(value = "User role")
    private Role role;

    public static List<Users> getAll(UUID customerUUID) {
      return find.query().where().eq("customer_uuid", customerUUID).findList();
    }
  }

  @Entity
  @Getter
  @AllArgsConstructor
  @NoArgsConstructor
  @Table(name = "role")
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

    public static List<Role> getAll(UUID customerUUID) {
      return find.query().where().eq("customer_uuid", customerUUID).findList();
    }

    public static Role get(UUID customerUUID, String name) {
      return find.query().where().eq("customer_uuid", customerUUID).eq("name", name).findOne();
    }
  }

  @Entity
  @AllArgsConstructor
  @NoArgsConstructor
  @Setter
  @Table(name = "role_binding")
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
        Users user, RoleBindingType roleBindingType, Role role, ResourceGroup resourceGroup) {

      Principal principal = Principal.find.byId(user.getUuid());
      RoleBinding roleBinding =
          new RoleBinding(
              UUID.randomUUID(),
              principal,
              user,
              null,
              roleBindingType,
              role,
              new Date(),
              new Date(),
              resourceGroup);
      roleBinding.save();
      return roleBinding;
    }

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

    public static List<RoleBinding> getAll(UUID principalUUID) {
      Principal principal = Principal.get(principalUUID);
      List<RoleBinding> list = find.query().where().eq("principal_uuid", principalUUID).findList();
      if (principal.getType().equals(Principal.PrincipalType.USER)) {
        Users user = Users.get(principalUUID);
        list.forEach(rb -> rb.setUser(user));
      } else {
        GroupMappingInfo info = GroupMappingInfo.get(principalUUID);
        list.forEach(rb -> rb.setGroupInfo(info));
      }
      return list;
    }
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

    public static GroupMappingInfo get(UUID groupUUID) {
      GroupMappingInfo info = find.query().where().eq("uuid", groupUUID).findOne();
      return info;
    }

    public static final Finder<UUID, GroupMappingInfo> find =
        new Finder<UUID, GroupMappingInfo>(GroupMappingInfo.class) {};
  }

  @Getter
  @Setter
  @AllArgsConstructor
  @NoArgsConstructor
  @ToString
  @Slf4j
  @EqualsAndHashCode
  public static class ResourceGroup {

    @Builder
    @NoArgsConstructor
    @AllArgsConstructor
    @Getter
    @Setter
    @ToString
    @EqualsAndHashCode
    public static class ResourceDefinition {
      @ApiModelProperty(value = "Resource Type")
      private PermissionInfo.ResourceType resourceType;

      @ApiModelProperty(value = "Select all resources (including future resources)")
      @Builder.Default
      private boolean allowAll = false;

      @ApiModelProperty(value = "Set of resource uuids")
      @Builder.Default
      private Set<UUID> resourceUUIDSet = new HashSet<>();

      public ResourceDefinition clone() {
        return ResourceDefinition.builder()
            .resourceType(this.resourceType)
            .allowAll(this.allowAll)
            .resourceUUIDSet(new HashSet<>(this.resourceUUIDSet))
            .build();
      }
    }

    private Set<ResourceDefinition> resourceDefinitionSet = new HashSet<>();

    public static ResourceGroup getSystemDefaultResourceGroup(UUID customerUUID, Users user) {
      return getSystemDefaultResourceGroup(customerUUID, user.getUuid(), user.getRole());
    }

    public static ResourceGroup getSystemDefaultResourceGroup(
        UUID customerUUID, UUID userUUID, Users.Role usersRole) {
      ResourceGroup defaultResourceGroup = new ResourceGroup();
      ResourceDefinition resourceDefinition;
      switch (usersRole) {
        case ConnectOnly:
          // For connect only role, user should have access to only his user profile,
          // nothing else.
          resourceDefinition =
              ResourceDefinition.builder()
                  .resourceType(PermissionInfo.ResourceType.USER)
                  .allowAll(false)
                  .resourceUUIDSet(new HashSet<>(Arrays.asList(userUUID)))
                  .build();
          defaultResourceGroup.resourceDefinitionSet.add(resourceDefinition);
          break;
        default:
          defaultResourceGroup.resourceDefinitionSet.addAll(
              getResourceDefinitionsForNonConnectOnlyRoles(customerUUID));
          break;
      }
      return defaultResourceGroup;
    }

    public static ResourceGroup getSystemDefaultResourceGroup(
        GroupMappingInfo info, Users.Role usersRole) {
      ResourceGroup defaultResourceGroup = new ResourceGroup();
      switch (usersRole) {
          // Resource group for ConnectOnly role is user specific as it requires userUUID.
          // ConnectOnly role binding will be added for all group members upon login, so it
          // can be skipped for groups.
        case ConnectOnly:
          return null;
        default:
          defaultResourceGroup.resourceDefinitionSet.addAll(
              getResourceDefinitionsForNonConnectOnlyRoles(info.getCustomerUUID()));
      }
      return defaultResourceGroup;
    }

    /**
     * Get list of Resource Definitions which apply to all system roles except ConnectOnly.
     *
     * @param customerUUID
     * @return
     */
    public static List<ResourceDefinition> getResourceDefinitionsForNonConnectOnlyRoles(
        UUID customerUUID) {
      List<ResourceDefinition> list = new ArrayList<>();
      ResourceDefinition resourceDefinition;
      // For all other built-in roles, we can default to all resource types in the
      // resource group.
      for (PermissionInfo.ResourceType resourceType : PermissionInfo.ResourceType.values()) {
        if (resourceType.equals(PermissionInfo.ResourceType.OTHER)) {
          resourceDefinition =
              ResourceDefinition.builder()
                  .resourceType(resourceType)
                  .allowAll(false)
                  .resourceUUIDSet(new HashSet<>(Arrays.asList(customerUUID)))
                  .build();
        } else {
          resourceDefinition =
              ResourceDefinition.builder().resourceType(resourceType).allowAll(true).build();
        }
        list.add(resourceDefinition);
      }
      return list;
    }
  }

  @Entity
  @Table(name = "universe")
  public static class Universe extends Model {

    public static final Finder<UUID, Universe> find = new Finder<UUID, Universe>(Universe.class) {};

    @Id public UUID universeUUID;

    public String name;

    @Column(columnDefinition = "TEXT", nullable = false)
    private String universeDetailsJson;

    public static Set<UUID> getAllUUIDs(Customer customer) {
      List<UUID> universeList = find.query().where().eq("customer_id", customer.getId()).findIds();
      Set<UUID> universeUUIDs = new HashSet<UUID>(universeList);
      return universeUUIDs;
    }
  }
}
