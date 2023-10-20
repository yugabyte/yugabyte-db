// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.common.rbac;

import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.RuntimeConfigEntry;
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
import java.util.List;
import java.util.Set;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Spy;
import play.Environment;
import play.Mode;

@RunWith(JUnitParamsRunner.class)
public class RoleBindingUtilTest extends FakeDBApplication {

  @Spy Environment environment;
  PermissionUtil permissionUtil;
  private RoleBindingUtil roleBindingUtil;
  private Customer customer;
  private Universe universe1;
  private Universe universe2;
  private Users user;
  private Role role;
  private RuntimeConfGetter confGetter;

  @Before
  public void setup() {
    confGetter = app.injector().instanceOf(RuntimeConfGetter.class);
    ClassLoader classLoader = Thread.currentThread().getContextClassLoader();
    this.environment = new Environment(new File("."), classLoader, Mode.TEST);
    this.permissionUtil = new PermissionUtil(environment);

    roleBindingUtil = new RoleBindingUtil(permissionUtil, confGetter);
    customer = ModelFactory.testCustomer("tc1", "Test Customer 1");
    universe1 = ModelFactory.createUniverse("Test Universe 1", customer.getId());
    universe2 = ModelFactory.createUniverse("Test Universe 2", customer.getId());
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
  }

  @Test
  public void testCreateRoleBinding() {
    ResourceDefinition rd1 =
        ResourceDefinition.builder().resourceType(ResourceType.UNIVERSE).allowAll(true).build();
    ResourceGroup rg1 = new ResourceGroup(new HashSet<>(Arrays.asList(rd1)));
    RoleBinding roleBinding =
        roleBindingUtil.createRoleBinding(
            user.getUuid(), role.getRoleUUID(), RoleBindingType.Custom, rg1);
    assertNotNull(roleBinding.getUuid());
    assertEquals(1, roleBinding.getResourceGroup().getResourceDefinitionSet().size());
    assertNotNull(roleBinding.getCreateTime());
  }

  @Test(expected = PlatformServiceException.class)
  public void testCreateRoleBindingWithInvalidRole() {
    ResourceDefinition rd1 =
        ResourceDefinition.builder().resourceType(ResourceType.UNIVERSE).allowAll(true).build();
    ResourceGroup rg1 = new ResourceGroup(new HashSet<>(Arrays.asList(rd1)));
    RoleBinding roleBinding =
        roleBindingUtil.createRoleBinding(
            user.getUuid(), UUID.randomUUID(), RoleBindingType.Custom, rg1);
  }

  @Test(expected = PlatformServiceException.class)
  public void testCreateRoleBindingWithInvalidUser() {
    ResourceDefinition rd1 =
        ResourceDefinition.builder().resourceType(ResourceType.UNIVERSE).allowAll(true).build();
    ResourceGroup rg1 = new ResourceGroup(new HashSet<>(Arrays.asList(rd1)));
    RoleBinding roleBinding =
        roleBindingUtil.createRoleBinding(
            UUID.randomUUID(), role.getRoleUUID(), RoleBindingType.Custom, rg1);
  }

