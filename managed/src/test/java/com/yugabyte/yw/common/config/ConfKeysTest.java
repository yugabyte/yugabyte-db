package com.yugabyte.yw.common.config;

import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.models.ScopedRuntimeConfig.GLOBAL_SCOPE_UUID;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.fakeRequest;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.rbac.Permission;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.rbac.ResourceGroup;
import com.yugabyte.yw.models.rbac.ResourceGroup.ResourceDefinition;
import com.yugabyte.yw.models.rbac.Role;
import com.yugabyte.yw.models.rbac.Role.RoleType;
import com.yugabyte.yw.models.rbac.RoleBinding;
import com.yugabyte.yw.models.rbac.RoleBinding.RoleBindingType;
import db.migration.default_.common.R__Sync_System_Roles;
import java.lang.reflect.Field;
import java.lang.reflect.Modifier;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

public class ConfKeysTest extends FakeDBApplication {
  private static final String LIST_KEYS = "/api/runtime_config/mutable_keys";
  private static final String LIST_KEY_INFO = "/api/runtime_config/mutable_key_info";
  private static final String KEY = "/api/customers/%s/runtime_config/%s/key/%s";

  private Customer defaultCustomer;
  private Universe defaultUniverse;
  private Provider defaultProvider;
  private String authToken;

  Permission permission1 = new Permission(ResourceType.OTHER, Action.CREATE);
  Permission permission2 = new Permission(ResourceType.OTHER, Action.READ);
  Permission permission3 = new Permission(ResourceType.OTHER, Action.UPDATE);
  Permission permission4 = new Permission(ResourceType.OTHER, Action.DELETE);

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUniverse = ModelFactory.createUniverse(defaultCustomer.getId());
    defaultProvider = ModelFactory.kubernetesProvider(defaultCustomer);
    Users user = ModelFactory.testUser(defaultCustomer, Users.Role.SuperAdmin);
    authToken = user.createAuthToken();
    Role role1 =
        Role.create(
            defaultCustomer.getUuid(),
            "FakeRole2",
            "testDescription",
            RoleType.Custom,
            new HashSet<>(Arrays.asList(permission1, permission2, permission3, permission4)));
    ResourceDefinition rd3 =
        ResourceDefinition.builder()
            .resourceType(ResourceType.OTHER)
            .resourceUUIDSet(new HashSet<>(Arrays.asList(defaultCustomer.getUuid())))
            .build();
    ResourceGroup rG = new ResourceGroup(new HashSet<>(Arrays.asList(rd3)));
    RoleBinding.create(user, RoleBindingType.Custom, role1, rG);

