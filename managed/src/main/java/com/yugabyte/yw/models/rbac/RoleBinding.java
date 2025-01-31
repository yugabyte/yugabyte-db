// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.models.rbac;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.GroupMappingInfo;
import com.yugabyte.yw.models.Principal;
import com.yugabyte.yw.models.Principal.PrincipalType;
import com.yugabyte.yw.models.Users;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.Cache;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.EnumValue;
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
import jakarta.persistence.Transient;
import java.util.Collections;
import java.util.Date;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import lombok.AllArgsConstructor;
import lombok.Getter;
import lombok.NoArgsConstructor;
import lombok.Setter;
import lombok.ToString;
import lombok.extern.slf4j.Slf4j;
import play.data.validation.Constraints;

@Slf4j
@Entity
@AllArgsConstructor
@NoArgsConstructor
@Getter
@Setter
@ToString
@Cache(enableQueryCache = true)
public class RoleBinding extends Model {

  @ApiModelProperty(value = "UUID", accessMode = READ_ONLY)
  @Constraints.Required
  @Id
  @Column(name = "uuid", nullable = false, unique = true)
  private UUID uuid;

  @ManyToOne
  @JoinColumn(name = "principal_uuid", referencedColumnName = "uuid")
  private Principal principal;

  @ApiModelProperty(value = "User")
  @Transient
  private Users user;

  @ApiModelProperty(value = "GroupInfo")
  @Transient
  private GroupMappingInfo groupInfo;

  /**
   * This shows whether the role binding is system generated or user generated. System generated
   * role bindings are usually for the LDAP group users. Custom role bindings are when the user sets
   * the role bindings usually through the UI.
   */
  public enum RoleBindingType {
    @EnumValue("System")
    System,

    @EnumValue("Custom")
    Custom
  }

  @Enumerated(EnumType.STRING)
  @Column(name = "type", nullable = false)
  @ApiModelProperty(value = "Role binding type")
  private RoleBindingType type;

  @ManyToOne
  @ApiModelProperty(value = "Role")
  @JoinColumn(name = "role_uuid", referencedColumnName = "role_uuid")
  private Role role;

  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  @Column(name = "create_time", nullable = false)
  @ApiModelProperty(value = "RoleBinding create time", example = "2022-12-12T13:07:18Z")
  @WhenCreated
  private Date createTime;

  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  @Column(name = "update_time", nullable = false)
  @ApiModelProperty(value = "RoleBinding last updated time", example = "2022-12-12T13:07:18Z")
  @WhenModified
  private Date updateTime;

  @ApiModelProperty(value = "Details of the resource group", accessMode = READ_WRITE)
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

  public static RoleBinding get(UUID roleBindingUUID) {
    RoleBinding rb = find.byId(roleBindingUUID);
    populatePrincipalInfo(rb);
    return rb;
  }

  /**
   * Returns an unmodifiable list containing all the role bindings applicable for this user. This
   * includes the bindings of the groups this user is a part of. DO NOT modify the list or the state
   * of it contents. To get only the role bindings of a user use {@link #getAll(UUID)} instead.
   *
   * @param userUUID
   * @return UnmodifiableList containing all the role bindings appilicable for the user.
   */
  public static List<RoleBinding> fetchRoleBindingsForUser(UUID userUUID) {
    Users user = Users.getOrBadRequest(userUUID);
    List<RoleBinding> list = find.query().where().eq("principal_uuid", userUUID).findList();
    list.forEach(rb -> rb.setUser(user));
    Set<UUID> groupMemberships = user.getGroupMemberships();
    if (groupMemberships == null) {
      return Collections.unmodifiableList(list);
    }
    // Fetch role bindings for the user's groups.
    for (UUID groupUUID : groupMemberships) {
      Principal p = Principal.get(groupUUID);
      if (p == null) {
        continue;
      }
      list.addAll(getAll(groupUUID));
    }
    return Collections.unmodifiableList(list);
  }

  public static RoleBinding getOrBadRequest(UUID roleBindingUUID) {
    RoleBinding roleBinding = get(roleBindingUUID);
    if (roleBinding == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Invalid RoleBinding UUID:" + roleBindingUUID);
    }
    return roleBinding;
  }

  public static List<RoleBinding> getAll() {
    List<RoleBinding> list = find.query().findList();
    list.forEach(rb -> populatePrincipalInfo(rb));
    return list;
  }

  public static List<RoleBinding> getAll(UUID principalUUID) {
    Principal principal = Principal.getOrBadRequest(principalUUID);
    List<RoleBinding> list = find.query().where().eq("principal_uuid", principalUUID).findList();
    if (principal.getType().equals(PrincipalType.USER)) {
      Users user = Users.getOrBadRequest(principalUUID);
      list.forEach(rb -> rb.setUser(user));
    } else {
      GroupMappingInfo info = GroupMappingInfo.getOrBadRequest(principalUUID);
      list.forEach(rb -> rb.setGroupInfo(info));
    }
    return list;
  }

  public static List<RoleBinding> getAllWithRole(UUID roleUUID) {
    List<RoleBinding> rbList = find.query().where().eq("role_uuid", roleUUID).findList();
    rbList.forEach(rb -> populatePrincipalInfo(rb));
    return rbList;
  }

  public static boolean checkUserHasRole(UUID userUUID, UUID roleUUID) {
    return find.query().where().eq("principal_uuid", userUUID).eq("role_uuid", roleUUID).exists();
  }

  public void edit(Role role, ResourceGroup resourceGroup) {
    this.role = role;
    this.resourceGroup = resourceGroup;
    this.update();
  }

  /**
   * If the role binding belongs to a user, we populate the user field else the groupInfo field.
   *
   * @param rb
   */
  private static void populatePrincipalInfo(RoleBinding rb) {
    if (rb == null) {
      return;
    }
    Principal principal = Principal.getOrBadRequest(rb.getPrincipal().getUuid());
    if (principal.getType().equals(PrincipalType.USER)) {
      rb.setUser(Users.getOrBadRequest(principal.getUserUUID()));
    } else {
      rb.setGroupInfo(GroupMappingInfo.getOrBadRequest(principal.getGroupUUID()));
    }
  }
}