  @Test
  public void testEditRoleBinding() {
    ResourceDefinition rd1 =
        ResourceDefinition.builder().resourceType(ResourceType.UNIVERSE).allowAll(true).build();
    ResourceGroup rg1 = new ResourceGroup(new HashSet<>(Arrays.asList(rd1)));
    RoleBinding roleBinding =
        roleBindingUtil.createRoleBinding(
            user.getUuid(), role.getRoleUUID(), RoleBindingType.Custom, rg1);
    assertNotNull(roleBinding.getUuid());
    assertEquals(1, roleBinding.getResourceGroup().getResourceDefinitionSet().size());
    assertNotNull(roleBinding.getCreateTime());

    UUID resourceUUID1 = UUID.randomUUID();
    UUID resourceUUID2 = UUID.randomUUID();
    rd1.setAllowAll(false);
    rd1.setResourceUUIDSet(new HashSet<>(Arrays.asList(resourceUUID1, resourceUUID2)));
    ResourceDefinition rd2 =
        ResourceDefinition.builder().resourceType(ResourceType.UNIVERSE).allowAll(true).build();
    rg1 = new ResourceGroup(new HashSet<>(Arrays.asList(rd1, rd2)));
    roleBinding = roleBindingUtil.editRoleBinding(roleBinding.getUuid(), role.getRoleUUID(), rg1);
    assertNotNull(roleBinding.getUuid());
    assertEquals(2, roleBinding.getResourceGroup().getResourceDefinitionSet().size());
    assertNotNull(roleBinding.getCreateTime());

    int allowAllCount = 0;
    for (ResourceDefinition rd : roleBinding.getResourceGroup().getResourceDefinitionSet()) {
      if (rd.isAllowAll()) {
        allowAllCount++;
        assertEquals(0, rd.getResourceUUIDSet().size());
      } else {
        assertNotNull(rd.getResourceUUIDSet());
        assertTrue(rd.getResourceUUIDSet().contains(resourceUUID1));
        assertTrue(rd.getResourceUUIDSet().contains(resourceUUID2));
      }
      assertTrue(rd.getResourceType().equals(ResourceType.UNIVERSE));
    }
    assertEquals(1, allowAllCount);

    Role role2 =
        Role.create(
            customer.getUuid(),
            "FakeRole2",
            "testDescription",
            RoleType.Custom,
            new HashSet<>(
                Arrays.asList(
                    new Permission(ResourceType.OTHER, Action.CREATE),
                    new Permission(ResourceType.OTHER, Action.READ),
                    new Permission(ResourceType.OTHER, Action.UPDATE))));

    ResourceDefinition rd3 =
        ResourceDefinition.builder().resourceType(ResourceType.OTHER).allowAll(true).build();
    ResourceDefinition rd4 =
        ResourceDefinition.builder()
            .resourceType(ResourceType.OTHER)
            .allowAll(false)
            .resourceUUIDSet(new HashSet<>(Arrays.asList(resourceUUID1, resourceUUID2)))
            .build();
    ResourceGroup rg2 = new ResourceGroup(new HashSet<>(Arrays.asList(rd3, rd4)));
    roleBinding = roleBindingUtil.editRoleBinding(roleBinding.getUuid(), role2.getRoleUUID(), rg2);
    assertNotNull(roleBinding.getUuid());
    assertEquals(2, roleBinding.getResourceGroup().getResourceDefinitionSet().size());
    assertNotNull(roleBinding.getCreateTime());

    allowAllCount = 0;
    for (ResourceDefinition rd : roleBinding.getResourceGroup().getResourceDefinitionSet()) {
      if (rd.isAllowAll()) {
        allowAllCount++;
        assertEquals(0, rd.getResourceUUIDSet().size());
      } else {
        assertNotNull(rd.getResourceUUIDSet());
        assertTrue(rd.getResourceUUIDSet().contains(resourceUUID1));
        assertTrue(rd.getResourceUUIDSet().contains(resourceUUID2));
      }
      assertTrue(rd.getResourceType().equals(ResourceType.OTHER));
    }
    assertEquals(1, allowAllCount);
  }

  @Test
  public void testValidateResourceDefinitionNonDefaultResourceType() {
    // Assert that exception is thrown if both of the fields 'allowAll' or 'resourceUUIDSet' are
    // filled.
    ResourceDefinition resourceDefinition1 =
        ResourceDefinition.builder()
            .resourceType(ResourceType.UNIVERSE)
            .allowAll(true)
            .resourceUUIDSet(new HashSet<>(Arrays.asList(universe1.getUniverseUUID())))
            .build();
    assertPlatformException(
        () -> roleBindingUtil.validateResourceDefinition(customer.getUuid(), resourceDefinition1));

    // Assert that no exception is thrown if any one of the fields 'allowAll' or 'resourceUUIDSet'
    // is filled.

    ResourceDefinition resourceDefinition2 =
        ResourceDefinition.builder()
            .resourceType(ResourceType.UNIVERSE)
            .allowAll(false)
            .resourceUUIDSet(new HashSet<>(Arrays.asList(universe1.getUniverseUUID())))
            .build();
    roleBindingUtil.validateResourceDefinition(customer.getUuid(), resourceDefinition2);

    // Assert that no exception is thrown if any one of the fields 'allowAll' or 'resourceUUIDSet'
    // is filled.
    ResourceDefinition resourceDefinition3 =
        ResourceDefinition.builder()
            .resourceType(ResourceType.UNIVERSE)
            .allowAll(true)
            .resourceUUIDSet(new HashSet<>())
            .build();
    roleBindingUtil.validateResourceDefinition(customer.getUuid(), resourceDefinition3);

    // Assert that no exception is thrown if none of the fields 'allowAll' or 'resourceUUIDSet' are
    // filled.
    ResourceDefinition resourceDefinition4 =
        ResourceDefinition.builder()
            .resourceType(ResourceType.UNIVERSE)
            .allowAll(false)
            .resourceUUIDSet(new HashSet<>())
            .build();
    roleBindingUtil.validateResourceDefinition(customer.getUuid(), resourceDefinition4);
  }

