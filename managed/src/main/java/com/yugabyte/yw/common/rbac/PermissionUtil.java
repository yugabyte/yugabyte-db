package com.yugabyte.yw.common.rbac;

import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.rbac.PermissionInfo.Permission;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import java.io.IOException;
import java.util.List;
import lombok.extern.slf4j.Slf4j;
import play.Environment;

@Slf4j
@Singleton
public class PermissionUtil {

  private final Environment environment;

  @Inject
  public PermissionUtil(Environment environment) {
    this.environment = environment;
  }

  public List<PermissionInfo> getAllPermissionInfo(ResourceType resourceType) {
    try {
      ObjectMapper mapper = new ObjectMapper();
      return mapper.readValue(
          environment.resourceAsStream(resourceType.getPermissionFilePath()),
          new TypeReference<List<PermissionInfo>>() {});
    } catch (IOException e) {
      e.printStackTrace();
      return null;
    }
  }

  public PermissionInfo getPermissionInfo(ResourceType resourceType, Permission permission) {
    List<PermissionInfo> allPermissionInfo = getAllPermissionInfo(resourceType);
    return allPermissionInfo.stream()
        .filter(PermissionInfo -> PermissionInfo.permission.equals(permission))
        .findFirst()
        .orElse(null);
  }

  public PermissionInfo getPermissionInfo(PermissionInfoIdentifier permissionInfoIdentifier) {
    return getPermissionInfo(
        permissionInfoIdentifier.getResourceType(), permissionInfoIdentifier.getPermission());
  }
}
