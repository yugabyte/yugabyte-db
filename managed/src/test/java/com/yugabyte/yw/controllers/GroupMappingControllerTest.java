/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.models.ScopedRuntimeConfig.GLOBAL_SCOPE_UUID;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.fakeRequest;

import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.ObjectReader;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.forms.LdapDnToYbaRoleData.LdapDnYbaRoleDataPair;
import com.yugabyte.yw.forms.OidcGroupToYbaRolesData.OidcGroupToYbaRolesPair;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.GroupMappingInfo;
import com.yugabyte.yw.models.GroupMappingInfo.GroupType;
import com.yugabyte.yw.models.LdapDnToYbaRole;
import com.yugabyte.yw.models.OidcGroupToYbaRoles;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.rbac.Role;
import db.migration.default_.common.R__Sync_System_Roles;
import db.migration.default_.postgres.V365__Migrate_Group_Mappings;
import java.io.IOException;
import java.util.List;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

@RunWith(JUnitParamsRunner.class)
public class GroupMappingControllerTest extends FakeDBApplication {

  private Customer defaultCustomer;
  private Users defaultUser;
  private ObjectMapper mapper;
  private UUID cUUID;
  private String authToken;

  private static final String SET_KEY_ENDPOINT = "/api/customers/%s/runtime_config/%s/key/%s";