  @Test
  public void testValidateResourceDefinitionDefaultResourceType() {
    // Assert that exception is thrown if both of the fields 'allowAll' or 'resourceUUIDSet' are
    // filled.
    ResourceDefinition resourceDefinition1 =
        ResourceDefinition.builder()
            .resourceType(ResourceType.OTHER)
            .allowAll(true)
            .resourceUUIDSet(new HashSet<>(Arrays.asList(UUID.randomUUID())))
            .build();
    assertPlatformException(
        () -> roleBindingUtil.validateResourceDefinition(customer.getUuid(), resourceDefinition1));

    // Assert that no exception is thrown if only 'resourceUUIDSet' is given for default resource
    // type with only customer UUID in the resource set.
    // This is the only correct resource definition for OTHER resource types.
    ResourceDefinition resourceDefinition2 =
        ResourceDefinition.builder()
            .resourceType(ResourceType.OTHER)
            .allowAll(false)
            .resourceUUIDSet(new HashSet<>(Arrays.asList(customer.getUuid())))
            .build();
    roleBindingUtil.validateResourceDefinition(customer.getUuid(), resourceDefinition2);

    // Assert that exception is thrown if 'allowAll' is true and 'resourceUUIDSet' is empty.
    ResourceDefinition resourceDefinition3 =
        ResourceDefinition.builder()
            .resourceType(ResourceType.OTHER)
            .allowAll(true)
            .resourceUUIDSet(new HashSet<>())
            .build();
    assertPlatformException(
        () -> roleBindingUtil.validateResourceDefinition(customer.getUuid(), resourceDefinition3));

    // Assert that exception is thrown if none of the fields 'allowAll' or 'resourceUUIDSet' are
    // filled.
    ResourceDefinition resourceDefinition4 =
        ResourceDefinition.builder()
            .resourceType(ResourceType.OTHER)
            .allowAll(false)
            .resourceUUIDSet(new HashSet<>())
            .build();
    assertPlatformException(
        () -> roleBindingUtil.validateResourceDefinition(customer.getUuid(), resourceDefinition4));
  }

  @Test
  public void testCleanupUniverseFromRoleBindings() {
    // Create a role binding with both universe1 and universe2 UUID.
    ResourceDefinition rd1 =
        ResourceDefinition.builder()
            .resourceType(ResourceType.UNIVERSE)
            .allowAll(false)
            .resourceUUIDSet(
                new HashSet<>(
                    Arrays.asList(universe1.getUniverseUUID(), universe2.getUniverseUUID())))
            .build();
    ResourceGroup rg1 = new ResourceGroup(new HashSet<>(Arrays.asList(rd1)));
    RoleBinding roleBinding =
        roleBindingUtil.createRoleBinding(
            user.getUuid(), role.getRoleUUID(), RoleBindingType.Custom, rg1);

    // Delete universe1. Universe1 UUID should also be removed from the role binding.
    universe1.delete();

    // Check that the role binding only contains universe2 UUID and not universe1 UUID.
    List<RoleBinding> roleBindingsForUser = RoleBinding.getAll(user.getUuid());
    assertEquals(1, roleBindingsForUser.size());
    assertEquals(
        1, roleBindingsForUser.get(0).getResourceGroup().getResourceDefinitionSet().size());

    Set<UUID> resourceUUIDs =
        roleBindingsForUser
            .get(0)
            .getResourceGroup()
            .getResourceDefinitionSet()
            .iterator()
            .next()
            .getResourceUUIDSet();
    assertFalse(resourceUUIDs.contains(universe1.getUniverseUUID()));
    assertTrue(resourceUUIDs.contains(universe2.getUniverseUUID()));
  }

