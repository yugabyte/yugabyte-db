// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.models;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.rbac.Permission;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.models.rbac.ResourceGroup;
import com.yugabyte.yw.models.rbac.ResourceGroup.ResourceDefinition;
import com.yugabyte.yw.models.rbac.Role;
import com.yugabyte.yw.models.rbac.Role.RoleType;
import com.yugabyte.yw.models.rbac.RoleBinding;
import com.yugabyte.yw.models.rbac.RoleBinding.RoleBindingType;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class RoleBindingTest extends FakeDBApplication {

  private Customer customer;
  private Users user;
  private Role role;
  private ResourceGroup rg1;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer("tc1", "Test Customer 1");
    user = ModelFactory.testUser(customer);
    role =
        Role.create(
            customer.getUuid(),
            "FakeRole1",
            "testDescription",
            RoleType.Custom,
            new HashSet<>(
                Arrays.asList(
                    new Permission(ResourceType.UNIVERSE, Action.CREATE),
                    new Permission(ResourceType.UNIVERSE, Action.READ),
                    new Permission(ResourceType.UNIVERSE, Action.UPDATE))));
    ResourceDefinition rd1 =
        ResourceDefinition.builder().resourceType(ResourceType.UNIVERSE).allowAll(true).build();
    rg1 = new ResourceGroup(new HashSet<>(Arrays.asList(rd1)));
  }

  @Test
  public void testCreate() {
    RoleBinding roleBinding = RoleBinding.create(user, RoleBindingType.Custom, role, rg1);
    assertNotNull(roleBinding.getUuid());
    assertNotNull(roleBinding.getCreateTime());
    assertEquals(1, roleBinding.getResourceGroup().getResourceDefinitionSet().size());
  }

  @Test
  public void testEdit() {
    RoleBinding roleBinding = RoleBinding.create(user, RoleBindingType.Custom, role, rg1);
    assertNotNull(roleBinding.getUuid());
    assertNotNull(roleBinding.getCreateTime());
    assertEquals(1, roleBinding.getResourceGroup().getResourceDefinitionSet().size());
    assertTrue(roleBinding.getRole().getRoleUUID().equals(role.getRoleUUID()));
    assertTrue(roleBinding.getRole().getRoleType().equals(role.getRoleType()));
    Set<Action> permissions =
        roleBinding.getRole().getPermissionDetails().getPermissionList().stream()
            .map(i -> i.getAction())
            .collect(Collectors.toSet());
    assertTrue(permissions.contains(Action.CREATE));
    assertTrue(permissions.contains(Action.READ));
    assertTrue(permissions.contains(Action.UPDATE));
    for (Permission info : roleBinding.getRole().getPermissionDetails().getPermissionList()) {
      assertTrue(info.getResourceType().equals(ResourceType.UNIVERSE));
    }
    for (ResourceDefinition definition :
        roleBinding.getResourceGroup().getResourceDefinitionSet()) {
      assertTrue(definition.getResourceType().equals(ResourceType.UNIVERSE));
      assertTrue(definition.isAllowAll());
      assertEquals(0, definition.getResourceUUIDSet().size());
    }

    Role role2 =
        Role.create(
            customer.getUuid(),
            "FakeRole2",
            "testDescription",
            RoleType.Custom,
            new HashSet<>(
                Arrays.asList(
                    new Permission(ResourceType.UNIVERSE, Action.CREATE),
                    new Permission(ResourceType.UNIVERSE, Action.READ))));
    roleBinding.edit(role2, rg1);
    assertNotNull(roleBinding.getUuid());
    assertNotNull(roleBinding.getCreateTime());
    assertEquals(1, roleBinding.getResourceGroup().getResourceDefinitionSet().size());
    assertTrue(roleBinding.getRole().getRoleUUID().equals(role2.getRoleUUID()));
    assertTrue(roleBinding.getRole().getRoleType().equals(role2.getRoleType()));
    permissions =
        roleBinding.getRole().getPermissionDetails().getPermissionList().stream()
            .map(i -> i.getAction())
            .collect(Collectors.toSet());
    assertTrue(permissions.contains(Action.CREATE));
    assertTrue(permissions.contains(Action.READ));
    for (Permission info : roleBinding.getRole().getPermissionDetails().getPermissionList()) {
      assertTrue(info.getResourceType().equals(ResourceType.UNIVERSE));
    }
    for (ResourceDefinition definition :
        roleBinding.getResourceGroup().getResourceDefinitionSet()) {
      assertTrue(definition.getResourceType().equals(ResourceType.UNIVERSE));
      assertTrue(definition.isAllowAll());
      assertEquals(0, definition.getResourceUUIDSet().size());
    }
  }
}
