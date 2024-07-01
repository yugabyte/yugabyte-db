package com.yugabyte.yw.api.v2;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static play.mvc.Http.Status.OK;

import com.yugabyte.yba.v2.client.ApiClient;
import com.yugabyte.yba.v2.client.ApiException;
import com.yugabyte.yba.v2.client.ApiResponse;
import com.yugabyte.yba.v2.client.Configuration;
import com.yugabyte.yba.v2.client.api.AuthenticationApi;
import com.yugabyte.yba.v2.client.models.GroupMappingSpec;
import com.yugabyte.yba.v2.client.models.GroupMappingSpec.TypeEnum;
import com.yugabyte.yba.v2.client.models.ResourceDefinitionSpec;
import com.yugabyte.yba.v2.client.models.ResourceDefinitionSpec.ResourceTypeEnum;
import com.yugabyte.yba.v2.client.models.ResourceGroupSpec;
import com.yugabyte.yba.v2.client.models.RoleResourceDefinitionSpec;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.rbac.Permission;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.GroupMappingInfo;
import com.yugabyte.yw.models.GroupMappingInfo.GroupType;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.rbac.Role;
import com.yugabyte.yw.models.rbac.Role.RoleType;
import com.yugabyte.yw.models.rbac.RoleBinding;
import db.migration.default_.common.R__Sync_System_Roles;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(JUnitParamsRunner.class)
public class AuthenticationGroupMappingApisTest extends FakeDBApplication {
  private Customer customer;
  private String authToken;

  private Users user;
  private ApiClient v2ApiClient;