  @Test
  public void testValidateRoleResourceDefinition() {
    // Create custom test role.
    Role role =
        Role.create(
            customer.getUuid(),
            "FakeRole2",
            "FakeRoleDescription1",
            RoleType.Custom,
            new HashSet<>(
                Arrays.asList(
                    new Permission(ResourceType.UNIVERSE, Action.CREATE),
                    new Permission(ResourceType.UNIVERSE, Action.READ),
                    new Permission(ResourceType.OTHER, Action.READ))));

    // Assert that exception is not thrown when correct resource definition for both UNIVERSE and
    // OTHER is given.
    ResourceDefinition resourceDefinition1 =
        ResourceDefinition.builder().resourceType(ResourceType.UNIVERSE).allowAll(true).build();
    ResourceDefinition resourceDefinition2 =
        ResourceDefinition.builder()
            .resourceType(ResourceType.OTHER)
            .allowAll(false)
            .resourceUUIDSet(new HashSet<>(Arrays.asList(customer.getUuid())))
            .build();
    ResourceGroup resourceGroup1 = new ResourceGroup();
    resourceGroup1.setResourceDefinitionSet(
        new HashSet<>(Arrays.asList(resourceDefinition1, resourceDefinition2)));
    RoleResourceDefinition roleResourceDefinition1 = new RoleResourceDefinition();
    roleResourceDefinition1.setRoleUUID(role.getRoleUUID());
    roleResourceDefinition1.setResourceGroup(resourceGroup1);
    // Assert that this doesn't throw any exception.
    roleBindingUtil.validateRoleResourceDefinition(customer.getUuid(), roleResourceDefinition1);
  }

  @Test
  public void testValidateRoleResourceDefinitionInvalid() {
    // Create custom test role.
    Role role =
        Role.create(
            customer.getUuid(),
            "FakeRole2",
            "FakeRoleDescription1",
            RoleType.Custom,
            new HashSet<>(
                Arrays.asList(
                    new Permission(ResourceType.UNIVERSE, Action.CREATE),
                    new Permission(ResourceType.UNIVERSE, Action.READ),
                    new Permission(ResourceType.OTHER, Action.READ))));

    // Assert that exception is thrown when correct resource definition for only UNIVERSE is given
    // and not OTHER.
    ResourceDefinition resourceDefinition1 =
        ResourceDefinition.builder().resourceType(ResourceType.UNIVERSE).allowAll(true).build();
    ResourceGroup resourceGroup1 = new ResourceGroup();
    resourceGroup1.setResourceDefinitionSet(new HashSet<>(Arrays.asList(resourceDefinition1)));
    RoleResourceDefinition roleResourceDefinition1 = new RoleResourceDefinition();
    roleResourceDefinition1.setRoleUUID(role.getRoleUUID());
    roleResourceDefinition1.setResourceGroup(resourceGroup1);
    // Exception should be thrown because we haven't given allowAll for OTHER in above resource
    // definition, even though OTHER.READ permission is given in the role.
    assertPlatformException(
        () ->
            roleBindingUtil.validateRoleResourceDefinition(
                customer.getUuid(), roleResourceDefinition1));
  }

