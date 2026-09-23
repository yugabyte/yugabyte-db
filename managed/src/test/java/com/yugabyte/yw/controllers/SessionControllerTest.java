// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.ApiUtils.getTestUserIntent;
import static com.yugabyte.yw.common.AssertHelper.*;
import static com.yugabyte.yw.common.FakeApiHelper.routeWithYWErrHandler;
import static com.yugabyte.yw.common.TestHelper.testDatabase;
import static org.hamcrest.CoreMatchers.*;
import static org.junit.Assert.*;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.*;
import static play.inject.Bindings.bind;
import static play.mvc.Http.Status.*;
import static play.test.Helpers.*;

import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.ObjectReader;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.inject.Inject;
import com.nimbusds.jose.JWSAlgorithm;
import com.nimbusds.jose.JWSHeader;
import com.nimbusds.jose.crypto.RSASSASigner;
import com.nimbusds.jwt.JWTClaimsSet;
import com.nimbusds.jwt.SignedJWT;
import com.yugabyte.yw.commissioner.CallHome;
import com.yugabyte.yw.commissioner.HealthChecker;
import com.yugabyte.yw.common.*;
import com.yugabyte.yw.common.alerts.AlertConfigurationWriter;
import com.yugabyte.yw.common.alerts.AlertDestinationService;
import com.yugabyte.yw.common.alerts.QueryAlerts;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.common.pa.PerfAdvisorService;
import com.yugabyte.yw.common.rbac.Permission;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.common.rbac.RoleBindingUtil;
import com.yugabyte.yw.controllers.handlers.ThirdPartyLoginHandler;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.*;
import com.yugabyte.yw.models.GroupMappingInfo.GroupType;
import com.yugabyte.yw.models.Users.UserType;
import com.yugabyte.yw.models.filters.PACollectorFilter;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.rbac.ResourceGroup;
import com.yugabyte.yw.models.rbac.Role;
import com.yugabyte.yw.models.rbac.Role.RoleType;
import com.yugabyte.yw.models.rbac.RoleBinding;
import com.yugabyte.yw.scheduler.Scheduler;
import db.migration.default_.common.R__Sync_System_Roles;
import java.io.IOException;
import java.security.KeyPair;
import java.security.KeyPairGenerator;
import java.security.PrivateKey;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeoutException;
import kamon.instrumentation.play.GuiceModule;
import okhttp3.mockwebserver.MockResponse;
import okhttp3.mockwebserver.MockWebServer;
import org.apache.directory.api.ldap.model.exception.LdapException;
import org.junit.After;
import org.junit.Test;
import org.pac4j.core.client.Clients;
import org.pac4j.core.config.Config;
import org.pac4j.core.context.session.SessionStore;
import org.pac4j.core.http.url.DefaultUrlResolver;
import org.pac4j.core.profile.CommonProfile;
import org.pac4j.core.profile.ProfileManager;
import org.pac4j.oidc.client.OidcClient;
import org.pac4j.oidc.config.OidcConfiguration;
import org.pac4j.oidc.profile.OidcProfile;
import org.pac4j.play.CallbackController;
import org.pac4j.play.java.SecureAction;
import org.pac4j.play.store.PlayCacheSessionStore;
import play.Application;
import play.Environment;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Http.Request;
import play.mvc.Result;
import play.test.Helpers;

public class SessionControllerTest {

  private static class HandlerWithEmailFromCtx extends ThirdPartyLoginHandler {

    @Inject
    public HandlerWithEmailFromCtx(
        Environment env,
        SessionStore playSessionStore,
        RuntimeConfGetter confGetter,
        RoleBindingUtil roleBindingUtil,
        ApiHelper apiHelper) {
      super(env, playSessionStore, confGetter, roleBindingUtil, apiHelper);
    }

    @Override
    public String getEmailFromCtx(Request request) {
      return "test@yugabyte.com";
    }

    @Override
    public CommonProfile getProfile(Request request) {
      OidcProfile oidcProfile = new OidcProfile();
      try {
        String email = "test@yugabyte.com";
        String issuer = "https://random-oidc-issuer.com";

        JWTClaimsSet claimsSet =
            new JWTClaimsSet.Builder()
                .issuer(issuer)
                .claim("email", email)
                .claim("groups", ImmutableList.of("Admin Group", "BackupAdmin Group"))
                .build();

        KeyPairGenerator generator = KeyPairGenerator.getInstance("RSA");
        generator.initialize(2048);
        KeyPair keyPair = generator.generateKeyPair();
        PrivateKey privateKey = keyPair.getPrivate();

        SignedJWT jwtToken =
            new SignedJWT(
                new JWSHeader.Builder(JWSAlgorithm.RS256).keyID("keyID").build(), claimsSet);
        jwtToken.sign(new RSASSASigner(privateKey));

        oidcProfile.setIdTokenString(jwtToken.serialize());
        oidcProfile.addAttribute("issuer", issuer);
      } catch (Exception e) {
        fail("Creating OidcProfile failed with error: " + e.getMessage());
      }
      return oidcProfile;
    }
  }

  private AlertDestinationService alertDestinationService;

  private Application app;

  private SettableRuntimeConfigFactory settableRuntimeConfigFactory;

  private LdapUtil ldapUtil;

  private final Config mockPac4jConfig = mock(Config.class);
  private final SecureAction mockSecureAction = mock(SecureAction.class);

  // Define test permissions to use later.
  private Permission permission1 = new Permission(ResourceType.UNIVERSE, Action.CREATE);
  private Permission permission2 = new Permission(ResourceType.UNIVERSE, Action.READ);

  private void startApp(boolean isMultiTenant) {
    startApp(isMultiTenant, ImmutableMap.of());
  }

  private void startApp(boolean isMultiTenant, Map<String, Object> additionalConfig) {
    HealthChecker mockHealthChecker = mock(HealthChecker.class);
    Scheduler mockScheduler = mock(Scheduler.class);
    CallHome mockCallHome = mock(CallHome.class);
    CallbackController mockCallbackController = mock(CallbackController.class);
    PlayCacheSessionStore mockSessionStore = mock(PlayCacheSessionStore.class);
    QueryAlerts mockQueryAlerts = mock(QueryAlerts.class);
    AlertConfigurationWriter mockAlertConfigurationWriter = mock(AlertConfigurationWriter.class);
    ldapUtil = mock(LdapUtil.class);
    final Clients clients =
        new Clients("/api/v1/callback", new OidcClient(new OidcConfiguration()));
    clients.setUrlResolver(new DefaultUrlResolver(true));
    final Config config = new Config(clients);
    config.setHttpActionAdapter(new PlatformHttpActionAdapter());
    // This test does not extend PlatformGuiceApplicationBaseTest, so reset the shared embedded
    // Postgres to a clean, freshly-migrated state before building the application (the base class
    // does this in its @Before for the tests that extend it).
    TestPostgres.ensureStarted();
    TestPostgres.resetDatabase();
    app =
        new GuiceApplicationBuilder()
            .disable(GuiceModule.class)
            .configure(testDatabase())
            .configure(ImmutableMap.of("yb.multiTenant", isMultiTenant))
            .configure(additionalConfig)
            .overrides(bind(Scheduler.class).toInstance(mockScheduler))
            .overrides(bind(HealthChecker.class).toInstance(mockHealthChecker))
            .overrides(bind(CallHome.class).toInstance(mockCallHome))
            .overrides(bind(CallbackController.class).toInstance(mockCallbackController))
            .overrides(bind(SessionStore.class).toInstance(mockSessionStore))
            .overrides(bind(QueryAlerts.class).toInstance(mockQueryAlerts))
            .overrides(
                bind(AlertConfigurationWriter.class).toInstance(mockAlertConfigurationWriter))
            .overrides(bind(LdapUtil.class).toInstance(ldapUtil))
            .overrides(bind(ThirdPartyLoginHandler.class).to(HandlerWithEmailFromCtx.class))
            .overrides(bind(org.pac4j.core.config.Config.class).toInstance(config))
            .build();
    Helpers.start(app);
    // Helpers.stop() runs CoordinatedShutdown, which sets this static; clear it so
    // ShutdownRejectFilter does not 503 subsequent tests in the same JVM.
    Util.resetYbaShutdownStarted();

    alertDestinationService = app.injector().instanceOf(AlertDestinationService.class);
    settableRuntimeConfigFactory = app.injector().instanceOf(SettableRuntimeConfigFactory.class);
  }

