// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.models.rbac;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;
import static play.mvc.Http.Status.NOT_FOUND;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.rbac.Permission;
import com.yugabyte.yw.models.helpers.PermissionDetails;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.EnumValue;
import io.ebean.annotation.WhenCreated;
import io.ebean.annotation.WhenModified;
import io.swagger.annotations.ApiModelProperty;
import java.util.Date;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.EnumType;
import javax.persistence.Enumerated;
import javax.persistence.Id;
import lombok.AllArgsConstructor;
import lombok.Getter;
import lombok.NoArgsConstructor;
import lombok.Setter;
import lombok.ToString;
import lombok.extern.slf4j.Slf4j;

@Entity
@Getter
@AllArgsConstructor
@NoArgsConstructor
@ToString
@Slf4j
public class Role extends Model {

  @Id
  @Column(name = "role_uuid", nullable = false)
  @ApiModelProperty(value = "Role UUID", accessMode = READ_ONLY)
  private UUID roleUUID;

  @Column(name = "customer_uuid", nullable = false)
  @ApiModelProperty(value = "Customer UUID")
  private UUID customerUUID;

  @Column(name = "name", nullable = false)
  @ApiModelProperty(value = "Role name", accessMode = READ_WRITE)
  private String name;

  @Column(name = "description", nullable = false)
  @ApiModelProperty(value = "Role description")
  private String description;

  @Column(name = "created_on", nullable = false)
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  @ApiModelProperty(value = "Role create time", example = "2022-12-12T13:07:18Z")
  @WhenCreated
  private Date createdOn;

  @Setter
  @Column(name = "updated_on", nullable = false)
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  @ApiModelProperty(value = "Role last updated time", example = "2022-12-12T13:07:18Z")
  @WhenModified
  private Date updatedOn;

  /**
   * This shows whether the role is a built-in role or user created role. System roles are the
   * SuperAdmin, Admin, BackupAdmin, ReadOnly, ConnectOnly. Custom roles are the user created roles
   * with custom permissions.
   */
  public enum RoleType {
    @EnumValue("System")
    System,

    @EnumValue("Custom")
    Custom
  }

  @Enumerated(EnumType.STRING)
  @Column(name = "role_type", nullable = false)
  @ApiModelProperty(value = "Type of the role")
  private RoleType roleType;

  @Setter
  @DbJson
  @Column(name = "permission_details")
  @ApiModelProperty(value = "Permission details of the role")
  private PermissionDetails permissionDetails;

  public static final Finder<UUID, Role> find = new Finder<UUID, Role>(Role.class) {};

  public static Role create(
      UUID customerUUID,
      String name,
      String description,
      RoleType roleType,
      Set<Permission> permissionList) {
    Role role =
        new Role(
            UUID.randomUUID(),
            customerUUID,
            name,
            description,
            new Date(),
            new Date(),
            roleType,
            new PermissionDetails(permissionList));
    role.save();
    return role;
  }

  public void updateRole(String description, Set<Permission> permissionList) {
    if (description != null) {
      this.description = description;
    }
    if (permissionList != null) {
      this.permissionDetails.setPermissionList(permissionList);
    }
    this.update();
  }

  public static Role get(UUID customerUUID, UUID roleUUID) {
    return find.query()
        .where()
        .eq("customer_uuid", customerUUID)
        .eq("role_uuid", roleUUID)
        .findOne();
  }

  public static Role getOrBadRequest(UUID customerUUID, UUID roleUUID)
      throws PlatformServiceException {
    Role role = get(customerUUID, roleUUID);
    if (role == null) {
      throw new PlatformServiceException(
          NOT_FOUND,
          String.format("Invalid role UUID '%s' for customer '%s'.", roleUUID, customerUUID));
    }
    return role;
  }

  public static Role get(UUID customerUUID, String name) {
    return find.query().where().eq("customer_uuid", customerUUID).eq("name", name).findOne();
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

  public static List<Role> getAll(UUID customerUUID) {
    return find.query().where().eq("customer_uuid", customerUUID).findList();
  }

  public static List<Role> getAll(UUID customerUUID, RoleType roleType) {
    return find.query()
        .where()
        .eq("customer_uuid", customerUUID)
        .eq("role_type", roleType.toString())
        .findList();
  }
}