  @Test
  public void testValidateRoleResourceDefinitionSystemRoleInvalid() {
    // Create custom test role.
    Role role =
        Role.create(
            customer.getUuid(),
            "FakeRole2",
            "FakeRoleDescription1",
            RoleType.System,
            new HashSet<>(
                Arrays.asList(
                    new Permission(ResourceType.UNIVERSE, Action.READ),
                    new Permission(ResourceType.UNIVERSE, Action.CREATE))));

    // Assert that exception is thrown when resource group is given for system defined roles.
    ResourceDefinition resourceDefinition1 =
        ResourceDefinition.builder().resourceType(ResourceType.UNIVERSE).allowAll(true).build();
    ResourceGroup resourceGroup1 = new ResourceGroup();
    resourceGroup1.setResourceDefinitionSet(new HashSet<>(Arrays.asList(resourceDefinition1)));
    RoleResourceDefinition roleResourceDefinition1 = new RoleResourceDefinition();
    roleResourceDefinition1.setRoleUUID(role.getRoleUUID());
    roleResourceDefinition1.setResourceGroup(resourceGroup1);
    // Exception should be thrown because system roles should not have a resource group attached.
    assertPlatformException(
        () ->
            roleBindingUtil.validateRoleResourceDefinition(
                customer.getUuid(), roleResourceDefinition1));
  }

  @Test
  public void testValidateRoleResourceDefinitionSystemRoleValid() {
    // Create custom test role.
    Role role =
        Role.create(
            customer.getUuid(),
            "FakeRole2",
            "FakeRoleDescription1",
            RoleType.System,
            new HashSet<>(
                Arrays.asList(
                    new Permission(ResourceType.UNIVERSE, Action.READ),
                    new Permission(ResourceType.UNIVERSE, Action.CREATE))));

    // Assert that exception is not thrown when resource group is not given for system defined
    // roles.
    RoleResourceDefinition roleResourceDefinition1 = new RoleResourceDefinition();
    roleResourceDefinition1.setRoleUUID(role.getRoleUUID());
    // Exception should not be thrown since system roles do not have a resource group attached.
    roleBindingUtil.validateRoleResourceDefinition(customer.getUuid(), roleResourceDefinition1);
  }

