package com.yugabyte.yw.api.v2;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static play.mvc.Http.Status.OK;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yba.v2.client.ApiClient;
import com.yugabyte.yba.v2.client.ApiException;
import com.yugabyte.yba.v2.client.ApiResponse;
import com.yugabyte.yba.v2.client.Configuration;
import com.yugabyte.yba.v2.client.api.AuthenticationApi;
import com.yugabyte.yba.v2.client.models.AuthGroupToRolesMapping;
import com.yugabyte.yba.v2.client.models.AuthGroupToRolesMapping.TypeEnum;
import com.yugabyte.yba.v2.client.models.ResourceDefinition;
import com.yugabyte.yba.v2.client.models.ResourceDefinition.ResourceTypeEnum;
import com.yugabyte.yba.v2.client.models.ResourceGroup;
import com.yugabyte.yba.v2.client.models.RoleResourceDefinition;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.rbac.Permission;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.GroupMappingInfo;
import com.yugabyte.yw.models.GroupMappingInfo.GroupType;
import com.yugabyte.yw.models.LdapDnToYbaRole;
import com.yugabyte.yw.models.OidcGroupToYbaRoles;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.rbac.Role;
import com.yugabyte.yw.models.rbac.Role.RoleType;
import com.yugabyte.yw.models.rbac.RoleBinding;
import db.migration.default_.common.R__Sync_System_Roles;
import db.migration.default_.postgres.V365__Migrate_Group_Mappings;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import junitparams.JUnitParamsRunner;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(JUnitParamsRunner.class)
public class AuthenticationGroupMappingApisTest extends FakeDBApplication {
  private Customer customer;
  private String authToken;
  private UUID cUUID;
  private AuthenticationApi api;

  private Users user;
  private ApiClient v2ApiClient;

