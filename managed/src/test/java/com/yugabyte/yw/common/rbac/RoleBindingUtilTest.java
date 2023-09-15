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
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(JUnitParamsRunner.class)
public class RoleBindingUtilTest extends FakeDBApplication {

  private RoleBindingUtil roleBindingUtil;
  private Customer customer;
  private Universe universe1;
  private Universe universe2;
  private Users user;
  private Role role;

  @Before
  public void setup() {
    roleBindingUtil = new RoleBindingUtil(null);
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

    // Assert that exception is thrown if none of the fields 'allowAll' or 'resourceUUIDSet' are
    // filled.
    ResourceDefinition resourceDefinition4 =
        ResourceDefinition.builder()
            .resourceType(ResourceType.UNIVERSE)
            .allowAll(false)
            .resourceUUIDSet(new HashSet<>())
            .build();
    assertPlatformException(
        () -> roleBindingUtil.validateResourceDefinition(customer.getUuid(), resourceDefinition4));
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
}