  @After
  public void tearDown() {
    Helpers.stop(app);
    TestHelper.shutdownDatabase();
  }

  @Test
  public void testSSO_noUserFound()
      throws InterruptedException, ExecutionException, TimeoutException {
    startApp(false);
    authorizeUserMockSetup(); // authorize "test@yugabyte.com"

    settableRuntimeConfigFactory
        .globalRuntimeConf()
        .setValue("yb.security.oidc_enable_auto_create_users", "false");
    Customer customer = ModelFactory.testCustomer("Test Customer 1");
    Users user = ModelFactory.testUser(customer, "not.matching@yugabyte.com");

    Result result = routeWithYWErrHandler(app, fakeRequest("GET", "/api/third_party_login"));
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(UNAUTHORIZED, result.status());
    assertThat(
        json.get("error").toString(),
        allOf(notNullValue(), containsString("User not found: test@yugabyte.com")));
  }

  @Test
  public void testSSO_userFound() {
    startApp(false);
    authorizeUserMockSetup(); // authorize "test@yugabyte.com"

    settableRuntimeConfigFactory
        .globalRuntimeConf()
        .setValue("yb.security.oidc_enable_auto_create_users", "false");
    Customer customer = ModelFactory.testCustomer("Test Customer 1");
    Users user = ModelFactory.testUser(customer, "test@yugabyte.com", Users.Role.Admin);

    Result result = route(app, fakeRequest("GET", "/api/third_party_login"));
    assertEquals("Headers:" + result.headers(), SEE_OTHER, result.status());
    assertEquals("/", result.headers().get("Location")); // Redirect
  }

  @Test
  public void testAutoCreateUserOnSSO() throws Exception {
    startApp(false);
    authorizeUserMockSetup(); // authorize "test@yugabyte.com"
    Customer customer = ModelFactory.testCustomer("Test Customer 1");
    R__Sync_System_Roles.syncSystemRoles();

    GroupMappingInfo group1 =
        GroupMappingInfo.create(
            customer.getUuid(),
            Role.get(customer.getUuid(), "Admin").getRoleUUID(),
            "Admin Group",
            GroupType.OIDC);
    GroupMappingInfo group2 =
        GroupMappingInfo.create(
            customer.getUuid(),
            Role.get(customer.getUuid(), "BackupAdmin").getRoleUUID(),
            "BackupAdmin Group",
            GroupType.OIDC);

    Result result = route(app, fakeRequest("GET", "/api/third_party_login"));
    assertEquals("Headers:" + result.headers(), SEE_OTHER, result.status());
    assertEquals("/", result.headers().get("Location")); // Redirect

    Users user = Users.find.query().where().eq("email", "test@yugabyte.com").findOne();
    // assert user created
    assertNotNull(user);
    // assert correct role assigned
    assertEquals(Users.Role.Admin, user.getRole());
  }

  @Test
  public void testCustomRolesMapping() throws Exception {
    startApp(false);
    authorizeUserMockSetup(); // authorize "test@yugabyte.com"
    settableRuntimeConfigFactory.globalRuntimeConf().setValue("yb.rbac.use_new_authz", "true");
    R__Sync_System_Roles.syncSystemRoles();
    Customer customer = ModelFactory.testCustomer("Test Customer 1");
    Users superAdmin = ModelFactory.testSuperAdminUserNewRbac(customer);
    String authToken = superAdmin.createAuthToken();
    // create custom role
    Role customRole =
        Role.create(
            customer.getUuid(),
            "testCustomRole",
            "testDescription",
            RoleType.Custom,
            new HashSet<>(Arrays.asList(permission1, permission2)));

    // create group mapping
    GroupMappingInfo group1 =
        GroupMappingInfo.create(
            customer.getUuid(),
            Role.get(customer.getUuid(), "ConnectOnly").getRoleUUID(),
            "Admin Group",
            GroupType.OIDC);

    RoleBinding roleBinding1 =
        RoleBinding.create(
            group1,
            RoleBinding.RoleBindingType.Custom,
            customRole,
            new ResourceGroup(
                new HashSet<>(
                    Arrays.asList(
                        ResourceGroup.ResourceDefinition.builder()
                            .resourceType(ResourceType.UNIVERSE)
                            .allowAll(true)
                            .build(),
                        ResourceGroup.ResourceDefinition.builder()
                            .resourceType(ResourceType.OTHER)
                            .allowAll(true)
                            .build()))));

    Result result = route(app, fakeRequest("GET", "/api/third_party_login"));
    assertEquals("Headers:" + result.headers(), SEE_OTHER, result.status());
    assertEquals("/", result.headers().get("Location")); // Redirect

    // check user.groupmembership on login
    Users user = Users.find.query().where().eq("email", "test@yugabyte.com").findOne();
    assertNotNull(user);
    assertNotNull(user.getGroupMemberships());
    assertTrue(user.getGroupMemberships().contains(group1.getGroupUUID()));
    // call role binding api and check user has the correct role bindings
    result = listRoleBindings(customer.getUuid(), user.getUuid(), authToken);
    assertEquals(OK, result.status());

    ObjectMapper mapper = new ObjectMapper();
    JsonNode json = Json.parse(contentAsString(result));
    ObjectReader reader = mapper.readerFor(new TypeReference<Map<UUID, List<RoleBinding>>>() {});
    Map<UUID, List<RoleBinding>> roleBindingList = reader.readValue(json);
    assertTrue(roleBindingList.get(user.getUuid()).contains(roleBinding1));
    // delete group mapping
    group1.delete();
    // call role binding api and check user has the correct role bindings
    result = listRoleBindings(customer.getUuid(), user.getUuid(), authToken);
    assertEquals(OK, result.status());

    json = Json.parse(contentAsString(result));
    reader = mapper.readerFor(new TypeReference<Map<UUID, List<RoleBinding>>>() {});
    roleBindingList = reader.readValue(json);
    assertFalse(roleBindingList.get(user.getUuid()).contains(roleBinding1));
  }

  private Result listRoleBindings(UUID customerUUID, UUID userUUID, String authToken) {
    String uri = "";
    if (userUUID == null) {
      uri = String.format("/api/customers/%s/rbac/role_binding", customerUUID.toString());
    } else {
      uri =
          String.format(
              "/api/customers/%s/rbac/role_binding?userUUID=%s",
              customerUUID.toString(), userUUID.toString());
    }
    return route(app, fakeRequest("GET", uri).header("X-AUTH-TOKEN", authToken));
  }

