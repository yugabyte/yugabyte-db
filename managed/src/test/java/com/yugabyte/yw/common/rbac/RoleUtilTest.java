package com.yugabyte.yw.common.rbac;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.rbac.PermissionInfo.Permission;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.rbac.Role;
import com.yugabyte.yw.models.rbac.Role.RoleType;
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
  public Customer customer;
  public PermissionInfoIdentifier permission1 =
      new PermissionInfoIdentifier(ResourceType.UNIVERSE, Permission.CREATE);
  public PermissionInfoIdentifier permission2 =
      new PermissionInfoIdentifier(ResourceType.DEFAULT, Permission.CREATE);

  @Before
  public void setup() {
    customer = ModelFactory.testCustomer("tc1", "Test Customer 1");
    ClassLoader classLoader = Thread.currentThread().getContextClassLoader();
    this.environment = new Environment(new File("."), classLoader, Mode.TEST);
    this.permissionUtil = new PermissionUtil(environment);
    this.roleUtil = new RoleUtil(new PermissionUtil(environment));
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
                    new PermissionInfoIdentifier(ResourceType.UNIVERSE, Permission.CREATE),
                    new PermissionInfoIdentifier(ResourceType.UNIVERSE, Permission.READ),
                    new PermissionInfoIdentifier(ResourceType.UNIVERSE, Permission.UPDATE))));
    assertNotNull(role.getRoleUUID());
    assertEquals("FakeRole1", role.getName());
    assertEquals(3, role.getPermissionDetails().getPermissionList().size());
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
                    new PermissionInfoIdentifier(ResourceType.UNIVERSE, Permission.CREATE),
                    new PermissionInfoIdentifier(ResourceType.UNIVERSE, Permission.UPDATE),
                    new PermissionInfoIdentifier(ResourceType.UNIVERSE, Permission.DELETE))));
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
                    new PermissionInfoIdentifier(ResourceType.UNIVERSE, Permission.CREATE),
                    new PermissionInfoIdentifier(ResourceType.UNIVERSE, Permission.READ),
                    new PermissionInfoIdentifier(ResourceType.UNIVERSE, Permission.UPDATE))));
    assertNotNull(role.getRoleUUID());
    assertEquals("FakeRole1", role.getName());
    assertEquals(3, role.getPermissionDetails().getPermissionList().size());
    assertNotNull(role.getCreatedOn());

    roleUtil.editRole(
        customer.getUuid(),
        role.getRoleUUID(),
        null,
        new HashSet<>(
            Arrays.asList(
                new PermissionInfoIdentifier(ResourceType.UNIVERSE, Permission.DELETE),
                new PermissionInfoIdentifier(ResourceType.UNIVERSE, Permission.DELETE),
                new PermissionInfoIdentifier(ResourceType.UNIVERSE, Permission.READ))));
    Role roleUpdated = Role.getOrBadRequest(customer.getUuid(), role.getRoleUUID());
    assertEquals(2, roleUpdated.getPermissionDetails().getPermissionList().size());
    assertNotEquals(roleUpdated.getCreatedOn(), roleUpdated.getUpdatedOn());
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
                    new PermissionInfoIdentifier(ResourceType.UNIVERSE, Permission.CREATE),
                    new PermissionInfoIdentifier(ResourceType.UNIVERSE, Permission.READ),
                    new PermissionInfoIdentifier(ResourceType.UNIVERSE, Permission.UPDATE))));
    assertNotNull(role.getRoleUUID());
    assertEquals("FakeRole1", role.getName());
    assertEquals(3, role.getPermissionDetails().getPermissionList().size());
    assertNotNull(role.getCreatedOn());

    roleUtil.editRole(
        customer.getUuid(),
        role.getRoleUUID(),
        null,
        new HashSet<>(
            Arrays.asList(
                new PermissionInfoIdentifier(ResourceType.UNIVERSE, Permission.CREATE),
                new PermissionInfoIdentifier(ResourceType.UNIVERSE, Permission.DELETE))));
  }
}
