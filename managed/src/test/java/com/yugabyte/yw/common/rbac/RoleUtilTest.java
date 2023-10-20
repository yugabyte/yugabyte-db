// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.common.rbac;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.rbac.ResourceGroup;
import com.yugabyte.yw.models.rbac.ResourceGroup.ResourceDefinition;
import com.yugabyte.yw.models.rbac.Role;
import com.yugabyte.yw.models.rbac.Role.RoleType;
import com.yugabyte.yw.models.rbac.RoleBinding;
import com.yugabyte.yw.models.rbac.RoleBinding.RoleBindingType;
import java.io.File;
import java.util.Arrays;
import java.util.HashSet;
import junitparams.JUnitParamsRunner;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Spy;
import play.Environment;
import play.Mode;

@RunWith(JUnitParamsRunner.class)
public class RoleUtilTest extends FakeDBApplication {
  @Spy Environment environment;
  PermissionUtil permissionUtil;
  RoleUtil roleUtil;
  RoleBindingUtil roleBindingUtil;
  RuntimeConfGetter confGetter;
  public Customer customer;
  public Permission permission1 = new Permission(ResourceType.UNIVERSE, Action.CREATE);
  public Permission permission2 = new Permission(ResourceType.OTHER, Action.CREATE);

  @Before
  public void setup() {
    customer = ModelFactory.testCustomer("tc1", "Test Customer 1");
    ClassLoader classLoader = Thread.currentThread().getContextClassLoader();
    this.environment = new Environment(new File("."), classLoader, Mode.TEST);
    confGetter = app.injector().instanceOf(RuntimeConfGetter.class);
    this.permissionUtil = new PermissionUtil(environment);
    this.roleBindingUtil = new RoleBindingUtil(permissionUtil, confGetter);
    this.roleUtil = new RoleUtil(permissionUtil, roleBindingUtil);
  }

  @Test
  public void testCreateValidRole() {
    Role role =
        roleUtil.createRole(
            customer.getUuid(),
            "FakeRole1",
            "FakeRoleDescription1",
            RoleType.Custom,
            new HashSet<>(
                Arrays.asList(
                    new Permission(ResourceType.UNIVERSE, Action.CREATE),
                    new Permission(ResourceType.OTHER, Action.READ),
                    new Permission(ResourceType.UNIVERSE, Action.READ),
                    new Permission(ResourceType.UNIVERSE, Action.UPDATE))));
    assertNotNull(role.getRoleUUID());
    assertEquals("FakeRole1", role.getName());
    assertEquals(4, role.getPermissionDetails().getPermissionList().size());
    assertNotNull(role.getCreatedOn());
  }

  @Test(expected = PlatformServiceException.class)
  public void testCreateInvalidRole() {
    // This is expected to fail because UNIVERSE.READ is a prerequisite of other permissions.
    Role role =
        roleUtil.createRole(
            customer.getUuid(),
            "FakeRole1",
            "FakeRoleDescription1",
            RoleType.Custom,
            new HashSet<>(
                Arrays.asList(
                    new Permission(ResourceType.UNIVERSE, Action.CREATE),
                    new Permission(ResourceType.OTHER, Action.READ),
                    new Permission(ResourceType.UNIVERSE, Action.UPDATE),
                    new Permission(ResourceType.UNIVERSE, Action.DELETE))));
  }

  @Test
  public void testEditValidRole() {
    Role role =
        roleUtil.createRole(
            customer.getUuid(),
            "FakeRole1",
            "FakeRoleDescription1",
            RoleType.Custom,
            new HashSet<>(
                Arrays.asList(
                    new Permission(ResourceType.UNIVERSE, Action.CREATE),
                    new Permission(ResourceType.OTHER, Action.READ),
                    new Permission(ResourceType.UNIVERSE, Action.READ),
                    new Permission(ResourceType.UNIVERSE, Action.UPDATE))));
    assertNotNull(role.getRoleUUID());
    assertEquals("FakeRole1", role.getName());
    assertEquals(4, role.getPermissionDetails().getPermissionList().size());
    assertNotNull(role.getCreatedOn());
    assertEquals(0, RoleBinding.getAll().size());

    roleUtil.editRole(
        customer.getUuid(),
        role.getRoleUUID(),
        null,
        new HashSet<>(
            Arrays.asList(
                new Permission(ResourceType.UNIVERSE, Action.DELETE),
                new Permission(ResourceType.UNIVERSE, Action.DELETE),
                new Permission(ResourceType.UNIVERSE, Action.READ))));
    Role roleUpdated = Role.getOrBadRequest(customer.getUuid(), role.getRoleUUID());
    assertEquals(2, roleUpdated.getPermissionDetails().getPermissionList().size());
    assertNotEquals(roleUpdated.getCreatedOn(), roleUpdated.getUpdatedOn());
    assertEquals(0, RoleBinding.getAll().size());
  }

