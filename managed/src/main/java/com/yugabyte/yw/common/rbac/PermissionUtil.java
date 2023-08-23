// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.common.rbac;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.models.Users;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Set;
import lombok.extern.slf4j.Slf4j;
import play.Environment;

@Slf4j
@Singleton
public class PermissionUtil {

  private final Environment environment;
  public static final String SYSTEM_ROLES_PERMISSIONS_FILE = "rbac/system_roles_permissions.json";

  @Inject
  public PermissionUtil(Environment environment) {
    this.environment = environment;
  }

  public List<PermissionInfo> getAllPermissionInfo() {
    List<PermissionInfo> permissionInfoList = new ArrayList<>();
    for (ResourceType resourceType : ResourceType.values()) {
      permissionInfoList.addAll(getAllPermissionInfo(resourceType));
    }
    return permissionInfoList;
  }

  public List<PermissionInfo> getAllPermissionInfo(ResourceType resourceType) {
    try {
      ObjectMapper mapper = new ObjectMapper();
      return mapper.readValue(
          environment.resourceAsStream(resourceType.getPermissionFilePath()),
          new TypeReference<List<PermissionInfo>>() {});
    } catch (IOException e) {
      e.printStackTrace();
      return Collections.emptyList();
    }
  }

  public PermissionInfo getPermissionInfo(ResourceType resourceType, Action action) {
    List<PermissionInfo> allPermissionInfo = getAllPermissionInfo(resourceType);
    return allPermissionInfo.stream()
        .filter(PermissionInfo -> PermissionInfo.action.equals(action))
        .findFirst()
        .orElse(null);
  }

  public PermissionInfo getPermissionInfo(Permission permission) {
    return getPermissionInfo(permission.getResourceType(), permission.getAction());
  }

  public void validatePermissionList(Set<Permission> permissionList)
      throws PlatformServiceException {
    for (Permission permission : permissionList) {
      Set<Permission> prerequisitePermissions =
          getPermissionInfo(permission).getPrerequisitePermissions();
      for (Permission prerequisitePermission : prerequisitePermissions) {
        if (!permissionList.contains(prerequisitePermission)) {
          String errMsg =
              String.format(
                  "Permissions list given is not valid. "
                      + "Ensure all prerequisite permissions are given. "
                      + "Given permission list = %s missed prerequisite permission = %s.",
                  permissionList, prerequisitePermission);
          log.error(errMsg);
          throw new PlatformServiceException(BAD_REQUEST, errMsg);
        }
      }
    }
  }

  public Set<Permission> getSystemRolePermissions(Users.Role userRole) {
    try {
      ObjectMapper mapper = new ObjectMapper();
      Map<String, Set<Permission>> systemRolePermissionMap =
          mapper.readValue(
              environment.resourceAsStream(SYSTEM_ROLES_PERMISSIONS_FILE),
              new TypeReference<Map<String, Set<Permission>>>() {});

      return systemRolePermissionMap.get(userRole.name());
    } catch (IOException e) {
      e.printStackTrace();
      return Collections.emptySet();
    }
  }
}