  // Define test permissions to use later.
  public Permission permission1 = new Permission(ResourceType.UNIVERSE, Action.CREATE);
  public Permission permission2 = new Permission(ResourceType.UNIVERSE, Action.READ);

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testSuperAdminUserNewRbac(customer);
    authToken = user.createAuthToken();
    v2ApiClient = Configuration.getDefaultApiClient();
    String basePath = String.format("http://localhost:%d/api/v2", port);
    v2ApiClient = v2ApiClient.setBasePath(basePath).addDefaultHeader("X-AUTH-TOKEN", authToken);
    Configuration.setDefaultApiClient(v2ApiClient);
    R__Sync_System_Roles.syncSystemRoles();
  }

  @Test
  public void testListMappingRbacOff() throws Exception {
    UUID cUUID = customer.getUuid();
    GroupMappingInfo info0 = GroupMappingInfo.create(cUUID, "test-group-1", GroupType.OIDC);
    GroupMappingInfo info1 = GroupMappingInfo.create(cUUID, "test-group-2", GroupType.LDAP);
    info1.setRoleUUID(Role.get(cUUID, "Admin").getRoleUUID());
    info1.save();
    AuthenticationApi api = new AuthenticationApi();
    ApiResponse<List<GroupMappingSpec>> result = api.listMappingsWithHttpInfo(cUUID);
    assertEquals(OK, result.getStatusCode());
    List<GroupMappingSpec> res = result.getData();
    assertEquals(2, res.size());

    assertEquals(1, res.get(0).getRoleResourceDefinitions().size());
    assertEquals(info0.getIdentifier(), res.get(0).getGroupIdentifier());
    assertEquals(info0.getRoleUUID(), res.get(0).getRoleResourceDefinitions().get(0).getRoleUuid());
    assertEquals(
        Role.get(cUUID, "ConnectOnly").getRoleUUID(),
        res.get(0).getRoleResourceDefinitions().get(0).getRoleUuid());

    assertEquals(1, res.get(1).getRoleResourceDefinitions().size());
    assertEquals(info1.getIdentifier(), res.get(1).getGroupIdentifier());
    assertEquals(info1.getRoleUUID(), res.get(1).getRoleResourceDefinitions().get(0).getRoleUuid());
    assertEquals(
        Role.get(cUUID, "Admin").getRoleUUID(),
        res.get(1).getRoleResourceDefinitions().get(0).getRoleUuid());
  }

  @Test
  public void testCRUDMappingsRbacOn() throws Exception {
    RuntimeConfigEntry.upsertGlobal("yb.rbac.use_new_authz", "true");
    AuthenticationApi api = new AuthenticationApi();
    UUID cUUID = customer.getUuid();
    // create custom role
    Role customRole =
        Role.create(
            cUUID,
            "testCustomRole",
            "testDescription",
            RoleType.Custom,
            new HashSet<>(Arrays.asList(permission1, permission2)));

    List<GroupMappingSpec> mappingSpecList = new ArrayList<>();
    GroupMappingSpec ldapSpec = new GroupMappingSpec();
    GroupMappingSpec oidcSpec = new GroupMappingSpec();

    ldapSpec.setGroupIdentifier("test-group-ldap");
    ldapSpec.setType(TypeEnum.LDAP);
    RoleResourceDefinitionSpec rrdSystemRole = new RoleResourceDefinitionSpec();
    rrdSystemRole.setRoleUuid(Role.get(cUUID, "Admin").getRoleUUID());
    ldapSpec.setRoleResourceDefinitions(Arrays.asList(rrdSystemRole));

    oidcSpec.setGroupIdentifier("test-group-oidc");
    oidcSpec.setType(TypeEnum.OIDC);
    RoleResourceDefinitionSpec rrdCustomRole = new RoleResourceDefinitionSpec();
    rrdCustomRole.setRoleUuid(customRole.getRoleUUID());
    ResourceDefinitionSpec rd1 =
        new ResourceDefinitionSpec().allowAll(true).resourceType(ResourceTypeEnum.UNIVERSE);
    ResourceDefinitionSpec rd2 =
        new ResourceDefinitionSpec()
            .allowAll(false)
            .resourceType(ResourceTypeEnum.OTHER)
            .resourceUuidSet(Arrays.asList(cUUID));
    rrdCustomRole.setResourceGroup(
        new ResourceGroupSpec().resourceDefinitionSet(Arrays.asList(rd1, rd2)));
    oidcSpec.setRoleResourceDefinitions(Arrays.asList(rrdSystemRole, rrdCustomRole));

    mappingSpecList.add(ldapSpec);
    mappingSpecList.add(oidcSpec);

    ApiResponse<Void> resUpdate = api.updateGroupMappingsWithHttpInfo(cUUID, mappingSpecList);
    assertEquals(OK, resUpdate.getStatusCode());

    ApiResponse<List<GroupMappingSpec>> resList = api.listMappingsWithHttpInfo(cUUID);
    assertEquals(OK, resList.getStatusCode());

    List<GroupMappingSpec> specList = resList.getData();
    assertEquals(2, specList.size());

    GroupMappingSpec ldapGroupInfo = specList.get(0);
    assertEquals("test-group-ldap", ldapGroupInfo.getGroupIdentifier());
    assertEquals(TypeEnum.LDAP, ldapGroupInfo.getType());
    assertEquals(1, ldapGroupInfo.getRoleResourceDefinitions().size());
    assertEquals(
        Role.get(cUUID, "Admin").getRoleUUID(),
        ldapGroupInfo.getRoleResourceDefinitions().get(0).getRoleUuid());

    GroupMappingSpec oidcGroupInfo = specList.get(1);
    assertEquals("test-group-oidc", oidcGroupInfo.getGroupIdentifier());
    assertEquals(TypeEnum.OIDC, oidcGroupInfo.getType());
    assertEquals(2, oidcGroupInfo.getRoleResourceDefinitions().size());
    assertEquals(
        Role.get(cUUID, "Admin").getRoleUUID(),
        oidcGroupInfo.getRoleResourceDefinitions().get(0).getRoleUuid());

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
    UUID cUUID = customer.getUuid();
    List<GroupMappingSpec> mappingSpecList = new ArrayList<>();
    GroupMappingSpec ldapSpec = new GroupMappingSpec();

    ldapSpec.setGroupIdentifier("test-group-ldap");
    ldapSpec.setType(TypeEnum.LDAP);
    RoleResourceDefinitionSpec rrdSystemRole = new RoleResourceDefinitionSpec();
    rrdSystemRole.setRoleUuid(Role.get(cUUID, "Admin").getRoleUUID());
    List<RoleResourceDefinitionSpec> rrdList = new ArrayList<>();
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
}