  public void authorizeUserMockSetup() {
    CommonProfile mockProfile = mock(CommonProfile.class);
    when(mockProfile.getEmail()).thenReturn("test@yugabyte.com");
    final Config pac4jConfig = app.injector().instanceOf(Config.class);
    ProfileManager mockProfileManager = mock(ProfileManager.class);
    doReturn(ImmutableList.of(mockProfile)).when(mockProfileManager).getProfiles();
    doReturn(Optional.of(mockProfile)).when(mockProfileManager).getProfile(CommonProfile.class);
    pac4jConfig.setProfileManagerFactory((webContext, mockSessionStore) -> mockProfileManager);
    PlayCacheSessionStore mockSessionStore = mock(PlayCacheSessionStore.class);
    pac4jConfig.setSessionStoreFactory(p -> mockSessionStore);
    doReturn(new PlatformHttpActionAdapter()).when(mockPac4jConfig).getHttpActionAdapter();
  }

  @Test
  public void testValidLogin() {
    startApp(false);
    Customer customer = ModelFactory.testCustomer();
    ModelFactory.testUser(customer);
    ObjectNode loginJson = Json.newObject();
    loginJson.put("email", "test@customer.com");
    loginJson.put("password", "password");
    Result result = route(app, fakeRequest("POST", "/api/login").bodyJson(loginJson));
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertNotNull(json.get("authToken"));
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testValidAPILogin() {
    startApp(false);
    Customer customer = ModelFactory.testCustomer();
    ModelFactory.testUser(customer);
    ObjectNode loginJson = Json.newObject();
    loginJson.put("email", "test@customer.com");
    loginJson.put("password", "password");
    Result result = route(app, fakeRequest("POST", "/api/api_login").bodyJson(loginJson));
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertNull("UI Session should not be created", json.get("authToken"));
    assertNotNull(json.get("apiToken"));
    assertEquals(1L, json.get("apiTokenVersion").asLong());
    assertAuditEntry(1, customer.getUuid());
  }

  // ---------------------------------------------------------------------------------------------
  // allow_local_login_with_sso == false restricts local login to SuperAdmins holding a local YBA
  // account (PLAT-22335). LDAP and OIDC SuperAdmins must use SSO; API tokens are never gated.
  // ---------------------------------------------------------------------------------------------

  private void restrictLocalLogin(boolean restricted) {
    settableRuntimeConfigFactory.globalRuntimeConf().setValue("yb.security.use_oauth", "true");
    settableRuntimeConfigFactory
        .globalRuntimeConf()
        .setValue("yb.security.allow_local_login_with_sso", String.valueOf(!restricted));
    settableRuntimeConfigFactory.globalRuntimeConf().setValue("yb.rbac.use_new_authz", "true");
  }

  /** Mirrors RBACController.setRoleBindings: SuperAdmin lives only in the role binding. */
  private Users nonPrimarySuperAdmin(Customer customer, String email) {
    Users user = ModelFactory.testUser(customer, email, Users.Role.ConnectOnly);
    RoleBinding.getAll(user.getUuid()).forEach(RoleBinding::delete);
    RoleBinding.create(
        user,
        RoleBinding.RoleBindingType.Custom,
        Role.get(customer.getUuid(), Users.Role.SuperAdmin.name()),
        // Derived from the role, not users.role -- exactly what populateSystemRoleResourceGroups
        // does. Deriving it from a ConnectOnly user yields a restricted resource group.
        ResourceGroup.getSystemDefaultResourceGroup(
            customer.getUuid(), user.getUuid(), Users.Role.SuperAdmin));
    return user;
  }

  private Users superAdminOfType(Customer customer, String email, UserType userType) {
    Users user = ModelFactory.testUser(customer, email, Users.Role.SuperAdmin);
    user.setUserType(userType);
    user.save();
    return user;
  }

  private Result login(String email) throws Exception {
    ObjectNode loginJson = Json.newObject();
    loginJson.put("email", email);
    loginJson.put("password", "password");
    return routeWithYWErrHandler(app, fakeRequest("POST", "/api/login").bodyJson(loginJson));
  }

  private void assertLoginRejected(Result result) {
    assertEquals(UNAUTHORIZED, result.status());
    assertThat(
        Json.parse(contentAsString(result)).get("error").toString(),
        allOf(notNullValue(), containsString("Local login is not permitted")));
  }

  private static String allowLocalLoginKeyUrl(Customer customer) {
    return String.format(
        "/api/v1/customers/%s/runtime_config/%s/key/yb.security.allow_local_login_with_sso",
        customer.getUuid(), ScopedRuntimeConfig.GLOBAL_SCOPE_UUID);
  }

  @Test
  public void testPrimarySuperAdminAllowedWhenLocalLoginRestricted() throws Exception {
    startApp(false);
    Customer customer = ModelFactory.testCustomer();
    ModelFactory.testUser(customer, "primary-sa@customer.com", Users.Role.SuperAdmin);
    restrictLocalLogin(true);

    Result result = login("primary-sa@customer.com");
    assertEquals(OK, result.status());
    assertNotNull(Json.parse(contentAsString(result)).get("authToken"));
  }

  @Test
  public void testNonPrimarySuperAdminAllowedWhenLocalLoginRestricted() throws Exception {
    startApp(false);
    Customer customer = ModelFactory.testCustomer();
    nonPrimarySuperAdmin(customer, "rbac-sa@customer.com");
    restrictLocalLogin(true);

    // PLAT-22335: SuperAdmin lives in the role binding, while users.role still reads ConnectOnly.
    Result result = login("rbac-sa@customer.com");
    assertEquals(OK, result.status());
    assertNotNull(Json.parse(contentAsString(result)).get("authToken"));
  }

  @Test
  public void testLdapSuperAdminRejectedWithoutBindingWhenLocalLoginRestricted() throws Exception {
    startApp(false);
    Customer customer = ModelFactory.testCustomer();
    superAdminOfType(customer, "ldap-sa@customer.com", UserType.ldap);
    settableRuntimeConfigFactory.globalRuntimeConf().setValue("yb.security.ldap.use_ldap", "true");
    restrictLocalLogin(true);

    // Denied without a bind, so they never authenticate and fall to the generic rejection rather
    // than the gate's message -- deliberate: a specific message here would let an unauthenticated
    // caller distinguish an LDAP account from a local or nonexistent one.
    Result result = login("ldap-sa@customer.com");
    assertEquals(UNAUTHORIZED, result.status());
    assertThat(
        Json.parse(contentAsString(result)).get("error").toString(),
        allOf(notNullValue(), containsString("Invalid User Credentials")));
    // The bind must never be attempted: loginWithLdap provisions users and rewrites role bindings.
    verify(ldapUtil, never()).loginWithLdap(any());

    settableRuntimeConfigFactory.globalRuntimeConf().setValue("yb.security.ldap.use_ldap", "false");
  }

  @Test
  public void testOidcSuperAdminRejectedWhenLocalLoginRestricted() throws Exception {
    startApp(false);
    Customer customer = ModelFactory.testCustomer();
    superAdminOfType(customer, "oidc-sa@customer.com", UserType.oidc);
    restrictLocalLogin(true);

    assertLoginRejected(login("oidc-sa@customer.com"));
  }

  @Test
  public void testSsoRefusedForRbacGrantedLocalSuperAdmin() throws Exception {
    startApp(false);
    authorizeUserMockSetup(); // authorize "test@yugabyte.com"
    settableRuntimeConfigFactory.globalRuntimeConf().setValue("yb.rbac.use_new_authz", "true");
    R__Sync_System_Roles.syncSystemRoles();
    Customer customer = ModelFactory.testCustomer("Test Customer 1");
    nonPrimarySuperAdmin(customer, "test@yugabyte.com");

    // users.role still reads ConnectOnly, so the legacy check let this user through and
    // findUserByEmailOrCreateNewUser would then strip their SuperAdmin binding.
    Result result = routeWithYWErrHandler(app, fakeRequest("GET", "/api/third_party_login"));
    assertEquals(FORBIDDEN, result.status());
    assertThat(
        Json.parse(contentAsString(result)).get("error").toString(),
        allOf(notNullValue(), containsString("SuperAdmin is not allowed login via SSO")));
  }

  @Test
  public void testSsoStillAllowedForExternalSuperAdmin() throws Exception {
    startApp(false);
    authorizeUserMockSetup(); // authorize "test@yugabyte.com"
    settableRuntimeConfigFactory
        .globalRuntimeConf()
        .setValue("yb.security.oidc_enable_auto_create_users", "false");
    settableRuntimeConfigFactory.globalRuntimeConf().setValue("yb.rbac.use_new_authz", "true");
    R__Sync_System_Roles.syncSystemRoles();
    Customer customer = ModelFactory.testCustomer("Test Customer 1");
    superAdminOfType(customer, "test@yugabyte.com", UserType.ldap);

    // The block is scoped to local accounts, so an SSO-provisioned SuperAdmin keeps SSO -- the
    // second half of the rule the original comment states, and what PLAT-17540 added.
    Result result = route(app, fakeRequest("GET", "/api/third_party_login"));
    assertEquals("Headers:" + result.headers(), SEE_OTHER, result.status());
  }

  @Test
  public void testWrongPasswordDoesNotRevealThatAnAccountIsExternal() throws Exception {
    startApp(false);
    Customer customer = ModelFactory.testCustomer();
    superAdminOfType(customer, "oidc-sa@customer.com", UserType.oidc);
    restrictLocalLogin(true);

    // Without a valid password the response must be indistinguishable from a nonexistent account,
    // so /api/login cannot be used to enumerate accounts or classify their auth backend. The
    // account-type message is only reachable once the caller has proved they own the account.
    ObjectNode loginJson = Json.newObject();
    loginJson.put("email", "oidc-sa@customer.com");
    loginJson.put("password", "definitely-wrong");
    Result wrongPassword =
        routeWithYWErrHandler(app, fakeRequest("POST", "/api/login").bodyJson(loginJson));
    loginJson.put("email", "no-such-user@customer.com");
    Result noSuchUser =
        routeWithYWErrHandler(app, fakeRequest("POST", "/api/login").bodyJson(loginJson));

    assertEquals(UNAUTHORIZED, wrongPassword.status());
    assertEquals(noSuchUser.status(), wrongPassword.status());
    assertEquals(contentAsString(noSuchUser), contentAsString(wrongPassword));
  }

  @Test
  public void testNonSuperAdminRejectedWhenLocalLoginRestricted() throws Exception {
    startApp(false);
    Customer customer = ModelFactory.testCustomer();
    ModelFactory.testUser(customer, "admin@customer.com", Users.Role.Admin);
    restrictLocalLogin(true);

    assertLoginRejected(login("admin@customer.com"));
  }

  @Test
  public void testGroupDerivedSuperAdminAllowedWhenLocalLoginRestricted() throws Exception {
    startApp(false);
    Customer customer = ModelFactory.testCustomer();
    Users user = ModelFactory.testUser(customer, "group-sa@customer.com", Users.Role.ConnectOnly);
    RoleBinding.getAll(user.getUuid()).forEach(RoleBinding::delete);
    GroupMappingInfo group =
        GroupMappingInfo.create(
            customer.getUuid(),
            Role.get(customer.getUuid(), "ConnectOnly").getRoleUUID(),
            "sa-group",
            GroupType.LDAP);
    RoleBinding.create(
        group,
        RoleBinding.RoleBindingType.Custom,
        Role.get(customer.getUuid(), Users.Role.SuperAdmin.name()),
        ResourceGroup.getSystemDefaultResourceGroup(
            customer.getUuid(), user.getUuid(), Users.Role.SuperAdmin));
    user.setGroupMemberships(new HashSet<>(Arrays.asList(group.getGroupUUID())));
    user.save();
    restrictLocalLogin(true);

    // SuperAdmin reached only by traversing group memberships.
    assertEquals(OK, login("group-sa@customer.com").status());
  }

  @Test
  public void testAllRolesAllowedWhenLocalLoginNotRestricted() throws Exception {
    startApp(false);
    Customer customer = ModelFactory.testCustomer();
    ModelFactory.testUser(customer, "primary-sa@customer.com", Users.Role.SuperAdmin);
    nonPrimarySuperAdmin(customer, "rbac-sa@customer.com");
    ModelFactory.testUser(customer, "admin@customer.com", Users.Role.Admin);
    restrictLocalLogin(false);

    for (String email :
        ImmutableList.of("primary-sa@customer.com", "rbac-sa@customer.com", "admin@customer.com")) {
      assertEquals("login for " + email, OK, login(email).status());
    }
  }

  @Test
  public void testLocalLoginUnaffectedWhenSsoDisabled() throws Exception {
    startApp(false);
    Customer customer = ModelFactory.testCustomer();
    ModelFactory.testUser(customer, "admin@customer.com", Users.Role.Admin);
    // The gate is scoped to SSO deployments; the flag alone must not restrict anything.
    settableRuntimeConfigFactory.globalRuntimeConf().setValue("yb.security.use_oauth", "false");
    settableRuntimeConfigFactory
        .globalRuntimeConf()
        .setValue("yb.security.allow_local_login_with_sso", "false");

    assertEquals(OK, login("admin@customer.com").status());
  }

  @Test
  public void testApiTokenUnaffectedForEverySuperAdminFlavour() {
    startApp(false);
    Customer customer = ModelFactory.testCustomer();
    String primaryToken =
        ModelFactory.testUser(customer, "primary-sa@customer.com", Users.Role.SuperAdmin)
            .upsertApiToken();
    String nonPrimaryToken =
        nonPrimarySuperAdmin(customer, "rbac-sa@customer.com").upsertApiToken();
    String ldapToken =
        superAdminOfType(customer, "ldap-sa@customer.com", UserType.ldap).upsertApiToken();
    restrictLocalLogin(true);

    // Even the flavours denied local login keep full API access, including re-enabling the flag.
    for (String apiToken : ImmutableList.of(primaryToken, nonPrimaryToken, ldapToken)) {
      assertEquals(
          OK,
          FakeApiHelper.doRequestWithApiTokenAndTextBody(
                  app, "PUT", allowLocalLoginKeyUrl(customer), apiToken, "true")
              .status());
      settableRuntimeConfigFactory
          .globalRuntimeConf()
          .setValue("yb.security.allow_local_login_with_sso", "false");
    }
  }

  @Test
  public void testSessionTokenFollowsTheSameRuleAsLogin() {
    startApp(false);
    Customer customer = ModelFactory.testCustomer();
    Users localSa =
        ModelFactory.testUser(customer, "primary-sa@customer.com", Users.Role.SuperAdmin);
    Users ldapSa = superAdminOfType(customer, "ldap-sa@customer.com", UserType.ldap);
    String localAuth = localSa.createAuthToken();
    String ldapAuth = ldapSa.createAuthToken();
    String ldapApi = ldapSa.upsertApiToken();
    restrictLocalLogin(true);

    String url = allowLocalLoginKeyUrl(customer);
    assertEquals(OK, FakeApiHelper.doRequestWithAuthToken(app, "GET", url, localAuth).status());
    assertEquals(
        UNAUTHORIZED, FakeApiHelper.doRequestWithAuthToken(app, "GET", url, ldapAuth).status());
    assertEquals(OK, FakeApiHelper.doRequestWithApiToken(app, "GET", url, ldapApi).status());
  }

  @Test
  public void testLoginWithInvalidPassword()
      throws InterruptedException, ExecutionException, TimeoutException {
    startApp(false);
    Customer customer = ModelFactory.testCustomer();
    ModelFactory.testUser(customer);
    ObjectNode loginJson = Json.newObject();
    loginJson.put("email", "test@customer.com");
    loginJson.put("password", "password1");
    Result result =
        routeWithYWErrHandler(app, fakeRequest("POST", "/api/login").bodyJson(loginJson));
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(UNAUTHORIZED, result.status());
    assertThat(
        json.get("error").toString(),
        allOf(notNullValue(), containsString("Invalid User Credentials")));
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testLoginWithNullPassword() {
    startApp(false);
    Customer customer = ModelFactory.testCustomer();
    ModelFactory.testUser(customer);
    ObjectNode loginJson = Json.newObject();
    loginJson.put("email", "test@customer.com");
    Result result =
        assertPlatformException(
            () -> route(app, fakeRequest("POST", "/api/login").bodyJson(loginJson)));
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(BAD_REQUEST, result.status());
    assertThat(
        json.get("error").toString(),
        allOf(notNullValue(), containsString("{\"password\":[\"This field is required\"]}")));
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testValidLoginWithLdap() throws LdapException {
    startApp(false);
    Customer customer = ModelFactory.testCustomer();
    Users user = ModelFactory.testUser(customer);
    user.setUserType(UserType.ldap);
    user.save();
    ObjectNode loginJson = Json.newObject();
    loginJson.put("email", "test@customer.com");
    loginJson.put("password", "password");
    settableRuntimeConfigFactory.globalRuntimeConf().setValue("yb.security.ldap.use_ldap", "true");
    when(ldapUtil.loginWithLdap(any())).thenReturn(user);
    Result result = route(app, fakeRequest("POST", "/api/login").bodyJson(loginJson));
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertNotNull(json.get("authToken"));
    assertAuditEntry(1, customer.getUuid());

    settableRuntimeConfigFactory.globalRuntimeConf().setValue("yb.security.ldap.use_ldap", "false");
  }

  @Test
  public void testInvalidLoginWithLdap() throws LdapException {
    startApp(false);
    Customer customer = ModelFactory.testCustomer();
    Users user = ModelFactory.testUser(customer);
    user.setUserType(UserType.ldap);
    user.save();
    ObjectNode loginJson = Json.newObject();
    loginJson.put("email", "test@customer.com");
    loginJson.put("password", "password1");
    settableRuntimeConfigFactory.globalRuntimeConf().setValue("yb.security.ldap.use_ldap", "true");
    when(ldapUtil.loginWithLdap(any())).thenReturn(null);
    Result result =
        assertPlatformException(
            () -> route(app, fakeRequest("POST", "/api/login").bodyJson(loginJson)));
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(UNAUTHORIZED, result.status());
    assertThat(
        json.get("error").toString(),
        allOf(notNullValue(), containsString("Invalid User Credentials")));
    assertAuditEntry(0, customer.getUuid());

    settableRuntimeConfigFactory.globalRuntimeConf().setValue("yb.security.ldap.use_ldap", "false");
  }

  @Test
  public void testLdapUserWithoutLdapConfig() {
    startApp(false);
    Customer customer = ModelFactory.testCustomer();
    Users user = ModelFactory.testUser(customer);
    user.setUserType(UserType.ldap);
    user.save();
    ObjectNode loginJson = Json.newObject();
    loginJson.put("email", "test@customer.com");
    loginJson.put("password", "password");
    Result result =
        assertPlatformException(
            () -> route(app, fakeRequest("POST", "/api/login").bodyJson(loginJson)));
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(UNAUTHORIZED, result.status());
    assertThat(
        json.get("error").toString(),
        allOf(notNullValue(), containsString("Invalid User Credentials")));
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testLocalUserWithLdapConfigured() throws LdapException {
    startApp(false);
    Customer customer = ModelFactory.testCustomer();
    Users user = ModelFactory.testUser(customer);
    ObjectNode loginJson = Json.newObject();
    loginJson.put("email", "test@customer.com");
    loginJson.put("password", "password");
    settableRuntimeConfigFactory.globalRuntimeConf().setValue("yb.security.ldap.use_ldap", "true");
    when(ldapUtil.loginWithLdap(any())).thenReturn(null);
    Result result = route(app, fakeRequest("POST", "/api/login").bodyJson(loginJson));
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertNotNull(json.get("authToken"));
    assertAuditEntry(1, customer.getUuid());

    settableRuntimeConfigFactory.globalRuntimeConf().setValue("yb.security.ldap.use_ldap", "false");
  }

  @Test
  public void testInsecureLoginValid() {
    startApp(false);
    Customer customer = ModelFactory.testCustomer("Test Customer 1");
    ModelFactory.testUser(customer, "tc1@test.com", Users.Role.ReadOnly);
    ConfigHelper configHelper = new ConfigHelper();
    configHelper.loadConfigToDB(
        ConfigHelper.ConfigType.Security, ImmutableMap.of("level", "insecure"));

    Result result = route(app, fakeRequest("GET", "/api/insecure_login"));
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertNotNull(json.get("apiToken"));
    assertEquals(1L, json.get("apiTokenVersion").asLong());
    assertNotNull(json.get("customerUUID"));
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testInsecureLoginWithoutReadOnlyUser()
      throws InterruptedException, ExecutionException, TimeoutException {
    startApp(false);
    Customer customer = ModelFactory.testCustomer("Test Customer 1");
    ModelFactory.testUser(customer, "tc1@test.com", Users.Role.Admin);
    ConfigHelper configHelper = new ConfigHelper();
    configHelper.loadConfigToDB(
        ConfigHelper.ConfigType.Security, ImmutableMap.of("level", "insecure"));

    Result result = routeWithYWErrHandler(app, fakeRequest("GET", "/api/insecure_login"));
    assertForbiddenWithException(result, "No read only customer exists.");
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testInsecureLoginInvalid()
      throws InterruptedException, ExecutionException, TimeoutException {
    startApp(false);
    Customer customer = ModelFactory.testCustomer("Test Customer 1");
    ModelFactory.testUser(customer);

    Result result = routeWithYWErrHandler(app, fakeRequest("GET", "/api/insecure_login"));

    assertForbiddenWithException(result, "Insecure login unavailable.");
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testRegisterCustomer() {
    startApp(true);
    ObjectNode registerJson = Json.newObject();
    registerJson.put("code", "fb");
    registerJson.put("email", "foo2@bar.com");
    registerJson.put("password", "pAssw_0rd");
    registerJson.put("name", "Foo");

    Result result =
        route(
            app, fakeRequest("POST", "/api/register?generateApiToken=true").bodyJson(registerJson));
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertNotNull(json.get("authToken"));
    assertNotNull(json.get("apiToken"));
    assertEquals(1L, json.get("apiTokenVersion").asLong());
    Customer c1 = Customer.get(UUID.fromString(json.get("customerUUID").asText()));
    assertAuditEntry(1, c1.getUuid());

    ObjectNode loginJson = Json.newObject();
    loginJson.put("email", "foo2@bar.com");
    loginJson.put("password", "pAssw_0rd");
    result = route(app, fakeRequest("POST", "/api/login").bodyJson(loginJson));
    json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertNotNull(json.get("authToken"));
    assertAuditEntry(2, c1.getUuid());
    assertNotNull(alertDestinationService.getDefaultDestination(c1.getUuid()));
  }

  @Test
  public void testRegisterCustomerWrongPassword()
      throws InterruptedException, ExecutionException, TimeoutException {
    startApp(true);
    ObjectNode registerJson = Json.newObject();
    registerJson.put("code", "fb");
    registerJson.put("email", "foo2@bar.com");
    registerJson.put("password", "pAssw0rd");
    registerJson.put("name", "Foo");

    Result result =
        routeWithYWErrHandler(app, fakeRequest("POST", "/api/register").bodyJson(registerJson));

    assertEquals(BAD_REQUEST, result.status());
  }

  @Test
  public void testRegisterMultiCustomer()
      throws InterruptedException, ExecutionException, TimeoutException {
    startApp(true);
    ObjectNode registerJson = Json.newObject();
    registerJson.put("code", "fb");
    registerJson.put("email", "foo2@bar.com");
    registerJson.put("password", "pAssw_0rd");
    registerJson.put("name", "Foo");

    Result result = route(app, fakeRequest("POST", "/api/register").bodyJson(registerJson));
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertNotNull(json.get("authToken"));
    String authToken = json.get("authToken").asText();
    Customer c1 = Customer.get(UUID.fromString(json.get("customerUUID").asText()));
    Users user = Users.get(UUID.fromString(json.get("userUUID").asText()));
    assertEquals(Users.Role.SuperAdmin, user.getRole());
    assertAuditEntry(1, c1.getUuid());

    ObjectNode registerJson2 = Json.newObject();
    registerJson2.put("code", "fb");
    registerJson2.put("email", "foo3@bar.com");
    registerJson2.put("password", "pAssw_0rd");
    registerJson2.put("name", "Foo");

    result =
        route(
            app,
            fakeRequest("POST", "/api/register")
                .bodyJson(registerJson2)
                .header("X-AUTH-TOKEN", authToken));
    json = Json.parse(contentAsString(result));

    // Register duplicate
    assertEquals(OK, result.status());
    assertNotNull(json.get("authToken"));
    assertAuditEntry(2, c1.getUuid());
    checkCount("count", "2");
    result =
        routeWithYWErrHandler(
            app,
            fakeRequest("POST", "/api/register")
                .bodyJson(registerJson2)
                .header("X-AUTH-TOKEN", authToken));
    assertConflict(result, "Customer already registered.");
    checkCount("count", "2"); // Make sure that count stays 2
    // TODO(amalyshev): also check that alert config was rolled back
  }

  public void checkCount(String count, String s2) {
    Result result;
    JsonNode json;
    result = route(app, fakeRequest("GET", "/api/customer_count"));
    json = Json.parse(contentAsString(result));
    assertOk(result);
    assertValue(json, count, s2);
  }

  @Test
  public void testRegisterMultiCustomerNoAuth()
      throws InterruptedException, ExecutionException, TimeoutException {
    startApp(true);
    ObjectNode registerJson = Json.newObject();
    registerJson.put("code", "fb");
    registerJson.put("email", "foo2@bar.com");
    registerJson.put("password", "pAssw_0rd");
    registerJson.put("name", "Foo");

    Result result = route(app, fakeRequest("POST", "/api/register").bodyJson(registerJson));
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertNotNull(json.get("authToken"));
    String authToken = json.get("authToken").asText();
    Customer c1 = Customer.get(UUID.fromString(json.get("customerUUID").asText()));

    ObjectNode registerJson2 = Json.newObject();
    registerJson2.put("code", "fb");
    registerJson2.put("email", "foo3@bar.com");
    registerJson2.put("password", "pAssw_0rd");
    registerJson2.put("name", "Foo");

    result =
        routeWithYWErrHandler(app, fakeRequest("POST", "/api/register").bodyJson(registerJson2));

    assertBadRequest(result, "Only Super Admins can register tenant.");
  }

  @Test
  public void testRegisterMultiCustomerWrongAuth()
      throws InterruptedException, ExecutionException, TimeoutException {
    startApp(true);
    ObjectNode registerJson = Json.newObject();
    registerJson.put("code", "fb");
    registerJson.put("email", "foo2@bar.com");
    registerJson.put("password", "pAssw_0rd");
    registerJson.put("name", "Foo");

    Result result = route(app, fakeRequest("POST", "/api/register").bodyJson(registerJson));
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertNotNull(json.get("authToken"));
    String authToken = json.get("authToken").asText();
    Customer c1 = Customer.get(UUID.fromString(json.get("customerUUID").asText()));

    ObjectNode registerJson2 = Json.newObject();
    registerJson2.put("code", "fb");
    registerJson2.put("email", "foo3@bar.com");
    registerJson2.put("password", "pAssw_0rd");
    registerJson2.put("name", "Foo");

    result =
        route(
            app,
            fakeRequest("POST", "/api/register")
                .bodyJson(registerJson2)
                .header("X-AUTH-TOKEN", authToken));
    json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertNotNull(json.get("authToken"));
    String authToken2 = json.get("authToken").asText();

    ObjectNode registerJson3 = Json.newObject();
    registerJson3.put("code", "fb");
    registerJson3.put("email", "foo4@bar.com");
    registerJson3.put("password", "pAssw_0rd");
    registerJson3.put("name", "Foo");

    result =
        routeWithYWErrHandler(
            app,
            fakeRequest("POST", "/api/register")
                .bodyJson(registerJson3)
                .header("X-AUTH-TOKEN", authToken2));

    assertBadRequest(result, "Only Super Admins can register tenant.");
  }

  @Test
  public void testRegisterCustomerWithLongerCode()
      throws InterruptedException, ExecutionException, TimeoutException {
    startApp(true);
    ObjectNode registerJson = Json.newObject();
    registerJson.put("code", "abcabcabcabcabcabc");
    registerJson.put("email", "foo2@bar.com");
    registerJson.put("password", "pAssw_0rd");
    registerJson.put("name", "Foo");

    Result result =
        routeWithYWErrHandler(app, fakeRequest("POST", "/api/register").bodyJson(registerJson));
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(BAD_REQUEST, result.status());
    assertValue(json, "error", "{\"code\":[\"Maximum length is 15\"]}");
  }

  @Test
  public void testRegisterCustomerExceedingLimit()
      throws InterruptedException, ExecutionException, TimeoutException {
    startApp(false);
    ModelFactory.testCustomer("Test Customer 1");
    ObjectNode registerJson = Json.newObject();
    registerJson.put("code", "fb");
    registerJson.put("email", "foo2@bar.com");
    registerJson.put("password", "pAssw_0rd");
    registerJson.put("name", "Foo");
    Result result =
        routeWithYWErrHandler(app, fakeRequest("POST", "/api/register").bodyJson(registerJson));
    assertBadRequest(result, "Cannot register multiple accounts in Single tenancy.");
  }

  @Test
  public void testRegisterCustomerWithoutEmail() {
    startApp(false);
    ObjectNode registerJson = Json.newObject();
    registerJson.put("email", "test@customer.com");
    Result result =
        assertPlatformException(
            () -> route(app, fakeRequest("POST", "/api/login").bodyJson(registerJson)));

    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(BAD_REQUEST, result.status());
    assertThat(
        json.get("error").toString(),
        allOf(notNullValue(), containsString("{\"password\":[\"This field is required\"]}")));
  }

  @Test
  public void testLogout() {
    startApp(false);
    Customer customer = ModelFactory.testCustomer("Test Customer 1");
    ModelFactory.testUser(customer);
    ObjectNode loginJson = Json.newObject();
    loginJson.put("email", "test@customer.com");
    loginJson.put("password", "password");
    Result result = route(app, fakeRequest("POST", "/api/login").bodyJson(loginJson));
    JsonNode json = Json.parse(contentAsString(result));
    assertAuditEntry(1, customer.getUuid());

    assertEquals(OK, result.status());
    String authToken = json.get("authToken").asText();
    result = route(app, fakeRequest("GET", "/api/logout").header("X-AUTH-TOKEN", authToken));
    assertEquals(OK, result.status());
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testAuthTokenExpiry() {
    startApp(false);
    Customer customer = ModelFactory.testCustomer("Test Customer 1");
    ModelFactory.testUser(customer);
    ObjectNode loginJson = Json.newObject();
    loginJson.put("email", "test@customer.com");
    loginJson.put("password", "password");
    Result result = route(app, fakeRequest("POST", "/api/login").bodyJson(loginJson));
    JsonNode json = Json.parse(contentAsString(result));
    String authToken1 = json.get("authToken").asText();
    loginJson.put("email", "test@customer.com");
    loginJson.put("password", "password");
    result = route(app, fakeRequest("POST", "/api/login").bodyJson(loginJson));
    json = Json.parse(contentAsString(result));
    String authToken2 = json.get("authToken").asText();
    assertEquals(authToken1, authToken2);
    assertAuditEntry(2, customer.getUuid());
  }

  @Test
  public void testApiTokenUpsert() {
    startApp(false);
    Customer customer = ModelFactory.testCustomer("Test Customer 1");
    ModelFactory.testUser(customer);
    ObjectNode loginJson = Json.newObject();
    loginJson.put("email", "test@customer.com");
    loginJson.put("password", "password");
    Result result = route(app, fakeRequest("POST", "/api/login").bodyJson(loginJson));
    assertAuditEntry(1, customer.getUuid());

    JsonNode json = Json.parse(contentAsString(result));
    String authToken = json.get("authToken").asText();
    String custUuid = json.get("customerUUID").asText();
    ObjectNode apiTokenJson = Json.newObject();
    apiTokenJson.put("authToken", authToken);
    result =
        route(
            app,
            fakeRequest("PUT", "/api/customers/" + custUuid + "/api_token")
                .header("X-AUTH-TOKEN", authToken));
    json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertNotNull(json.get("apiToken"));
    assertEquals(1L, json.get("apiTokenVersion").asLong());
    assertAuditEntry(2, customer.getUuid());
  }

  @Test
  public void testApiTokenUpdateWithVersion() {
    startApp(false);
    Customer customer = ModelFactory.testCustomer("Test Customer 1");
    Users user = ModelFactory.testUser(customer);
    ObjectNode loginJson = Json.newObject();
    loginJson.put("email", "test@customer.com");
    loginJson.put("password", "password");
    Result result = route(app, fakeRequest("POST", "/api/login").bodyJson(loginJson));
    assertAuditEntry(1, customer.getUuid());

    user.upsertApiToken();

    JsonNode json = Json.parse(contentAsString(result));
    String authToken = json.get("authToken").asText();
    String custUuid = json.get("customerUUID").asText();
    ObjectNode apiTokenJson = Json.newObject();
    apiTokenJson.put("authToken", authToken);
    result =
        route(
            app,
            fakeRequest("PUT", "/api/customers/" + custUuid + "/api_token?apiTokenVersion=1")
                .header("X-AUTH-TOKEN", authToken));
    json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertNotNull(json.get("apiToken"));
    assertEquals(2L, json.get("apiTokenVersion").asLong());
    assertAuditEntry(2, customer.getUuid());
  }

  @Test
  public void testApiTokenUpdate() {
    startApp(false);
    Customer customer = ModelFactory.testCustomer("Test Customer 1");
    ModelFactory.testUser(customer);
    ObjectNode loginJson = Json.newObject();
    loginJson.put("email", "test@customer.com");
    loginJson.put("password", "password");
    Result result = route(app, fakeRequest("POST", "/api/login").bodyJson(loginJson));
    JsonNode json = Json.parse(contentAsString(result));
    String authToken = json.get("authToken").asText();
    String custUuid = json.get("customerUUID").asText();
    ObjectNode apiTokenJson = Json.newObject();
    apiTokenJson.put("authToken", authToken);
    result =
        route(
            app,
            fakeRequest("PUT", "/api/customers/" + custUuid + "/api_token")
                .header("X-AUTH-TOKEN", authToken));
    json = Json.parse(contentAsString(result));
    String apiToken1 = json.get("apiToken").asText();
    Long apiTokenVersion1 = json.get("apiTokenVersion").asLong();
    apiTokenJson.put("authToken", authToken);
    result =
        route(
            app,
            fakeRequest("PUT", "/api/customers/" + custUuid + "/api_token")
                .header("X-AUTH-TOKEN", authToken));
    json = Json.parse(contentAsString(result));
    String apiToken2 = json.get("apiToken").asText();
    Long apiTokenVersion2 = json.get("apiTokenVersion").asLong();
    assertNotEquals(apiToken1, apiToken2);
    assertEquals(Long.valueOf(apiTokenVersion1 + 1), apiTokenVersion2);
    assertAuditEntry(3, customer.getUuid());
  }

  @Test
  public void testApiTokenUpdateWrongVersion() {
    startApp(false);
    Customer customer = ModelFactory.testCustomer("Test Customer 1");
    Users user = ModelFactory.testUser(customer);
    ObjectNode loginJson = Json.newObject();
    loginJson.put("email", "test@customer.com");
    loginJson.put("password", "password");
    Result result = route(app, fakeRequest("POST", "/api/login").bodyJson(loginJson));
    assertAuditEntry(1, customer.getUuid());

    user.upsertApiToken();

    JsonNode json = Json.parse(contentAsString(result));
    String authToken = json.get("authToken").asText();
    String custUuid = json.get("customerUUID").asText();
    ObjectNode apiTokenJson = Json.newObject();
    apiTokenJson.put("authToken", authToken);
    result =
        assertPlatformException(
            () ->
                route(
                    app,
                    fakeRequest(
                            "PUT", "/api/customers/" + custUuid + "/api_token?apiTokenVersion=2")
                        .header("X-AUTH-TOKEN", authToken)));
    json = Json.parse(contentAsString(result));

    assertEquals(BAD_REQUEST, result.status());
    assertThat(
        json.get("error").toString(),
        allOf(notNullValue(), containsString("API token version has changed")));
  }

  @Test
  public void testCustomerCount() {
    startApp(false);
    Result result = route(app, fakeRequest("GET", "/api/customer_count"));
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertValue(json, "count", "0");
    ModelFactory.testCustomer("Test Customer 1");
    checkCount("count", "1");
  }

  @Test
  public void testAppVersion() {
    startApp(false);
    Result result = route(app, fakeRequest("GET", "/api/app_version"));
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertEquals(json, Json.newObject());
    ConfigHelper configHelper = new ConfigHelper();
    configHelper.loadConfigToDB(
        ConfigHelper.ConfigType.SoftwareVersion, ImmutableMap.of("version", "0.0.1"));
    result = route(app, fakeRequest("GET", "/api/app_version"));
    json = Json.parse(contentAsString(result));
    assertOk(result);
    assertValue(json, "version", "0.0.1");
  }

  @Test
  public void testProxyRequestInvalidFormat()
      throws InterruptedException, ExecutionException, TimeoutException {
    startApp(false);
    Customer customer = ModelFactory.testCustomer("Test Customer 1");
    Users user = ModelFactory.testUser(customer);
    String authToken = user.createAuthToken();
    Universe universe = ModelFactory.createUniverse(customer.getId());
    Http.RequestBuilder request =
        fakeRequest("GET", "/universes/" + universe.getUniverseUUID() + "/proxy/www.test.com")
            .header("X-AUTH-TOKEN", authToken);
    Result result = routeWithYWErrHandler(app, request);
    assertBadRequest(result, "Invalid proxy request");
  }

  @Test
  public void testProxyRequestInvalidIP()
      throws InterruptedException, ExecutionException, TimeoutException {
    startApp(false);
    Customer customer = ModelFactory.testCustomer("Test Customer 1");
    Users user = ModelFactory.testUser(customer);
    String authToken = user.createAuthToken();
    Universe universe = ModelFactory.createUniverse(customer.getId());
    Http.RequestBuilder request =
        fakeRequest(
                "GET", "/universes/" + universe.getUniverseUUID() + "/proxy/" + "127.0.0.1:7000")
            .header("X-AUTH-TOKEN", authToken);
    Result result = routeWithYWErrHandler(app, request);
    assertBadRequest(result, "Invalid proxy request");
  }

  @Test
  public void testProxyRequestInvalidPort()
      throws InterruptedException, ExecutionException, TimeoutException {
    startApp(false);
    Customer customer = ModelFactory.testCustomer("Test Customer 1");
    Users user = ModelFactory.testUser(customer);
    String authToken = user.createAuthToken();
    Provider provider = ModelFactory.awsProvider(customer);

    Region r = Region.create(provider, "region-1", "PlacementRegion-1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ-1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ-2", "subnet-2");
    AvailabilityZone.createOrThrow(r, "az-3", "PlacementAZ-3", "subnet-3");
    InstanceType i =
        InstanceType.upsert(
            provider.getUuid(), "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());
    UniverseDefinitionTaskParams.UserIntent userIntent = getTestUserIntent(r, provider, i, 3);
    Universe universe = ModelFactory.createUniverse(customer.getId());
    Universe.saveDetails(
        universe.getUniverseUUID(), ApiUtils.mockUniverseUpdater(userIntent, "test-prefix"));
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    NodeDetails node = universe.getUniverseDetails().nodeDetailsSet.stream().findFirst().get();
    System.out.println("PRIVATE IP: " + node.cloudInfo.private_ip);
    Http.RequestBuilder request =
        fakeRequest(
                "GET",
                "/universes/"
                    + universe.getUniverseUUID()
                    + "/proxy/"
                    + node.cloudInfo.private_ip
                    + ":7001/")
            .header("X-AUTH-TOKEN", authToken);
    Result result = routeWithYWErrHandler(app, request);
    assertBadRequest(result, "Invalid proxy request");
  }

  @Test
  public void testProxyRequestValid()
      throws InterruptedException, ExecutionException, TimeoutException {
    startApp(false);
    Customer customer = ModelFactory.testCustomer("Test Customer 1");
    Users user = ModelFactory.testUser(customer);
    String authToken = user.createAuthToken();
    Provider provider = ModelFactory.awsProvider(customer);

    Region r = Region.create(provider, "region-1", "PlacementRegion-1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ-1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ-2", "subnet-2");
    AvailabilityZone.createOrThrow(r, "az-3", "PlacementAZ-3", "subnet-3");
    InstanceType i =
        InstanceType.upsert(
            provider.getUuid(), "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());
    UniverseDefinitionTaskParams.UserIntent userIntent = getTestUserIntent(r, provider, i, 3);
    Universe universe = ModelFactory.createUniverse(customer.getId());
    Universe.saveDetails(
        universe.getUniverseUUID(), ApiUtils.mockUniverseUpdater(userIntent, "test-prefix"));
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    NodeDetails node = details.nodeDetailsSet.stream().findFirst().get();

    // Set to an invalid IP
    node.cloudInfo.private_ip = "host-n1";
    universe.setUniverseDetails(details);
    universe.update();
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());

    String nodeAddr = node.cloudInfo.private_ip + ":" + node.masterHttpPort;
    Http.RequestBuilder request =
        fakeRequest("GET", "/universes/" + universe.getUniverseUUID() + "/proxy/" + nodeAddr + "/")
            .header("X-AUTH-TOKEN", authToken);
    Result result = routeWithYWErrHandler(app, request);
    // Expect the request to fail since the hostname isn't real.
    // This shows that it got past validation though
    assertInternalServerError(result, null /*errorStr*/);
  }

  @Test
  public void testProxyRequestUnAuthenticated() {
    startApp(false);
    Customer customer = ModelFactory.testCustomer("Test Customer 1");
    Provider provider = ModelFactory.awsProvider(customer);

    Region r = Region.create(provider, "region-1", "PlacementRegion-1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ-1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ-2", "subnet-2");
    AvailabilityZone.createOrThrow(r, "az-3", "PlacementAZ-3", "subnet-3");
    InstanceType i =
        InstanceType.upsert(
            provider.getUuid(), "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());
    UniverseDefinitionTaskParams.UserIntent userIntent = getTestUserIntent(r, provider, i, 3);
    Universe universe = ModelFactory.createUniverse(customer.getId());
    Universe.saveDetails(
        universe.getUniverseUUID(), ApiUtils.mockUniverseUpdater(userIntent, "test-prefix"));
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    NodeDetails node = universe.getUniverseDetails().nodeDetailsSet.stream().findFirst().get();
    String nodeAddr = node.cloudInfo.private_ip + ":" + node.masterHttpPort;
    Result result =
        route(
            app,
            fakeRequest(
                "GET", "/universes/" + universe.getUniverseUUID() + "/proxy/" + nodeAddr + "/"));
    // Expect the request to fail since the hostname isn't real.
    // This shows that it got past validation though
    assertUnauthorizedNoException(result, "Unable To Authenticate User");
  }

  @Test
  public void testRegisterCustomerCreatesPACollector() throws IOException {
    try (MockWebServer paServer = new MockWebServer()) {
      paServer.start();
      String paUrl = paServer.url("/").toString().replaceAll("/$", "");

      startApp(false, ImmutableMap.of("yb.pa.url", paUrl, "yb.pa.api_token", "test-pa-token"));

      // putCustomerMetadata will be called during registration, return valid response.
      paServer.enqueue(
          new MockResponse()
              .setBody("{\"id\":\"00000000-0000-0000-0000-000000000000\"}")
              .addHeader("Content-Type", "application/json"));

      ObjectNode registerJson = Json.newObject();
      registerJson.put("code", "fb");
      registerJson.put("email", "foo2@bar.com");
      registerJson.put("password", "pAssw_0rd");
      registerJson.put("name", "Foo");

      Result result = route(app, fakeRequest("POST", "/api/register").bodyJson(registerJson));
      JsonNode json = Json.parse(contentAsString(result));

      assertEquals(OK, result.status());
      UUID customerUuid = UUID.fromString(json.get("customerUUID").asText());

      PerfAdvisorService perfAdvisorService = app.injector().instanceOf(PerfAdvisorService.class);
      List<PACollector> collectors =
          perfAdvisorService.list(PACollectorFilter.builder().customerUuid(customerUuid).build());
      assertFalse(
          "PA collector should be registered during customer registration", collectors.isEmpty());

      PACollector collector = collectors.get(0);
      assertEquals(customerUuid, collector.getCustomerUUID());
      assertEquals(paUrl, collector.getPaUrl());
    }
  }
}
