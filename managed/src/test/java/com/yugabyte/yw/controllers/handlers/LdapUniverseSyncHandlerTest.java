package com.yugabyte.yw.controllers.handlers;

import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.gflags.SpecificGFlags.PerProcessFlags;
import com.yugabyte.yw.forms.LdapUnivSyncFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;
import play.mvc.Result;

@RunWith(MockitoJUnitRunner.class)
public class LdapUniverseSyncHandlerTest extends FakeDBApplication {

  private Customer defaultCustomer;
  private Universe defaultUniverse;
  private LdapUniverseSyncHandler handler;
  private LdapUnivSyncFormData ldapUnivSyncFormData;
  Map<String, String> tserverMap = new HashMap<>();
  List<String> emptyList = new ArrayList<>();

  @Before
  public void setup() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUniverse = ModelFactory.createUniverse(defaultCustomer.getId());
    defaultUniverse = ModelFactory.addNodesToUniverse(defaultUniverse.getUniverseUUID(), 1);
    handler = new LdapUniverseSyncHandler();
    ldapUnivSyncFormData = new LdapUnivSyncFormData();

    // setting up TServerGflags
    Map<ServerType, Map<String, String>> value = new HashMap<>();
    value.put(ServerType.TSERVER, tserverMap);

    PerProcessFlags perProcessFlags = new PerProcessFlags();
    perProcessFlags.value = value;

    UniverseDefinitionTaskParams universeDetails = defaultUniverse.getUniverseDetails();
    Cluster primaryCluster = universeDetails.getPrimaryCluster();