  @Test(expected = PlatformServiceException.class)
  public void testEditInvalidRole() {
    Role role =
        roleUtil.createRole(
            customer.getUuid(),
            "FakeRole1",
            "FakeRoleDescription1",
            RoleType.Custom,
            new HashSet<>(
                Arrays.asList(
                    new Permission(ResourceType.UNIVERSE, Action.CREATE),
                    new Permission(ResourceType.OTHER, Action.READ),
                    new Permission(ResourceType.UNIVERSE, Action.READ),
                    new Permission(ResourceType.UNIVERSE, Action.UPDATE))));
    assertNotNull(role.getRoleUUID());
    assertEquals("FakeRole1", role.getName());
    assertEquals(4, role.getPermissionDetails().getPermissionList().size());
    assertNotNull(role.getCreatedOn());

    roleUtil.editRole(
        customer.getUuid(),
        role.getRoleUUID(),
        null,
        new HashSet<>(
            Arrays.asList(
                new Permission(ResourceType.UNIVERSE, Action.CREATE),
                new Permission(ResourceType.OTHER, Action.READ),
                new Permission(ResourceType.UNIVERSE, Action.DELETE))));
  }

  @Test
  public void testEditRoleWithRoleBindings() {
    // Create a customer role.
    Role role =
        roleUtil.createRole(
            customer.getUuid(),
            "FakeRole1",
            "FakeRoleDescription1",
            RoleType.Custom,
            new HashSet<>(
                Arrays.asList(
                    new Permission(ResourceType.UNIVERSE, Action.READ),
                    new Permission(ResourceType.UNIVERSE, Action.UPDATE))));
    assertNotNull(role.getRoleUUID());
    assertEquals(2, role.getPermissionDetails().getPermissionList().size());
    assertEquals(0, RoleBinding.getAll().size());

    // Create a user with role bindings.
    Users user = ModelFactory.testUser(customer);
    Universe universe = ModelFactory.createUniverse();
    ResourceDefinition rd1 =
        ResourceDefinition.builder()
            .resourceType(ResourceType.UNIVERSE)
            .resourceUUIDSet(new HashSet<>(Arrays.asList(universe.getUniverseUUID())))
            .build();
    ResourceGroup rg1 = new ResourceGroup(new HashSet<>(Arrays.asList(rd1)));
    RoleBinding roleBinding =
        roleBindingUtil.createRoleBinding(
            user.getUuid(), role.getRoleUUID(), RoleBindingType.Custom, rg1);
    assertEquals(rg1, RoleBinding.get(roleBinding.getUuid()).getResourceGroup());

    // Edit the role, assert the role bindings also expanded.
    roleUtil.editRole(
        customer.getUuid(),
        role.getRoleUUID(),
        null,
        new HashSet<>(
            Arrays.asList(
                new Permission(ResourceType.UNIVERSE, Action.CREATE),
                new Permission(ResourceType.OTHER, Action.READ),
                new Permission(ResourceType.UNIVERSE, Action.READ),
                new Permission(ResourceType.UNIVERSE, Action.UPDATE))));
    Role roleUpdated = Role.getOrBadRequest(customer.getUuid(), role.getRoleUUID());
    assertEquals(4, roleUpdated.getPermissionDetails().getPermissionList().size());
    assertEquals(1, RoleBinding.getAll().size());
    ResourceDefinition expectedRD1 =
        ResourceDefinition.builder().resourceType(ResourceType.UNIVERSE).allowAll(true).build();
    ResourceGroup expectedRG1 = new ResourceGroup(new HashSet<>(Arrays.asList(expectedRD1)));
    assertEquals(expectedRG1, RoleBinding.get(roleBinding.getUuid()).getResourceGroup());
  }
}
