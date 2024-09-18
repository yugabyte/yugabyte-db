// Copyright (c) Yugabyte, Inc.

package api.v2.handlers;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.NOT_FOUND;

import api.v2.mappers.RoleResourceDefinitionMapper;
import api.v2.models.AuthGroupToRolesMapping;
import api.v2.models.AuthGroupToRolesMapping.TypeEnum;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.rbac.RoleBindingUtil;
import com.yugabyte.yw.common.rbac.RoleResourceDefinition;
import com.yugabyte.yw.controllers.TokenAuthenticator;
import com.yugabyte.yw.models.GroupMappingInfo;
import com.yugabyte.yw.models.GroupMappingInfo.GroupType;
import com.yugabyte.yw.models.rbac.Role;
import com.yugabyte.yw.models.rbac.Role.RoleType;
import com.yugabyte.yw.models.rbac.RoleBinding;
import com.yugabyte.yw.models.rbac.RoleBinding.RoleBindingType;
import io.ebean.annotation.Transactional;
import java.time.ZoneOffset;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Http;

@Slf4j
@Singleton
public class AuthenticationHandler {

  @Inject RuntimeConfGetter confGetter;
  @Inject TokenAuthenticator tokenAuthenticator;
  @Inject RoleBindingUtil roleBindingUtil;

  public List<AuthGroupToRolesMapping> listMappings(UUID cUUID) throws Exception {

    checkRuntimeConfig();

    List<GroupMappingInfo> groupInfoList =
        GroupMappingInfo.find.query().where().eq("customer_uuid", cUUID).findList();
    List<AuthGroupToRolesMapping> groupMappingList = new ArrayList<AuthGroupToRolesMapping>();
    for (GroupMappingInfo info : groupInfoList) {
      AuthGroupToRolesMapping groupMapping =
          new AuthGroupToRolesMapping()
              .groupIdentifier(info.getIdentifier())
              .type(TypeEnum.valueOf(info.getType().toString()))
              .creationDate(info.getCreationDate().toInstant().atOffset(ZoneOffset.UTC))
              .uuid(info.getGroupUUID());

      List<RoleResourceDefinition> roleResourceDefinitions = new ArrayList<>();
      RoleResourceDefinition rrd = new RoleResourceDefinition();
      rrd.setRoleUUID(info.getRoleUUID());
      // Fetch all role rolebindings for the current group if RBAC is on.
      if (confGetter.getGlobalConf(GlobalConfKeys.useNewRbacAuthz)) {
        if (Role.get(cUUID, "ConnectOnly").getRoleUUID().equals(info.getRoleUUID())) {
          roleResourceDefinitions.add(rrd);
        }
        List<RoleBinding> roleBindingList = RoleBinding.getAll(info.getGroupUUID());
        for (RoleBinding rb : roleBindingList) {
          RoleResourceDefinition roleResourceDefinition =
              new RoleResourceDefinition(rb.getRole().getRoleUUID(), rb.getResourceGroup());
          roleResourceDefinitions.add(roleResourceDefinition);
        }
      } else {
        roleResourceDefinitions.add(rrd);
      }
      groupMapping.setRoleResourceDefinitions(
          RoleResourceDefinitionMapper.INSTANCE.toV2RoleResourceDefinitionList(
              roleResourceDefinitions));
      groupMappingList.add(groupMapping);
    }
    return groupMappingList;
  }

  @Transactional
  public void updateGroupMappings(
      Http.Request request, UUID cUUID, List<AuthGroupToRolesMapping> AuthGroupToRolesMapping) {
    boolean isSuperAdmin = tokenAuthenticator.superAdminAuthentication(request);
    if (!isSuperAdmin) {
      throw new PlatformServiceException(BAD_REQUEST, "Only SuperAdmin can create group mappings!");
    }

    checkRuntimeConfig();

    for (AuthGroupToRolesMapping mapping : AuthGroupToRolesMapping) {
      GroupMappingInfo mappingInfo =
          GroupMappingInfo.find
              .query()
              .where()
              .eq("customer_uuid", cUUID)
              .ieq("identifier", mapping.getGroupIdentifier())
              .findOne();

      if (mappingInfo == null) {
        // new entry for new group
        log.info("Adding new group mapping entry for group: " + mapping.getGroupIdentifier());
        mappingInfo =
            GroupMappingInfo.create(
                cUUID,
                mapping.getGroupIdentifier(),
                GroupType.valueOf(mapping.getType().toString()));
      } else {
        // clear role bindings for existing group
        RoleBindingUtil.clearRoleBindingsForGroup(mappingInfo);
      }

      List<RoleResourceDefinition> roleResourceDefinitions =
          RoleResourceDefinitionMapper.INSTANCE.toV1RoleResourceDefinitionList(
              mapping.getRoleResourceDefinitions());

      roleBindingUtil.validateRoles(cUUID, roleResourceDefinitions);
      roleBindingUtil.validateResourceGroups(cUUID, roleResourceDefinitions);

      // Add role bindings if rbac is on.
      if (confGetter.getGlobalConf(GlobalConfKeys.useNewRbacAuthz)) {
        for (RoleResourceDefinition rrd : roleResourceDefinitions) {
          Role rbacRole = Role.getOrBadRequest(cUUID, rrd.getRoleUUID());
          // Resource group is null for system roles, so need to populate that before
          // adding role binding.
          if (rbacRole.getRoleType().equals(RoleType.Custom)) {
            RoleBinding.create(
                mappingInfo, RoleBindingType.Custom, rbacRole, rrd.getResourceGroup());
          } else {
            RoleBindingUtil.createSystemRoleBindingsForGroup(mappingInfo, rbacRole);
          }
        }
        // This role will be ignored when rbac is on.
        mappingInfo.setRoleUUID(Role.get(cUUID, "ConnectOnly").getRoleUUID());
      } else {
        validate(roleResourceDefinitions);
        mappingInfo.setRoleUUID(roleResourceDefinitions.get(0).getRoleUUID());
      }
      mappingInfo.save();
    }
  }

  @Transactional
  public void deleteGroupMappings(Http.Request request, UUID cUUID, UUID gUUID) {
    boolean isSuperAdmin = tokenAuthenticator.superAdminAuthentication(request);
    if (!isSuperAdmin) {
      throw new PlatformServiceException(BAD_REQUEST, "Only SuperAdmin can delete group mappings!");
    }

    checkRuntimeConfig();

    GroupMappingInfo entity =
        GroupMappingInfo.find
            .query()
            .where()
            .eq("customer_uuid", cUUID)
            .eq("uuid", gUUID)
            .findOne();
    if (entity == null) {
      throw new PlatformServiceException(NOT_FOUND, "No group mapping found with uuid: " + gUUID);
    }

    log.info("Deleting Group Mapping with name: " + entity.getIdentifier());
    entity.delete();
  }

  private void checkRuntimeConfig() {
    if (!confGetter.getGlobalConf(GlobalConfKeys.groupMappingRbac)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "yb.security.group_mapping_rbac_support runtime config is disabled!");
    }
  }

  /**
   * Validation to make sure only a single system role is present when RBAC is off.
   *
   * @param cUUID
   * @param rrdList
   */
  private void validate(List<RoleResourceDefinition> rrdList) {
    if (rrdList.size() != 1) {
      throw new PlatformServiceException(BAD_REQUEST, "Need to specify a single system role!");
    }
  }
}
