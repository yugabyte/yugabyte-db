package com.yugabyte.yw.models.rbac;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_ONLY;
import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.Users;
import io.ebean.Finder;
import io.ebean.Model;
import io.ebean.annotation.DbJson;
import io.ebean.annotation.EnumValue;
import io.ebean.annotation.WhenCreated;
import io.ebean.annotation.WhenModified;
import io.swagger.annotations.ApiModelProperty;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.EnumType;
import javax.persistence.Enumerated;
import javax.persistence.Id;
import javax.persistence.JoinColumn;
import javax.persistence.ManyToOne;
import lombok.AllArgsConstructor;
import lombok.Getter;
import lombok.NoArgsConstructor;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;
import play.data.validation.Constraints;

@Slf4j
@Entity
@AllArgsConstructor
@NoArgsConstructor
@Getter
@Setter
public class RoleBinding extends Model {

  @ApiModelProperty(value = "UUID", accessMode = READ_ONLY)
  @Constraints.Required
  @Id
  @Column(name = "uuid", nullable = false, unique = true)
  private UUID uuid;

  @ManyToOne
  @ApiModelProperty(value = "User")
  @JoinColumn(name = "user_uuid", referencedColumnName = "uuid")
  private Users user;

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
    RoleBinding roleBinding =
        new RoleBinding(
            UUID.randomUUID(), user, roleBindingType, role, new Date(), new Date(), resourceGroup);
    roleBinding.save();
    return roleBinding;
  }

  public static RoleBinding get(UUID roleBindingUUID) {
    return find.query().where().eq("uuid", roleBindingUUID).findOne();
  }

  public static RoleBinding getOrBadRequest(UUID roleBindingUUID) {
    RoleBinding roleBinding = get(roleBindingUUID);
    if (roleBinding == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Invalid RoleBinding UUID:" + roleBindingUUID);
    }
    return roleBinding;
  }

  public static List<RoleBinding> getAll(UUID userUUID) {
    return find.query().where().eq("user_uuid", userUUID).findList();
  }

  public void edit(Role role, ResourceGroup resourceGroup) {
    this.role = role;
    this.resourceGroup = resourceGroup;
    this.update();
  }
}
