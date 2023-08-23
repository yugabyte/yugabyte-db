// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.common.rbac;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;

import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import java.io.File;
import java.util.HashSet;
import java.util.List;
import junitparams.JUnitParamsRunner;
import lombok.extern.slf4j.Slf4j;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Spy;
import play.Environment;
import play.Mode;

@Slf4j
@RunWith(JUnitParamsRunner.class)
public class PermissionUtilTest {
  PermissionUtil permissionUtil;

  @Spy Environment environment;

  @Before
  public void setup() {
    ClassLoader classLoader = Thread.currentThread().getContextClassLoader();
    this.environment = new Environment(new File("."), classLoader, Mode.TEST);
    this.permissionUtil = new PermissionUtil(environment);
  }

  @Test
  public void testUniqueResourceTypePerFile() {
    // Check if all objects in a file have same resourceType.
    for (ResourceType resourceType : ResourceType.values()) {
      log.info("Testing unique resource type: " + resourceType.toString());
      List<PermissionInfo> allPermissionInfo = permissionUtil.getAllPermissionInfo(resourceType);
      for (PermissionInfo permissionInfo : allPermissionInfo) {
        assertEquals(resourceType, permissionInfo.resourceType);
      }
    }
  }

  @Test
  public void testUniquePermissionsInFile() {
    // All objects in a file should have unique permission.
    for (ResourceType resourceType : ResourceType.values()) {
      log.info("Testing unique permissions on : " + resourceType.toString());
      HashSet<Action> allPermissionsInFile = new HashSet<Action>();
      List<PermissionInfo> allPermissionInfo = permissionUtil.getAllPermissionInfo(resourceType);
      for (PermissionInfo permissionInfo : allPermissionInfo) {
        assertFalse(allPermissionsInFile.contains(permissionInfo.getAction()));
        allPermissionsInFile.add(permissionInfo.getAction());
      }
    }
  }

  @Test
  public void testUniquePrerequisitePermissionsInFile() {
    // All objects in a file should have unique prerequisite permissions.
    for (ResourceType resourceType : ResourceType.values()) {
      log.info("Testing unique prerequisite permissions on : " + resourceType.toString());
      List<PermissionInfo> allPermissionInfo = permissionUtil.getAllPermissionInfo(resourceType);
      for (PermissionInfo permissionInfo : allPermissionInfo) {
        // For each PermissionInfo object, traverse the list of prerequisite permissions.
        HashSet<Permission> allPrerequisitePermissionsInFile = new HashSet<Permission>();
        for (Permission permission : permissionInfo.getPrerequisitePermissions()) {
          assertFalse(allPrerequisitePermissionsInFile.contains(permission));
          allPrerequisitePermissionsInFile.add(permission);
        }
      }
    }
  }
}
