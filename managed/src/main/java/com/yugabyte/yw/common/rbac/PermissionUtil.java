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
import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import play.Environment;

@Slf4j
@Singleton
public class PermissionUtil {

  private final Environment environment;
  // In memory map to reduce disk lookup of metadata from static files, acts like a cache.
  public Map<Permission, PermissionInfo> permissionInfoMap = new HashMap<>();

  @Inject
  public PermissionUtil(Environment environment) {
    this.environment = environment;

    // Populate all the permissions and their metadata objects in permissionInfoMap when initiating
    // singleton util class.
    List<PermissionInfo> allPermissionInfos = getAllPermissionInfoFromDisk();
    for (PermissionInfo permissionInfo : allPermissionInfos) {
      Permission permission =
          new Permission(permissionInfo.getResourceType(), permissionInfo.getAction());
      this.permissionInfoMap.put(permission, permissionInfo);
    }
  }

  public List<PermissionInfo> getAllPermissionInfoFromDisk() {
    List<PermissionInfo> permissionInfoList = new ArrayList<>();
    for (ResourceType resourceType : ResourceType.values()) {
      permissionInfoList.addAll(getAllPermissionInfoFromDisk(resourceType));
    }
    return permissionInfoList;
  }

  public List<PermissionInfo> getAllPermissionInfoFromDisk(ResourceType resourceType) {
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

  public List<PermissionInfo> getAllPermissionInfoFromCache() {
    return new ArrayList<PermissionInfo>(permissionInfoMap.values());
  }

  public List<PermissionInfo> getAllPermissionInfoFromCache(ResourceType resourceType) {
    return this.permissionInfoMap.values().stream()
        .filter(pi -> resourceType.equals(pi.getResourceType()))
        .collect(Collectors.toList());
  }

  public PermissionInfo getPermissionInfo(ResourceType resourceType, Action action) {
    return this.permissionInfoMap.get(new Permission(resourceType, action));
  }

  public PermissionInfo getPermissionInfo(Permission permission) {
    return this.permissionInfoMap.get(permission);
  }

  public void validatePermissionList(Set<Permission> permissionList)
      throws PlatformServiceException {
    // Validate if all the permissions are valid.
    for (Permission permission : permissionList) {
      if (getPermissionInfo(permission) == null) {
        String errorMsg = String.format("Permission '%s' is not valid.", permission);
        log.error(errorMsg);
        throw new PlatformServiceException(BAD_REQUEST, errorMsg);
      }
    }

    // Ensure that the given permission list does not contain super admin actions.
    if (permissionList.contains(new Permission(ResourceType.OTHER, Action.SUPER_ADMIN_ACTIONS))) {
      String errorMsg = "Super admin actions not allowed in custom role.";
      log.error(errorMsg);
      throw new PlatformServiceException(BAD_REQUEST, errorMsg);
    }

    // Validate if all the prerequisite permissions are given.
    Set<Permission> missingPermissions = new HashSet<>();
    for (Permission permission : permissionList) {
      Set<Permission> prerequisitePermissions =
          getPermissionInfo(permission).getPrerequisitePermissions();
      for (Permission prerequisitePermission : prerequisitePermissions) {
        if (!permissionList.contains(prerequisitePermission)) {
          // Keep track of the missing prerequisite permission.
          missingPermissions.add(prerequisitePermission);
        }
      }
    }
    if (!missingPermissions.isEmpty()) {
      String errMsg =
          String.format(
              "Permissions list given is not valid. "
                  + "Ensure all prerequisite permissions are given. "
                  + "Given permission list = %s, missed prerequisite permissions = %s.",
              permissionList, missingPermissions);
      log.error(errMsg);
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }
  }
}