  @Test
  public void testGetResourceUuids() {
    // Create custom test role.
    Role role1 =
        Role.create(
            customer.getUuid(),
            "FakeRole11",
            "FakeRoleDescription11",
            RoleType.System,
            new HashSet<>(Arrays.asList(new Permission(ResourceType.UNIVERSE, Action.READ))));

    // Create custom test role.
    Role role2 =
        Role.create(
            customer.getUuid(),
            "FakeRole12",
            "FakeRoleDescription12",
            RoleType.System,
            new HashSet<>(
                Arrays.asList(
                    new Permission(ResourceType.UNIVERSE, Action.READ),
                    new Permission(ResourceType.UNIVERSE, Action.UPDATE))));

    Universe universe3 = ModelFactory.createUniverse("Test Universe 3", customer.getId());

    RoleResourceDefinition roleResourceDefinition11 = new RoleResourceDefinition();
    ResourceDefinition resourceDefinition11 =
        ResourceDefinition.builder()
            .resourceType(ResourceType.UNIVERSE)
            .allowAll(false)
            .resourceUUIDSet(new HashSet<>(Arrays.asList(universe1.getUniverseUUID())))
            .build();
    ResourceGroup resourceGroup11 = new ResourceGroup();
    resourceGroup11.setResourceDefinitionSet(new HashSet<>(Arrays.asList(resourceDefinition11)));
    roleResourceDefinition11.setResourceGroup(resourceGroup11);
    RoleBinding rB1 = RoleBinding.create(user, RoleBindingType.Custom, role1, resourceGroup11);
    Set<UUID> resourceUUIDs =
        roleBindingUtil.getResourceUuids(user.getUuid(), ResourceType.UNIVERSE, Action.READ);
    assertEquals(3, resourceUUIDs.size());
    assertTrue(resourceUUIDs.contains(universe1.getUniverseUUID()));
    assertTrue(resourceUUIDs.contains(universe2.getUniverseUUID()));
    assertTrue(resourceUUIDs.contains(universe3.getUniverseUUID()));
    RuntimeConfigEntry.upsertGlobal("yb.rbac.use_new_authz", "true");
    resourceUUIDs =
        roleBindingUtil.getResourceUuids(user.getUuid(), ResourceType.UNIVERSE, Action.READ);
    assertEquals(1, resourceUUIDs.size());
    assertTrue(resourceUUIDs.contains(universe1.getUniverseUUID()));
    rB1.delete();

    RoleResourceDefinition roleResourceDefinition12 = new RoleResourceDefinition();
    ResourceDefinition resourceDefinition12 =
        ResourceDefinition.builder()
            .resourceType(ResourceType.UNIVERSE)
            .allowAll(false)
            .resourceUUIDSet(new HashSet<>(Arrays.asList(universe2.getUniverseUUID())))
            .build();
    ResourceGroup resourceGroup12 = new ResourceGroup();
    resourceGroup12.setResourceDefinitionSet(new HashSet<>(Arrays.asList(resourceDefinition12)));
    roleResourceDefinition12.setResourceGroup(resourceGroup12);
    RoleBinding rB2 = RoleBinding.create(user, RoleBindingType.Custom, role2, resourceGroup12);
    RuntimeConfigEntry.upsertGlobal("yb.rbac.use_new_authz", "false");
    resourceUUIDs =
        roleBindingUtil.getResourceUuids(user.getUuid(), ResourceType.UNIVERSE, Action.READ);
    assertEquals(3, resourceUUIDs.size());
    assertTrue(resourceUUIDs.contains(universe1.getUniverseUUID()));
    assertTrue(resourceUUIDs.contains(universe2.getUniverseUUID()));
    assertTrue(resourceUUIDs.contains(universe3.getUniverseUUID()));
    RuntimeConfigEntry.upsertGlobal("yb.rbac.use_new_authz", "true");
    resourceUUIDs =
        roleBindingUtil.getResourceUuids(user.getUuid(), ResourceType.UNIVERSE, Action.READ);
    assertEquals(1, resourceUUIDs.size());
    assertTrue(resourceUUIDs.contains(universe2.getUniverseUUID()));
    rB2.delete();

    rB1 = RoleBinding.create(user, RoleBindingType.Custom, role1, resourceGroup11);
    rB2 = RoleBinding.create(user, RoleBindingType.Custom, role2, resourceGroup12);
    RuntimeConfigEntry.upsertGlobal("yb.rbac.use_new_authz", "true");
    resourceUUIDs =
        roleBindingUtil.getResourceUuids(user.getUuid(), ResourceType.UNIVERSE, Action.READ);
    assertEquals(2, resourceUUIDs.size());
    assertTrue(resourceUUIDs.contains(universe1.getUniverseUUID()));
    assertTrue(resourceUUIDs.contains(universe2.getUniverseUUID()));

    RoleResourceDefinition roleResourceDefinition13 = new RoleResourceDefinition();
    ResourceDefinition resourceDefinition13 =
        ResourceDefinition.builder().resourceType(ResourceType.UNIVERSE).allowAll(true).build();
    ResourceGroup resourceGroup13 = new ResourceGroup();
    resourceGroup13.setResourceDefinitionSet(new HashSet<>(Arrays.asList(resourceDefinition13)));
    roleResourceDefinition13.setResourceGroup(resourceGroup13);
    rB1 = RoleBinding.create(user, RoleBindingType.Custom, role1, resourceGroup11);
    rB2 = RoleBinding.create(user, RoleBindingType.Custom, role2, resourceGroup12);
    RoleBinding rB3 = RoleBinding.create(user, RoleBindingType.Custom, role, resourceGroup13);
    RuntimeConfigEntry.upsertGlobal("yb.rbac.use_new_authz", "false");
    resourceUUIDs =
        roleBindingUtil.getResourceUuids(user.getUuid(), ResourceType.UNIVERSE, Action.READ);
    assertEquals(3, resourceUUIDs.size());
    assertTrue(resourceUUIDs.contains(universe1.getUniverseUUID()));
    assertTrue(resourceUUIDs.contains(universe2.getUniverseUUID()));
    assertTrue(resourceUUIDs.contains(universe3.getUniverseUUID()));
    RuntimeConfigEntry.upsertGlobal("yb.rbac.use_new_authz", "true");
    resourceUUIDs =
        roleBindingUtil.getResourceUuids(user.getUuid(), ResourceType.UNIVERSE, Action.READ);
    assertEquals(3, resourceUUIDs.size());
    assertTrue(resourceUUIDs.contains(universe1.getUniverseUUID()));
    assertTrue(resourceUUIDs.contains(universe2.getUniverseUUID()));
    assertTrue(resourceUUIDs.contains(universe3.getUniverseUUID()));
  }
}