    // Run the system roles sync migration to validate the UseNewRbacAuthzListener.
    // Required for the "yb.rbac.use_new_authz" runtime config.
    R__Sync_System_Roles.syncSystemRoles();
  }

  private Result setKey(String path, String newVal, UUID scopeUUID) {
    Http.RequestBuilder request =
        fakeRequest("PUT", String.format(KEY, defaultCustomer.getUuid(), scopeUUID, path))
            .header("X-AUTH-TOKEN", authToken)
            .bodyText(newVal);
    return route(request);
  }

  private String getConfVal(ConfKeyInfo<?> keyInfo) {
    final RuntimeConfigFactory configFactory =
        app.injector().instanceOf(RuntimeConfigFactory.class);
    switch (keyInfo.getScope()) {
      case GLOBAL:
        return configFactory.globalRuntimeConf().getString(keyInfo.getKey());
      case CUSTOMER:
        return configFactory.forCustomer(defaultCustomer).getString(keyInfo.getKey());
      case PROVIDER:
        return configFactory.forProvider(defaultProvider).getString(keyInfo.getKey());
      case UNIVERSE:
        return configFactory.forUniverse(defaultUniverse).getString(keyInfo.getKey());
      default:
        return "";
    }
  }

  @Test
  public void testRuntimeConfKeysAuto() {

    Result result = doRequestWithAuthToken("GET", LIST_KEYS, authToken);
    assertEquals(OK, result.status());
    Set<String> listKeys =
        ImmutableSet.copyOf(Json.parse(contentAsString(result)).elements()).stream()
            .map(JsonNode::asText)
            .collect(Collectors.toSet());

    result = doRequestWithAuthToken("GET", LIST_KEY_INFO, authToken);
    assertEquals(OK, result.status());
    Set<String> metaKeys =
        ImmutableSet.copyOf(Json.parse(contentAsString(result))).stream()
            .map(JsonNode -> JsonNode.get("key"))
            .map(JsonNode::asText)
            .collect(Collectors.toSet());

    Map<Class<? extends RuntimeConfigKeysModule>, UUID> scopes = new HashMap<>();
    scopes.put(GlobalConfKeys.class, GLOBAL_SCOPE_UUID);
    scopes.put(CustomerConfKeys.class, defaultCustomer.getUuid());
    scopes.put(UniverseConfKeys.class, defaultUniverse.getUniverseUUID());
    scopes.put(ProviderConfKeys.class, defaultProvider.getUuid());

    Map<ConfDataType<?>, String> validVals = new HashMap<>();

    validVals.put(ConfDataType.DurationType, "10 days");
    validVals.put(ConfDataType.DoubleType, "12.34");
    validVals.put(ConfDataType.LongType, "10834283");
    validVals.put(ConfDataType.BooleanType, "true");
    validVals.put(ConfDataType.PeriodType, "10 weeks");
    validVals.put(ConfDataType.IntegerType, "10");
    validVals.put(ConfDataType.BytesType, "10 TB");
    validVals.put(ConfDataType.VersionCheckModeEnum, "NEVER");
    validVals.put(ConfDataType.SkipCertValdationEnum, "ALL");
    validVals.put(ConfDataType.ProtocolEnum, "TCP");
    validVals.put(
        ConfDataType.KeyValuesSetMultimapType,
        "[\"yb_task:task1\",\"yb_task:task2\",\"yb_dev:*\"]");
    validVals.put(ConfDataType.LdapSearchScopeEnum, "SUBTREE");
    validVals.put(ConfDataType.LdapDefaultRoleEnum, "ReadOnly");
    validVals.put(ConfDataType.LdapTlsProtocol, "TLSv1_2");

    // No data validation for these types yet
    Set<ConfDataType<?>> exceptions =
        ImmutableSet.of(
            ConfDataType.StringListType,
            ConfDataType.StringType,
            ConfDataType.TagListType,
            ConfDataType.IntegerListType);

    Set<ConfDataType<?>> includedObjectsType =
        ImmutableSet.of(ConfDataType.KeyValuesSetMultimapType);

    for (Class<?> c : scopes.keySet()) {
      for (Field field : c.getDeclaredFields()) {
        if (Modifier.isStatic(field.getModifiers()) && field.getType().equals(ConfKeyInfo.class)) {
          try {
            ConfKeyInfo<?> keyInfo = (ConfKeyInfo<?>) field.get(null);

            if (exceptions.contains(keyInfo.getDataType())) continue;

            if (!validVals.keySet().contains(keyInfo.getDataType())) {
              String failMsg =
                  String.format(
                      "Please add valid values for the DataType %s you defined in this test",
                      keyInfo.getDataType().getName());
              fail(failMsg);
            }

            assertTrue(keyInfo.getKey(), listKeys.contains(keyInfo.getKey()));
            assertTrue(keyInfo.getKey(), metaKeys.contains(keyInfo.getKey()));

            Result r =
                assertPlatformException(
                    () -> {
                      setKey(keyInfo.getKey(), "Invalid Val", scopes.get(c));
                    });
            assertEquals(BAD_REQUEST, r.status());

            r = setKey(keyInfo.getKey(), validVals.get(keyInfo.getDataType()), scopes.get(c));
            assertEquals(OK, r.status());
            // Skip this validation for objects until we start validating objects.
            if (!includedObjectsType.contains(keyInfo.getDataType())) {
              assertEquals(validVals.get(keyInfo.getDataType()), getConfVal(keyInfo));
            }
          } catch (IllegalAccessException e) {
            fail(e.getMessage());
          }
        }
      }
    }
  }
}