  // Define test permissions to use later.
  public Permission permission1 = new Permission(ResourceType.UNIVERSE, Action.CREATE);
  public Permission permission2 = new Permission(ResourceType.UNIVERSE, Action.READ);

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    cUUID = customer.getUuid();
    user = ModelFactory.testSuperAdminUserNewRbac(customer);
    api = new AuthenticationApi();
    authToken = user.createAuthToken();
    v2ApiClient = Configuration.getDefaultApiClient();
    String basePath = String.format("http://localhost:%d/api/v2", port);
    v2ApiClient = v2ApiClient.setBasePath(basePath).addDefaultHeader("X-AUTH-TOKEN", authToken);
    Configuration.setDefaultApiClient(v2ApiClient);
    R__Sync_System_Roles.syncSystemRoles();
  }

  @Test
  public void testListMappingRbacOff() throws Exception {
    GroupMappingInfo info0 = GroupMappingInfo.create(cUUID, "test-group-1", GroupType.OIDC);
    GroupMappingInfo info1 = GroupMappingInfo.create(cUUID, "test-group-2", GroupType.LDAP);
    info1.setRoleUUID(Role.get(cUUID, "Admin").getRoleUUID());
    info1.save();
    assertNotNull(info0.getCreationDate());
    assertNotNull(info1.getCreationDate());
    ApiResponse<List<AuthGroupToRolesMapping>> result = api.listMappingsWithHttpInfo(cUUID);
    assertEquals(OK, result.getStatusCode());
    List<AuthGroupToRolesMapping> res = result.getData();
    assertEquals(2, res.size());

    AuthGroupToRolesMapping res0 = res.get(0), res1 = res.get(1);

    assertEquals(1, res0.getRoleResourceDefinitions().size());
    assertEquals(info0.getIdentifier(), res0.getGroupIdentifier());
    assertEquals(info0.getRoleUUID(), res0.getRoleResourceDefinitions().get(0).getRoleUuid());
    assertNotNull(res0.getCreationDate());
    assertEquals(
        Role.get(cUUID, "ConnectOnly").getRoleUUID(),
        res0.getRoleResourceDefinitions().get(0).getRoleUuid());

    assertEquals(1, res1.getRoleResourceDefinitions().size());
    assertEquals(info1.getIdentifier(), res1.getGroupIdentifier());
    assertEquals(info1.getRoleUUID(), res1.getRoleResourceDefinitions().get(0).getRoleUuid());
    assertEquals(
        Role.get(cUUID, "Admin").getRoleUUID(),
        res1.getRoleResourceDefinitions().get(0).getRoleUuid());

    ApiResponse<Void> delResponse = api.deleteGroupMappingsWithHttpInfo(cUUID, res0.getUuid());
    assertEquals(OK, delResponse.getStatusCode());
  }

  @Test
  public void testCRUDMappingsRbacOn() throws Exception {
    RuntimeConfigEntry.upsertGlobal("yb.rbac.use_new_authz", "true");
    AuthenticationApi api = new AuthenticationApi();
    // create custom role
    Role customRole =
        Role.create(
            cUUID,
            "testCustomRole",
            "testDescription",
            RoleType.Custom,
            new HashSet<>(Arrays.asList(permission1, permission2)));

    List<AuthGroupToRolesMapping> mappingSpecList = new ArrayList<>();
    AuthGroupToRolesMapping ldapSpec = new AuthGroupToRolesMapping();
    AuthGroupToRolesMapping oidcSpec = new AuthGroupToRolesMapping();

    ldapSpec.setGroupIdentifier("test-group-ldap");
    ldapSpec.setType(TypeEnum.LDAP);
    RoleResourceDefinition rrdSystemRole = new RoleResourceDefinition();
    rrdSystemRole.setRoleUuid(Role.get(cUUID, "Admin").getRoleUUID());
    ldapSpec.setRoleResourceDefinitions(Arrays.asList(rrdSystemRole));

    oidcSpec.setGroupIdentifier("test-group-oidc");
    oidcSpec.setType(TypeEnum.OIDC);
    RoleResourceDefinition rrdCustomRole = new RoleResourceDefinition();
    rrdCustomRole.setRoleUuid(customRole.getRoleUUID());
    ResourceDefinition rd1 =
        new ResourceDefinition().allowAll(true).resourceType(ResourceTypeEnum.UNIVERSE);
    ResourceDefinition rd2 =
        new ResourceDefinition()
            .allowAll(false)
            .resourceType(ResourceTypeEnum.OTHER)
            .resourceUuidSet(Arrays.asList(cUUID));
    rrdCustomRole.setResourceGroup(
        new ResourceGroup().resourceDefinitionSet(Arrays.asList(rd1, rd2)));
    oidcSpec.setRoleResourceDefinitions(Arrays.asList(rrdSystemRole, rrdCustomRole));

    mappingSpecList.add(ldapSpec);
    mappingSpecList.add(oidcSpec);

    ApiResponse<Void> resUpdate = api.updateGroupMappingsWithHttpInfo(cUUID, mappingSpecList);
    assertEquals(OK, resUpdate.getStatusCode());

    ApiResponse<List<AuthGroupToRolesMapping>> resList = api.listMappingsWithHttpInfo(cUUID);
    assertEquals(OK, resList.getStatusCode());

    List<AuthGroupToRolesMapping> specList = resList.getData();
    assertEquals(2, specList.size());

    AuthGroupToRolesMapping ldapGroupInfo = specList.get(0);
    assertEquals("test-group-ldap", ldapGroupInfo.getGroupIdentifier());
    assertEquals(TypeEnum.LDAP, ldapGroupInfo.getType());
    assertEquals(2, ldapGroupInfo.getRoleResourceDefinitions().size());
    assertNotNull(ldapGroupInfo.getRoleResourceDefinitions().get(1).getResourceGroup());
    assertEquals(
        Role.get(cUUID, "Admin").getRoleUUID(),
        ldapGroupInfo.getRoleResourceDefinitions().get(1).getRoleUuid());

    AuthGroupToRolesMapping oidcGroupInfo = specList.get(1);
    assertEquals("test-group-oidc", oidcGroupInfo.getGroupIdentifier());
    assertEquals(TypeEnum.OIDC, oidcGroupInfo.getType());
    assertEquals(3, oidcGroupInfo.getRoleResourceDefinitions().size());
    assertEquals(
        Role.get(cUUID, "Admin").getRoleUUID(),
        oidcGroupInfo.getRoleResourceDefinitions().get(1).getRoleUuid());

    UUID oidcGroupUUID = oidcGroupInfo.getUuid();

    oidcSpec.setGroupIdentifier("Test-Group-OIDC");
    assertTrue(mappingSpecList.remove(ldapSpec));
    resUpdate = api.updateGroupMappingsWithHttpInfo(cUUID, mappingSpecList);
    assertEquals(OK, resUpdate.getStatusCode());

    resList = api.listMappingsWithHttpInfo(cUUID);
    assertEquals(OK, resList.getStatusCode());

    specList = resList.getData();
    assertEquals(2, specList.size());
    // Make sure group names are case insensitive and uuid remains the same after update.
    assertEquals(oidcGroupUUID, specList.get(1).getUuid());

    ApiResponse<Void> delResponse = api.deleteGroupMappingsWithHttpInfo(cUUID, oidcGroupUUID);
    assertEquals(OK, delResponse.getStatusCode());

    GroupMappingInfo info = GroupMappingInfo.get(oidcGroupUUID);
    assertEquals(null, info);
    assertEquals(
        0, RoleBinding.find.query().where().eq("principal_uuid", oidcGroupUUID).findList().size());
  }

  @Test
  public void testBadInput() throws Exception {
    RuntimeConfigEntry.upsertGlobal("yb.rbac.use_new_authz", "false");
    AuthenticationApi api = new AuthenticationApi();
    List<AuthGroupToRolesMapping> mappingSpecList = new ArrayList<>();
    AuthGroupToRolesMapping ldapSpec = new AuthGroupToRolesMapping();

    ldapSpec.setGroupIdentifier("test-group-ldap");
    ldapSpec.setType(TypeEnum.LDAP);
    RoleResourceDefinition rrdSystemRole = new RoleResourceDefinition();
    rrdSystemRole.setRoleUuid(Role.get(cUUID, "Admin").getRoleUUID());
    List<RoleResourceDefinition> rrdList = new ArrayList<>();
    rrdList.add(rrdSystemRole);
    rrdList.add(rrdSystemRole);
    ldapSpec.setRoleResourceDefinitions(rrdList);

    mappingSpecList.add(ldapSpec);
    assertThrows(
        ApiException.class, () -> api.updateGroupMappingsWithHttpInfo(cUUID, mappingSpecList));

    ldapSpec.getRoleResourceDefinitions().remove(0);
    ApiResponse<Void> resUpdate = api.updateGroupMappingsWithHttpInfo(cUUID, mappingSpecList);
    assertEquals(OK, resUpdate.getStatusCode());
  }

  @Test
  public void testV1toV2migrationRbacOn() throws Exception {
    // Create v1 entries for LDAP and OIDC group mappings.
    // Run the migration and assert that role bindings are added for existing group mappings.
    RuntimeConfigEntry.upsertGlobal("yb.rbac.use_new_authz", "true");
    LdapDnToYbaRole lm1 = LdapDnToYbaRole.create(cUUID, "dn1", Users.Role.Admin);
    LdapDnToYbaRole lm2 = LdapDnToYbaRole.create(cUUID, "dn2", Users.Role.BackupAdmin);
    LdapDnToYbaRole lm3 = LdapDnToYbaRole.create(cUUID, "dn3", Users.Role.ReadOnly);

    Role role1 = Role.get(cUUID, "Admin");
    Role role2 = Role.get(cUUID, "BackupAdmin");
    Role role3 = Role.get(cUUID, "ReadOnly");

    OidcGroupToYbaRoles r1 =
        OidcGroupToYbaRoles.create(cUUID, "Group-1", ImmutableList.of(role1.getRoleUUID()));
    OidcGroupToYbaRoles r2 =
        OidcGroupToYbaRoles.create(cUUID, "Group-2", ImmutableList.of(role2.getRoleUUID()));
    OidcGroupToYbaRoles r3 =
        OidcGroupToYbaRoles.create(cUUID, "Group-3", ImmutableList.of(role3.getRoleUUID()));

    List<String> oidcGroupNames =
        OidcGroupToYbaRoles.find.all().stream()
            .map(OidcGroupToYbaRoles::getGroupName)
            .collect(Collectors.toList());

    List<String> ldapGroupNames =
        LdapDnToYbaRole.find.all().stream()
            .map(
                group -> {
                  return group.distinguishedName;
                })
            .collect(Collectors.toList());

    List<UUID> ldapRoles =
        LdapDnToYbaRole.find.all().stream()
            .map(
                group -> {
                  return group.ybaRole;
                })
            .map(role -> Role.get(cUUID, role.name()).getRoleUUID())
            .collect(Collectors.toList());
    List<UUID> oidcRoles =
        OidcGroupToYbaRoles.find.all().stream()
            .map(group -> group.getYbaRoles().get(0))
            .collect(Collectors.toList());

    V365__Migrate_Group_Mappings.migrateGroupMappings();

    ApiResponse<List<AuthGroupToRolesMapping>> resList = api.listMappingsWithHttpInfo(cUUID);
    assertEquals(OK, resList.getStatusCode());

    List<AuthGroupToRolesMapping> specList = resList.getData();
    assertEquals(6, specList.size());

    List<UUID> ldapRolesList =
        specList.stream()
            .filter(groupMapping -> groupMapping.getType().equals(TypeEnum.LDAP))
            .flatMap(groupMapping -> groupMapping.getRoleResourceDefinitions().stream())
            .map(RoleResourceDefinition::getRoleUuid)
            .collect(Collectors.toList());

    List<UUID> oidcRolesList =
        specList.stream()
            .filter(groupMapping -> groupMapping.getType().equals(TypeEnum.OIDC))
            .flatMap(groupMapping -> groupMapping.getRoleResourceDefinitions().stream())
            .map(RoleResourceDefinition::getRoleUuid)
            .collect(Collectors.toList());

    // Make sure response contains all assigned roles.
    assertTrue(ldapRolesList.containsAll(ldapRoles));
    assertTrue(oidcRolesList.containsAll(oidcRoles));

    // Make sure all role bindings have resource groups.
    for (AuthGroupToRolesMapping groupMapping : specList) {
      assertNotNull(groupMapping.getRoleResourceDefinitions());
      assertNotNull(groupMapping.getCreationDate());
      for (RoleResourceDefinition rrds : groupMapping.getRoleResourceDefinitions()) {
        assertNotNull(rrds.getRoleUuid());
        // We don't add role bindings for ConnectOnly Role.
        if (!Role.get(cUUID, rrds.getRoleUuid()).getName().equals("ConnectOnly")) {
          assertNotNull(rrds.getResourceGroup());
          assertNotNull(rrds.getResourceGroup().getResourceDefinitionSet());
        }
      }
      if (groupMapping.getType().equals(TypeEnum.LDAP)) {
        assertTrue(ldapGroupNames.contains(groupMapping.getGroupIdentifier()));
      } else {
        assertTrue(oidcGroupNames.contains(groupMapping.getGroupIdentifier()));
      }
      // test delete
      ApiResponse<Void> delResponse =
          api.deleteGroupMappingsWithHttpInfo(cUUID, groupMapping.getUuid());
      assertEquals(OK, delResponse.getStatusCode());
    }
  }
}
