// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.models;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.rbac.Permission;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.models.rbac.Role;
import com.yugabyte.yw.models.rbac.Role.RoleType;
import io.ebean.DuplicateKeyException;
import java.util.Arrays;
import java.util.HashSet;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class RoleTest extends FakeDBApplication {

  private Customer customer;
  public Permission permission1 = new Permission(ResourceType.UNIVERSE, Action.READ);
  public Permission permission2 = new Permission(ResourceType.OTHER, Action.READ);

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer("tc1", "Test Customer 1");
  }

  @Test
  public void testCreate() {
    Role role =
        Role.create(
            customer.getUuid(),
            "FakeRole1",
            "testDescription",
            RoleType.Custom,
            new HashSet<>(Arrays.asList(permission1)));
    assertNotNull(role.getRoleUUID());
    assertEquals("FakeRole1", role.getName());
    assertNotNull(role.getCreatedOn());
    assertEquals(1, role.getPermissionDetails().getPermissionList().size());
    for (Permission info : role.getPermissionDetails().getPermissionList()) {
      assertTrue(info.getAction().equals(Action.READ));
      assertTrue(info.getResourceType().equals(ResourceType.UNIVERSE));
    }
  }

  @Test
  public void testRoleNameUnique() {
    // Create role with name FakeRole1 in customer.
    Role role1 =
        Role.create(
            customer.getUuid(),
            "FakeRole1",
            "testDescription",
            RoleType.Custom,
            new HashSet<>(Arrays.asList(permission1)));
    assertNotNull(role1.getRoleUUID());
    assertEquals("FakeRole1", role1.getName());
    assertNotNull(role1.getCreatedOn());
    assertEquals(1, role1.getPermissionDetails().getPermissionList().size());

    // Try to create another role with same name "FakeRole1" in the same customer.
    // Should fail due to unique constraint.
    assertThrows(
        DuplicateKeyException.class,
        () -> {
          Role role2 =
              Role.create(
                  customer.getUuid(),
                  "FakeRole1",
                  "testDescription2",
                  RoleType.Custom,
                  new HashSet<>(Arrays.asList(permission2)));
        });
  }
}