    primaryCluster.userIntent.specificGFlags = new SpecificGFlags();
    primaryCluster.userIntent.specificGFlags.setPerProcessFlags(perProcessFlags);
  }

  /* ==== API Tests ==== */
  @Test
  public void testInvalidYsqlLdapServer() {
    // Test for ldapServer
    String gFlag = "test";

    ldapUnivSyncFormData.setTargetApi(LdapUnivSyncFormData.TargetApi.ysql);

    Result result =
        assertPlatformException(() -> handler.populateFromGflagYSQL(ldapUnivSyncFormData, gFlag));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(
        resultJson,
        "error",
        String.format("Couldnt extract the value from the gflag. ldapServer(null) is required."));
  }

  @Test
  public void testInvalidYsqlLdapPort() {
    // Test for ldapPort
    String gFlag = "test";

    ldapUnivSyncFormData.setTargetApi(LdapUnivSyncFormData.TargetApi.ysql);
    ldapUnivSyncFormData.setLdapServer("test");
    ldapUnivSyncFormData.setLdapBindDn("test");
    ldapUnivSyncFormData.setLdapBindPassword("test");
    ldapUnivSyncFormData.setLdapSearchFilter("test");
    ldapUnivSyncFormData.setLdapBasedn("test");

    ldapUnivSyncFormData = handler.populateFromGflagYSQL(ldapUnivSyncFormData, gFlag);
    assertTrue(ldapUnivSyncFormData.getLdapPort() == 389);

    ldapUnivSyncFormData.setLdapPort(null);
    ldapUnivSyncFormData.setUseLdapTls(true);
    ldapUnivSyncFormData = handler.populateFromGflagYSQL(ldapUnivSyncFormData, gFlag);

    assertTrue(ldapUnivSyncFormData.getLdapPort() == 636);
  }

  @Test
  public void testInvalidYsqlLdapBindDn() {
    // Test for ldapBindDn
    String gFlag = "test";

    ldapUnivSyncFormData.setTargetApi(LdapUnivSyncFormData.TargetApi.ysql);
    ldapUnivSyncFormData.setLdapServer("test");

    Result result =
        assertPlatformException(() -> handler.populateFromGflagYSQL(ldapUnivSyncFormData, gFlag));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(
        resultJson,
        "error",
        String.format("Couldnt extract the value from the gflag. ldapBindDn(null) is required."));
  }

  @Test
  public void testInvalidYsqlLdapBindPassword() {
    // Test for ldapBindPassword
    String gFlag = "test";

    ldapUnivSyncFormData.setTargetApi(LdapUnivSyncFormData.TargetApi.ysql);
    ldapUnivSyncFormData.setLdapServer("test");
    ldapUnivSyncFormData.setLdapBindDn("test");

    Result result =
        assertPlatformException(() -> handler.populateFromGflagYSQL(ldapUnivSyncFormData, gFlag));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(
        resultJson,
        "error",
        String.format(
            "Couldnt extract the value from the gflag. ldapBindPassword(null) is required."));
  }

  @Test
  public void testInvalidYsqlLdapSearchFilter() {
    // Test for ldapSearchFilter
    String gFlag = "test";

    ldapUnivSyncFormData.setTargetApi(LdapUnivSyncFormData.TargetApi.ysql);
    ldapUnivSyncFormData.setLdapServer("test");
    ldapUnivSyncFormData.setLdapBindDn("test");
    ldapUnivSyncFormData.setLdapBindPassword("test");

    Result result =
        assertPlatformException(() -> handler.populateFromGflagYSQL(ldapUnivSyncFormData, gFlag));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(
        resultJson,
        "error",
        String.format(
            "Couldnt extract the value from the gflag. ldapSearchFilter(null) is required."));
  }

  @Test
  public void testInvalidYsqlLdapBaseDn() {
    // Test for ldapBaseDn
    String gFlag = "test";

    ldapUnivSyncFormData.setTargetApi(LdapUnivSyncFormData.TargetApi.ysql);
    ldapUnivSyncFormData.setLdapServer("test");
    ldapUnivSyncFormData.setLdapBindDn("test");
    ldapUnivSyncFormData.setLdapBindPassword("test");
    ldapUnivSyncFormData.setLdapSearchFilter("test");

    Result result =
        assertPlatformException(() -> handler.populateFromGflagYSQL(ldapUnivSyncFormData, gFlag));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(
        resultJson,
        "error",
        String.format("Couldnt extract the value from the gflag. ldapBasedn(null) is required."));
  }

  @Test
  public void testInvalidYsqlUseLdapTls() {
    // Test for ldaptls
    String gFlag = "test";
    ldapUnivSyncFormData.setTargetApi(LdapUnivSyncFormData.TargetApi.ysql);
    ldapUnivSyncFormData.setLdapServer("test");
    ldapUnivSyncFormData.setLdapBindDn("test");
    ldapUnivSyncFormData.setLdapBindPassword("test");
    ldapUnivSyncFormData.setLdapSearchFilter("test");
    ldapUnivSyncFormData.setLdapBasedn("test");

    ldapUnivSyncFormData = handler.populateFromGflagYSQL(ldapUnivSyncFormData, gFlag);
    assertFalse(ldapUnivSyncFormData.getUseLdapTls());
  }

  @Test
  public void testValidYsqlGflag() {
    String gFlag =
        "host all yugabyte 127.0.0.1/0 password,\"host all all all ldap ldapserver=0.0.0.0"
            + " ldapsearchfilter=\"\"(objectclass=test)\"\""
            + " ldapbinddn=\"\"cn=test,dc=test,dc=org\"\" ldapbasedn=\"\"dc=test,dc=org\"\""
            + " ldapbindpasswd=test\" ldaptls=0 ldapport=636";

    ldapUnivSyncFormData.setTargetApi(LdapUnivSyncFormData.TargetApi.ysql);

    ldapUnivSyncFormData = handler.populateFromGflagYSQL(ldapUnivSyncFormData, gFlag);

    assertEquals("0.0.0.0", ldapUnivSyncFormData.getLdapServer());
    assertTrue(ldapUnivSyncFormData.getLdapPort() == 636);
    assertEquals("cn=test,dc=test,dc=org", ldapUnivSyncFormData.getLdapBindDn());
    assertEquals("test", ldapUnivSyncFormData.getLdapBindPassword());
    assertEquals("(objectclass=test)", ldapUnivSyncFormData.getLdapSearchFilter());
    assertEquals("dc=test,dc=org", ldapUnivSyncFormData.getLdapBasedn());
    assertEquals(false, ldapUnivSyncFormData.getUseLdapTls());

    gFlag =
        "host all yugabyte 127.0.0.1/0 password,\"host all all all ldap ldapserver=0.0.0.0"
            + " ldapsearchfilter=\"\"(objectclass=test)\"\" ldapbinddn=\"\"cn=test,dc=test"
            + ",dc=org\"\" ldapbasedn=\"\"dc=test,dc=org\"\" ldapbindpasswd=test\" ldaptls=1";

    ldapUnivSyncFormData.setUseLdapTls(null);
    ldapUnivSyncFormData = handler.populateFromGflagYSQL(ldapUnivSyncFormData, gFlag);

    assertTrue(ldapUnivSyncFormData.getUseLdapTls());
  }

  @Test
  public void testNoYsqlGflag() {
    ldapUnivSyncFormData.setTargetApi(LdapUnivSyncFormData.TargetApi.ysql);
    Result result =
        assertPlatformException(
            () ->
                handler.syncUniv(
                    defaultCustomer.getUuid(),
                    defaultCustomer,
                    defaultUniverse.getUniverseUUID(),
                    defaultUniverse,
                    ldapUnivSyncFormData));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(
        resultJson,
        "error",
        String.format(
            "LDAP authentication for YSQL is not enabled on the universe: %s",
            defaultUniverse.getName()));
  }

  @Test
  public void testInvalidYcqlLdapServer() {
    // Test for ldapServer
    ldapUnivSyncFormData.setTargetApi(LdapUnivSyncFormData.TargetApi.ycql);

    Result result =
        assertPlatformException(
            () -> handler.populateFromGflagYCQL(ldapUnivSyncFormData, tserverMap));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(
        resultJson,
        "error",
        String.format("Couldnt extract the value from the gflag. ldapServer(null) is required."));
  }

  @Test
  public void testInvalidYcqlLdapBindDn() {
    // Test for ldapBindDn
    ldapUnivSyncFormData.setTargetApi(LdapUnivSyncFormData.TargetApi.ycql);
    ldapUnivSyncFormData.setLdapServer("test");
    ldapUnivSyncFormData.setLdapPort(123);

    Result result =
        assertPlatformException(
            () -> handler.populateFromGflagYCQL(ldapUnivSyncFormData, tserverMap));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(
        resultJson,
        "error",
        String.format("Couldnt extract the value from the gflag. ldapBindDn(null) is required."));
  }

  @Test
  public void testInvalidYcqlLdapBindPassword() {
    // Test for ldapBindPassword
    ldapUnivSyncFormData.setTargetApi(LdapUnivSyncFormData.TargetApi.ycql);
    ldapUnivSyncFormData.setLdapServer("test");
    ldapUnivSyncFormData.setLdapPort(123);
    ldapUnivSyncFormData.setLdapBindDn("test");

    Result result =
        assertPlatformException(
            () -> handler.populateFromGflagYCQL(ldapUnivSyncFormData, tserverMap));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(
        resultJson,
        "error",
        String.format(
            "Couldnt extract the value from the gflag. ldapBindPassword(null) is required."));
  }

  @Test
  public void testInvalidYcqlLdapSearchFilter() {
    // Test for ldapSearchFilter
    ldapUnivSyncFormData.setTargetApi(LdapUnivSyncFormData.TargetApi.ycql);
    ldapUnivSyncFormData.setLdapServer("test");
    ldapUnivSyncFormData.setLdapPort(123);
    ldapUnivSyncFormData.setLdapBindDn("test");
    ldapUnivSyncFormData.setLdapBindPassword("test");

    Result result =
        assertPlatformException(
            () -> handler.populateFromGflagYCQL(ldapUnivSyncFormData, tserverMap));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(
        resultJson,
        "error",
        String.format(
            "Couldnt extract the value from the gflag. ldapSearchFilter(null) is required."));
  }

  @Test
  public void testInvalidYcqlLdapBaseDn() {
    // Test for ldapBaseDn
    ldapUnivSyncFormData.setTargetApi(LdapUnivSyncFormData.TargetApi.ycql);
    ldapUnivSyncFormData.setLdapServer("test");
    ldapUnivSyncFormData.setLdapPort(123);
    ldapUnivSyncFormData.setLdapBindDn("test");
    ldapUnivSyncFormData.setLdapBindPassword("test");
    ldapUnivSyncFormData.setLdapSearchFilter("test");

    Result result =
        assertPlatformException(
            () -> handler.populateFromGflagYCQL(ldapUnivSyncFormData, tserverMap));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(
        resultJson,
        "error",
        String.format("Couldnt extract the value from the gflag. ldapBasedn(null) is required."));
  }

  @Test
  public void testInvalidYcqlUseLdapTls() {
    // Test for ldaptls
    ldapUnivSyncFormData.setTargetApi(LdapUnivSyncFormData.TargetApi.ycql);
    ldapUnivSyncFormData.setLdapServer("test");
    ldapUnivSyncFormData.setLdapPort(123);
    ldapUnivSyncFormData.setLdapBindDn("test");
    ldapUnivSyncFormData.setLdapBindPassword("test");
    ldapUnivSyncFormData.setLdapSearchFilter("test");
    ldapUnivSyncFormData.setLdapBasedn("test");

    ldapUnivSyncFormData = handler.populateFromGflagYCQL(ldapUnivSyncFormData, tserverMap);
    assertFalse(ldapUnivSyncFormData.getUseLdapTls());
  }

  @Test
  public void testValidYcqlGflag() {
    tserverMap.put(GFlagsUtil.USE_CASSANDRA_AUTHENTICATION, "true");
    ldapUnivSyncFormData.setTargetApi(LdapUnivSyncFormData.TargetApi.ycql);

    tserverMap.put(LdapUniverseSyncHandler.YCQL_LDAP_SERVER, "ldap://0.0.0.0:000");
    tserverMap.put(LdapUniverseSyncHandler.YCQL_LDAP_BIND_DN, "cn=test,dc=test,dc=org");
    tserverMap.put(LdapUniverseSyncHandler.YCQL_LDAP_BIND_PASSWD, "test");
    tserverMap.put(LdapUniverseSyncHandler.YCQL_LDAP_SEARCH_FILTER, "(objectclass=test)");
    tserverMap.put(LdapUniverseSyncHandler.YCQL_LDAP_BASE_DN, "dc=test,dc=org");
    tserverMap.put(LdapUniverseSyncHandler.YCQL_LDAP_TLS, "false");

    ldapUnivSyncFormData = handler.populateFromGflagYCQL(ldapUnivSyncFormData, tserverMap);

    assertEquals("0.0.0.0", ldapUnivSyncFormData.getLdapServer());
    assertTrue(ldapUnivSyncFormData.getLdapPort() == 0);
    assertEquals("cn=test,dc=test,dc=org", ldapUnivSyncFormData.getLdapBindDn());
    assertEquals("test", ldapUnivSyncFormData.getLdapBindPassword());
    assertEquals("(objectclass=test)", ldapUnivSyncFormData.getLdapSearchFilter());
    assertEquals("dc=test,dc=org", ldapUnivSyncFormData.getLdapBasedn());
    assertEquals(false, ldapUnivSyncFormData.getUseLdapTls());

    ldapUnivSyncFormData.setUseLdapTls(null);
    tserverMap.put(LdapUniverseSyncHandler.YCQL_LDAP_TLS, "true");

    ldapUnivSyncFormData = handler.populateFromGflagYCQL(ldapUnivSyncFormData, tserverMap);

    Assert.assertEquals(true, ldapUnivSyncFormData.getUseLdapTls());
  }

  @Test
  public void testNoYcqlGflag() {
    ldapUnivSyncFormData.setTargetApi(LdapUnivSyncFormData.TargetApi.ycql);
    Result result =
        assertPlatformException(
            () ->
                handler.syncUniv(
                    defaultCustomer.getUuid(),
                    defaultCustomer,
                    defaultUniverse.getUniverseUUID(),
                    defaultUniverse,
                    ldapUnivSyncFormData));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(
        resultJson,
        "error",
        String.format(
            "LDAP authentication for YCQL is not enabled on the universe: %s",
            defaultUniverse.getName()));
  }
}