  @Before
  public void setup() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUser = ModelFactory.testUser(defaultCustomer, Users.Role.SuperAdmin);
    mapper = new ObjectMapper();
    cUUID = defaultCustomer.getUuid();
    authToken = defaultUser.createAuthToken();
    R__Sync_System_Roles.syncSystemRoles();
  }

  @Test
  public void testList() throws IOException {
    GroupMappingInfo lm1 =
        GroupMappingInfo.create(
            cUUID, Role.get(cUUID, "Admin").getRoleUUID(), "dn1", GroupType.LDAP);
    GroupMappingInfo lm2 =
        GroupMappingInfo.create(
            cUUID, Role.get(cUUID, "BackupAdmin").getRoleUUID(), "dn2", GroupType.LDAP);
    GroupMappingInfo lm3 =
        GroupMappingInfo.create(
            cUUID, Role.get(cUUID, "ReadOnly").getRoleUUID(), "dn3", GroupType.LDAP);

    LdapDnYbaRoleDataPair lp1 = new LdapDnYbaRoleDataPair();
    LdapDnYbaRoleDataPair lp2 = new LdapDnYbaRoleDataPair();
    LdapDnYbaRoleDataPair lp3 = new LdapDnYbaRoleDataPair();

    lp1.setDistinguishedName(lm1.getIdentifier());
    lp2.setDistinguishedName(lm2.getIdentifier());
    lp3.setDistinguishedName(lm3.getIdentifier());

    lp1.setYbaRole(Users.Role.valueOf(Role.get(cUUID, lm1.getRoleUUID()).getName()));
    lp2.setYbaRole(Users.Role.valueOf(Role.get(cUUID, lm2.getRoleUUID()).getName()));
    lp3.setYbaRole(Users.Role.valueOf(Role.get(cUUID, lm3.getRoleUUID()).getName()));

    ObjectReader reader = mapper.readerFor(new TypeReference<List<LdapDnYbaRoleDataPair>>() {});
    List<LdapDnYbaRoleDataPair> LdapDnYbaRoleDataPairs =
        reader.readValue(fetchLdapDnToYbaRoles().get("ldapDnToYbaRolePairs"));
    assertEquals(LdapDnYbaRoleDataPairs.size(), 3);
    assertTrue(LdapDnYbaRoleDataPairs.containsAll(ImmutableList.of(lp1, lp2, lp3)));
  }

  @Test
  public void testFailUpdateRoleBadFormat() {
    ObjectNode bodyJson = Json.newObject();

    ObjectNode invalidMapping = Json.newObject();
    invalidMapping.put("ybaRole", "InvalidRole");
    invalidMapping.put("distinguishedName", "dn");
    ArrayNode pairList = mapper.createArrayNode().add(invalidMapping);

    bodyJson.put("ldapDnToYbaRolePairs", pairList);
    assertPlatformException(() -> updateLdapDnToYbaRoles(bodyJson));
  }

  @Test
  public void testSuccessUpdateMappings() {
    String dn = "cn=user,dc=com";
    Users.Role role = Users.Role.Admin;

    LdapDnYbaRoleDataPair lp = new LdapDnYbaRoleDataPair();
    lp.setDistinguishedName(dn);
    lp.setYbaRole(role);
    ObjectNode bodyJson = Json.newObject();
    ArrayNode pairList = mapper.createArrayNode().add(mapper.convertValue(lp, JsonNode.class));

    bodyJson.put("ldapDnToYbaRolePairs", pairList);
    Result r = updateLdapDnToYbaRoles(bodyJson);
    assertOk(r);

    List<GroupMappingInfo> updatedMappings =
        GroupMappingInfo.find.query().where().eq("type", "LDAP").findList();

    assertEquals(updatedMappings.size(), 1);
    assertEquals(updatedMappings.get(0).getIdentifier(), dn);
    assertEquals(updatedMappings.get(0).getRoleUUID(), Role.get(cUUID, "Admin").getRoleUUID());
  }

  @Test
  public void testOverwriteMappings() {
    String dn = "cn=user,dc=com";
    GroupMappingInfo oldMapping =
        GroupMappingInfo.create(cUUID, Role.get(cUUID, "Admin").getRoleUUID(), dn, GroupType.LDAP);

    String dn2 = "cn=user2,dc=org";
    Users.Role role2 = Users.Role.BackupAdmin;

    LdapDnYbaRoleDataPair lp = new LdapDnYbaRoleDataPair();
    lp.setDistinguishedName(dn2);
    lp.setYbaRole(role2);
    ObjectNode bodyJson = Json.newObject();
    ArrayNode pairList = mapper.createArrayNode().add(mapper.convertValue(lp, JsonNode.class));
    bodyJson.put("ldapDnToYbaRolePairs", pairList);
    Result r = updateLdapDnToYbaRoles(bodyJson);
    assertOk(r);

    List<GroupMappingInfo> updatedMappings =
        GroupMappingInfo.find.query().where().eq("type", "LDAP").findList();
    assertEquals(updatedMappings.contains(oldMapping), false);
    assertEquals(updatedMappings.size(), 1);
    assertEquals(updatedMappings.get(0).getIdentifier(), dn2);
    assertEquals(
        updatedMappings.get(0).getRoleUUID(), Role.get(cUUID, "BackupAdmin").getRoleUUID());
  }

  @Test
  public void testSuperAdminMapping() {
    String dn = "cn=user,dc=com";
    Users.Role role = Users.Role.SuperAdmin;

    LdapDnYbaRoleDataPair lp = new LdapDnYbaRoleDataPair();
    lp.setDistinguishedName(dn);
    lp.setYbaRole(role);
    ObjectNode bodyJson = Json.newObject();
    ArrayNode pairList = mapper.createArrayNode().add(mapper.convertValue(lp, JsonNode.class));

    bodyJson.put("ldapDnToYbaRolePairs", pairList);
    Result r = assertPlatformException(() -> updateLdapDnToYbaRoles(bodyJson));
    assertBadRequest(r, "SuperAdmin cannot be mapped to a disinguished name!");
  }

  @Test
  public void testOidcUpdateMapping() {
    Role role = Role.get(cUUID, "Admin");
    OidcGroupToYbaRolesPair pair = new OidcGroupToYbaRolesPair();
    pair.setGroupName("Admins");
    pair.setRoles(ImmutableList.of(role.getRoleUUID()));

    ObjectNode bodyJson = Json.newObject();
    ArrayNode pairList = mapper.createArrayNode().add(mapper.convertValue(pair, JsonNode.class));
    bodyJson.put("oidcGroupToYbaRolesPairs", pairList);

    // create new mapping
    Result r = updateOidcGroupToYbaRoles(bodyJson);
    assertOk(r);

    GroupMappingInfo entity =
        GroupMappingInfo.find
            .query()
            .where()
            .ieq("identifier", "admins")
            .eq("type", "OIDC")
            .findOne();
    assertNotNull(entity);
    assertEquals(entity.getRoleUUID(), role.getRoleUUID());

    // update existing mapping
    role = Role.get(cUUID, "BackupAdmin");
    pair.setRoles(ImmutableList.of(role.getRoleUUID()));
    bodyJson = Json.newObject();
    pairList = mapper.createArrayNode().add(mapper.convertValue(pair, JsonNode.class));
    bodyJson.put("oidcGroupToYbaRolesPairs", pairList);

    Result res = updateOidcGroupToYbaRoles(bodyJson);
    assertOk(res);

    entity =
        GroupMappingInfo.find
            .query()
            .where()
            .ieq("identifier", "admins")
            .eq("type", "OIDC")
            .findOne();
    ;
    assertNotNull(entity);
    assertEquals(entity.getRoleUUID(), role.getRoleUUID());

    // test delete
    deleteOidcGroupMapping("Admins");
    entity =
        GroupMappingInfo.find
            .query()
            .where()
            .ieq("identifier", "admins")
            .eq("type", "OIDC")
            .findOne();
    ;
    assertNull(entity);

    // try assigning SuperAdmin role
    role = Role.get(cUUID, "SuperAdmin");
    pair.setRoles(ImmutableList.of(role.getRoleUUID()));
    bodyJson = Json.newObject();
    pairList = mapper.createArrayNode().add(mapper.convertValue(pair, JsonNode.class));
    bodyJson.put("oidcGroupToYbaRolesPairs", pairList);
    final ObjectNode requestBody = bodyJson;

    r = assertPlatformException(() -> updateOidcGroupToYbaRoles(requestBody));
    assertBadRequest(r, "Cannot assign SuperAdmin role to groups!");
  }

  @Test
  public void testListOidcMapping() throws IOException {
    Role role1 = Role.get(cUUID, "Admin");
    Role role2 = Role.get(cUUID, "BackupAdmin");
    Role role3 = Role.get(cUUID, "ReadOnly");
    GroupMappingInfo group1 =
        GroupMappingInfo.create(cUUID, role1.getRoleUUID(), "Group-1", GroupType.OIDC);
    GroupMappingInfo group2 =
        GroupMappingInfo.create(cUUID, role2.getRoleUUID(), "Group-2", GroupType.OIDC);
    GroupMappingInfo group3 =
        GroupMappingInfo.create(cUUID, role3.getRoleUUID(), "Group-3", GroupType.OIDC);

    OidcGroupToYbaRolesPair rp1 = new OidcGroupToYbaRolesPair();
    OidcGroupToYbaRolesPair rp2 = new OidcGroupToYbaRolesPair();
    OidcGroupToYbaRolesPair rp3 = new OidcGroupToYbaRolesPair();

    rp1.setGroupName(group1.getIdentifier());
    rp1.setRoles(List.of(group1.getRoleUUID()));

    rp2.setGroupName(group2.getIdentifier());
    rp2.setRoles(List.of(group2.getRoleUUID()));

    rp3.setGroupName(group3.getIdentifier());
    rp3.setRoles(List.of(group3.getRoleUUID()));

    ObjectReader reader = mapper.readerFor(new TypeReference<List<OidcGroupToYbaRolesPair>>() {});
    List<OidcGroupToYbaRolesPair> OidcGroupToYbaRolePairs =
        reader.readValue(fetchOidcGroupToYbaRoles().get("oidcGroupToYbaRolesPairs"));
    assertEquals(OidcGroupToYbaRolePairs.size(), 3);
    assertTrue(OidcGroupToYbaRolePairs.containsAll(ImmutableList.of(rp1, rp2, rp3)));
  }

  @Test
  @Parameters({"true", "false"})
  public void testV1ApiPostMigration(String rbacValue) throws Exception {
    // Create v1 entries for LDAP and OIDC group mappings.
    // Run the migration and assert that the v1 PUT & GET APIs funtion as expected.
    LdapDnToYbaRole lm1 = LdapDnToYbaRole.create(cUUID, "dn1", Users.Role.Admin);
    LdapDnToYbaRole lm2 = LdapDnToYbaRole.create(cUUID, "dn2", Users.Role.BackupAdmin);
    LdapDnToYbaRole lm3 = LdapDnToYbaRole.create(cUUID, "dn3", Users.Role.ReadOnly);

    LdapDnYbaRoleDataPair lp1 = new LdapDnYbaRoleDataPair();
    LdapDnYbaRoleDataPair lp2 = new LdapDnYbaRoleDataPair();
    LdapDnYbaRoleDataPair lp3 = new LdapDnYbaRoleDataPair();

    lp1.setDistinguishedName(lm1.distinguishedName);
    lp2.setDistinguishedName(lm2.distinguishedName);
    lp3.setDistinguishedName(lm3.distinguishedName);

    lp1.setYbaRole(lm1.ybaRole);
    lp2.setYbaRole(lm2.ybaRole);
    lp3.setYbaRole(lm3.ybaRole);

    Role role1 = Role.get(cUUID, "Admin");
    Role role2 = Role.get(cUUID, "BackupAdmin");
    Role role3 = Role.get(cUUID, "ReadOnly");

    OidcGroupToYbaRoles r1 =
        OidcGroupToYbaRoles.create(cUUID, "Group-1", ImmutableList.of(role1.getRoleUUID()));
    OidcGroupToYbaRoles r2 =
        OidcGroupToYbaRoles.create(cUUID, "Group-2", ImmutableList.of(role2.getRoleUUID()));
    OidcGroupToYbaRoles r3 =
        OidcGroupToYbaRoles.create(cUUID, "Group-3", ImmutableList.of(role3.getRoleUUID()));

    OidcGroupToYbaRolesPair rp1 = new OidcGroupToYbaRolesPair();
    OidcGroupToYbaRolesPair rp2 = new OidcGroupToYbaRolesPair();
    OidcGroupToYbaRolesPair rp3 = new OidcGroupToYbaRolesPair();

    rp1.setGroupName(r1.getGroupName());
    rp1.setRoles(r1.getYbaRoles());

    rp2.setGroupName(r2.getGroupName());
    rp2.setRoles(r2.getYbaRoles());

    rp3.setGroupName(r3.getGroupName());
    rp3.setRoles(r3.getYbaRoles());

    setKey(GlobalConfKeys.useNewRbacAuthz.getKey(), rbacValue, GLOBAL_SCOPE_UUID);

    V365__Migrate_Group_Mappings.migrateGroupMappings();

    ObjectReader ldapReader = mapper.readerFor(new TypeReference<List<LdapDnYbaRoleDataPair>>() {});
    List<LdapDnYbaRoleDataPair> LdapDnYbaRoleDataPairs =
        ldapReader.readValue(fetchLdapDnToYbaRoles().get("ldapDnToYbaRolePairs"));
    assertEquals(LdapDnYbaRoleDataPairs.size(), 3);
    assertTrue(LdapDnYbaRoleDataPairs.containsAll(ImmutableList.of(lp1, lp2, lp3)));

    ObjectReader oidcReader =
        mapper.readerFor(new TypeReference<List<OidcGroupToYbaRolesPair>>() {});
    List<OidcGroupToYbaRolesPair> OidcGroupToYbaRolePairs =
        oidcReader.readValue(fetchOidcGroupToYbaRoles().get("oidcGroupToYbaRolesPairs"));
    assertEquals(OidcGroupToYbaRolePairs.size(), 3);
    assertTrue(OidcGroupToYbaRolePairs.containsAll(ImmutableList.of(rp1, rp2, rp3)));

    // Test delete
    deleteOidcGroupMapping("group-1");
  }

  private Result setKey(String path, String newVal, UUID scopeUUID) {
    Http.RequestBuilder request =
        fakeRequest(
                "PUT", String.format(SET_KEY_ENDPOINT, defaultCustomer.getUuid(), scopeUUID, path))
            .header("X-AUTH-TOKEN", authToken)
            .bodyText(newVal);
    return route(request);
  }

  private JsonNode fetchLdapDnToYbaRoles() {
    String authToken = defaultUser.createAuthToken();
    String method = "GET";
    String url = "/api/customers/" + cUUID + "/ldap_mappings";

    Result r = doRequestWithAuthToken(method, url, authToken);
    assertOk(r);
    return Json.parse(contentAsString(r));
  }

  private Result updateLdapDnToYbaRoles(ObjectNode bodyJson) {
    String authToken = defaultUser.createAuthToken();
    String method = "PUT";
    String url = "/api/customers/" + cUUID + "/ldap_mappings";
    return doRequestWithAuthTokenAndBody(method, url, authToken, bodyJson);
  }

  private Result updateOidcGroupToYbaRoles(ObjectNode bodyJson) {
    String authToken = defaultUser.createAuthToken();
    String method = "PUT";
    String url = "/api/customers/" + cUUID + "/oidc_mappings";
    return doRequestWithAuthTokenAndBody(method, url, authToken, bodyJson);
  }

  private JsonNode fetchOidcGroupToYbaRoles() {
    String authToken = defaultUser.createAuthToken();
    String method = "GET";
    String url = "/api/customers/" + cUUID + "/oidc_mappings";

    Result r = doRequestWithAuthToken(method, url, authToken);
    assertOk(r);
    return Json.parse(contentAsString(r));
  }

  private void deleteOidcGroupMapping(String groupName) {
    String authToken = defaultUser.createAuthToken();
    String method = "DELETE";
    String url = "/api/customers/" + cUUID + "/oidc_mappings/" + groupName;

    Result r = doRequestWithAuthToken(method, url, authToken);
    assertOk(r);
  }
}
