// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.api.v2;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.is;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.test.Helpers.contentAsString;

import api.v2.handlers.UniverseManagementHandler;
import ch.qos.logback.classic.Logger;
import ch.qos.logback.classic.spi.ILoggingEvent;
import ch.qos.logback.core.read.ListAppender;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yba.v2.client.ApiException;
import com.yugabyte.yba.v2.client.api.UniverseApi;
import com.yugabyte.yba.v2.client.models.UniverseCreateSpec;
import com.yugabyte.yba.v2.client.models.UniverseDeleteSpec;
import com.yugabyte.yba.v2.client.models.YBATask;
import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.DestroyUniverse;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import io.ebean.DB;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import java.util.stream.Collectors;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.slf4j.LoggerFactory;
import play.libs.Json;
import play.mvc.Result;

/** Tests for Create/Get/Delete of Universe using v2.UniverseApiControllerImp */
@RunWith(JUnitParamsRunner.class)
public class UniverseApiControllerTest extends UniverseTestBase {

  @Test
  public void testGetUniverseV2() throws ApiException, IOException {
    UUID uUUID = createUniverse(customer.getId()).getUniverseUUID();
    Universe dbUniverse =
        Universe.saveDetails(
            uUUID,
            universe -> {
              // arch
              universe.getUniverseDetails().arch = PublicCloudConstants.Architecture.aarch64;
              // systemd
              universe.getUniverseDetails().getPrimaryCluster().userIntent.useSystemd = true;
              // ysql
              universe.getUniverseDetails().getPrimaryCluster().userIntent.enableYSQL = true;
              universe.getUniverseDetails().getPrimaryCluster().userIntent.enableYSQLAuth = true;
              universe.getUniverseDetails().getPrimaryCluster().userIntent.ysqlPassword =
                  "password#1";
              // regionList
              List<Region> regions = Region.getByProvider(providerUuid);
              universe.getUniverseDetails().getPrimaryCluster().userIntent.regionList =
                  regions != null
                      ? regions.stream().map(r -> r.getUuid()).toList()
                      : new ArrayList<>();
              // Volumes
              universe.getUniverseDetails().getPrimaryCluster().userIntent.deviceInfo =
                  ApiUtils.getDummyDeviceInfo(2, 150);
              // instanceTags
              universe.getUniverseDetails().getPrimaryCluster().userIntent.instanceTags =
                  Map.of("tag1", "value1", "tag2", "value2");
              // instanceType
              universe.getUniverseDetails().getPrimaryCluster().userIntent.instanceType =
                  ApiUtils.UTIL_INST_TYPE;
              // GFlags
              universe.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags =
                  SpecificGFlags.construct(
                      Map.of("mflag1", "mval1", "mflag2", "mval2"),
                      Map.of("tflag1", "tval1", "tflag2", "tval2"));
              SpecificGFlags.PerProcessFlags azFlags = new SpecificGFlags.PerProcessFlags();
              azFlags.value.put(ServerType.MASTER, Map.of("mperaz1", "val1", "mperaz2", "val2"));
              azFlags.value.put(ServerType.TSERVER, Map.of("tperaz1", "v1", "tperaz2", "v2"));
              universe
                  .getUniverseDetails()
                  .getPrimaryCluster()
                  .userIntent
                  .specificGFlags
                  .setPerAZ(
                      Map.of(universe.getUniverseDetails().getPrimaryCluster().uuid, azFlags));
            });
    when(mockGFlagsValidation.getGFlagDetails(anyString(), anyString(), anyString()))
        .thenReturn(Optional.empty());
    UniverseApi api = new UniverseApi();
    com.yugabyte.yba.v2.client.models.Universe universeResp =
        api.getUniverse(customer.getUuid(), uUUID);
    validateUniverseSpec(universeResp.getSpec(), dbUniverse);
    validateUniverseInfo(universeResp.getInfo(), dbUniverse);
  }

  private static final String YCQL_LDAP_SECRET = "ycql-ldap-lr23812";
  private static final String HBA_WITH_SECRET =
      "host all all 0.0.0.0/0 ldap ldapserver=ldap.example.com ldapbindpasswd=hba-lr23812";
  private static final String HBA_REDACTED =
      "host all all 0.0.0.0/0 ldap ldapserver=ldap.example.com ldapbindpasswd=REDACTED";

  private void stubGFlagsValidation() throws IOException {
    when(mockGFlagsValidation.getGFlagDetails(anyString(), anyString(), anyString()))
        .thenReturn(Optional.empty());
  }

  /**
   * The primary holds the LDAP gflags per process and per AZ. The stored row holds the admin
   * passwords, as rows saved before storage redaction still do.
   */
  private Universe createUniverseWithSecrets() {
    Universe universe = createUniverse("secrets-universe", customer.getId());
    Map<String, String> ldapGFlags =
        Map.of(
            "ycql_ldap_bind_passwd",
            YCQL_LDAP_SECRET,
            GFlagsUtil.YSQL_HBA_CONF_CSV,
            HBA_WITH_SECRET);
    Universe.saveDetails(
        universe.getUniverseUUID(),
        u -> {
          UserIntent userIntent = u.getUniverseDetails().getPrimaryCluster().userIntent;
          userIntent.specificGFlags = SpecificGFlags.construct(ldapGFlags, ldapGFlags);
          SpecificGFlags.PerProcessFlags azFlags = new SpecificGFlags.PerProcessFlags();
          azFlags.value.put(ServerType.TSERVER, ldapGFlags);
          userIntent.specificGFlags.setPerAZ(Map.of(UUID.randomUUID(), azFlags));
        });
    ObjectNode details =
        (ObjectNode)
            Json.parse(
                DB.sqlQuery(
                        "SELECT universe_details_json FROM universe WHERE universe_uuid = :uuid")
                    .setParameter("uuid", universe.getUniverseUUID())
                    .findOne()
                    .getString("universe_details_json"));
    ObjectNode userIntent = (ObjectNode) details.at("/clusters/0/userIntent");
    userIntent.put("ysqlPassword", "ysql-admin-lr23812");
    userIntent.put("ycqlPassword", "ycql-admin-lr23812");
    DB.sqlUpdate("UPDATE universe SET universe_details_json = :json WHERE universe_uuid = :uuid")
        .setParameter("json", details.toString())
        .setParameter("uuid", universe.getUniverseUUID())
        .execute();
    return universe;
  }

  private JsonNode rawGetUniverse(UUID universeUUID) {
    Result result =
        doRequestWithAuthToken(
            "GET",
            String.format("/api/v2/customers/%s/universes/%s", customer.getUuid(), universeUUID),
            authToken);
    assertEquals(contentAsString(result), 200, result.status());
    return Json.parse(contentAsString(result));
  }

  private static void assertSecretsRedacted(JsonNode universe) {
    assertFalse(universe.toString(), universe.toString().contains("lr23812"));
    JsonNode gflags = universe.at("/spec/clusters/0/gflags");
    for (JsonNode processGFlags :
        List.of(
            gflags.get("tserver"),
            gflags.get("master"),
            gflags.get("az_gflags").elements().next().get("tserver"))) {
      assertEquals("REDACTED", processGFlags.get("ycql_ldap_bind_passwd").asText());
      assertEquals(HBA_REDACTED, processGFlags.get(GFlagsUtil.YSQL_HBA_CONF_CSV).asText());
    }
    assertAdminPasswordsOmitted(universe);
  }

  private static void assertAdminPasswordsOmitted(JsonNode universe) {
    for (String api : List.of("ysql", "ycql")) {
      JsonNode apiSpec = universe.at("/spec/" + api);
      assertTrue(universe.toString(), apiSpec.isObject());
      assertFalse(apiSpec.toString(), apiSpec.has("password"));
    }
  }

  @Test
  public void testGetUniverseV2RedactSecrets() throws IOException {
    when(mockRuntimeConfig.getBoolean(
            GlobalConfKeys.enableGFlagsSensitiveDataApiRedaction.getKey()))
        .thenReturn(true);
    stubGFlagsValidation();
    Universe universe = createUniverseWithSecrets();

    assertSecretsRedacted(rawGetUniverse(universe.getUniverseUUID()));
  }

  @Test
  public void testGetUniverseV2KeepsLdapGFlagsWithApiRedactionOff() throws IOException {
    // The mocked runtime config leaves enableGFlagsSensitiveDataApiRedaction off.
    stubGFlagsValidation();
    Universe universe = createUniverseWithSecrets();

    JsonNode resp = rawGetUniverse(universe.getUniverseUUID());
    JsonNode tserver = resp.at("/spec/clusters/0/gflags/tserver");
    assertEquals(YCQL_LDAP_SECRET, tserver.get("ycql_ldap_bind_passwd").asText());
    assertEquals(HBA_WITH_SECRET, tserver.get(GFlagsUtil.YSQL_HBA_CONF_CSV).asText());
    assertAdminPasswordsOmitted(resp);
  }

  @Test
  public void testCreateUniverseV2() throws ApiException, IOException {
    UniverseApi api = new UniverseApi();
    UniverseCreateSpec universeCreateSpec = getUniverseCreateSpecV2();

    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    when(mockRuntimeConfig.getInt("yb.universe.otel_collector_metrics_port")).thenReturn(8889);
    when(mockGFlagsValidation.getGFlagDetails(anyString(), anyString(), anyString()))
        .thenReturn(Optional.empty());
    YBATask createTask = api.createUniverse(customer.getUuid(), universeCreateSpec);
    assertThat(createTask.getTaskUuid(), is(fakeTaskUUID));
    ArgumentCaptor<UniverseDefinitionTaskParams> v1CreateParamsCapture =
        ArgumentCaptor.forClass(UniverseDefinitionTaskParams.class);
    verify(mockCommissioner).submit(eq(TaskType.CreateUniverse), v1CreateParamsCapture.capture());
    UniverseDefinitionTaskParams v1CreateParams = v1CreateParamsCapture.getValue();

    // validate that the Universe create params matches properties specified in the createSpec
    validateUniverseCreateSpec(universeCreateSpec, v1CreateParams);
  }

  @Test
  public void testCreateUniverseV2LogsNoSecrets() throws ApiException, IOException {
    UniverseApi api = new UniverseApi();
    UniverseCreateSpec universeCreateSpec = getUniverseCreateSpecV2();
    universeCreateSpec
        .getSpec()
        .getClusters()
        .get(0)
        .getGflags()
        .setTserver(
            // Ordered so that a redaction fallback, which drops the text after the HBA password,
            // would also drop tflag1. ldaptls=0 keeps the password off the end of the HBA line,
            // which falls back until PLAT-23624.
            ImmutableMap.of(
                "ycql_ldap_bind_passwd",
                "ycql-bind-secret-xyz",
                "ysql_hba_conf_csv",
                "host all all 0.0.0.0/0 ldap ldapserver=ldap.example.com"
                    + " ldapbindpasswd=hba-bind-secret-xyz ldaptls=0",
                "tflag1",
                "tval1"));
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    when(mockRuntimeConfig.getInt("yb.universe.otel_collector_metrics_port")).thenReturn(8889);
    when(mockGFlagsValidation.getGFlagDetails(anyString(), anyString(), anyString()))
        .thenReturn(Optional.empty());
    Logger logger = (Logger) LoggerFactory.getLogger(UniverseManagementHandler.class);
    ListAppender<ILoggingEvent> appender = new ListAppender<>();
    appender.start();
    logger.addAppender(appender);
    try {
      api.createUniverse(customer.getUuid(), universeCreateSpec);
    } finally {
      logger.detachAppender(appender);
    }

    List<String> logged =
        appender.list.stream()
            .map(ILoggingEvent::getFormattedMessage)
            .filter(message -> message.startsWith("Create Universe"))
            .collect(Collectors.toList());
    assertEquals(2, logged.size());
    for (String message : logged) {
      for (String secret :
          List.of(
              "ysqlPassword#1", "ycqlPassword#1", "hba-bind-secret-xyz", "ycql-bind-secret-xyz")) {
        assertFalse(secret + " in: " + message, message.contains(secret));
      }
      assertTrue(message, message.contains("tval1"));
      assertFalse(message, message.contains("_redactionParseError"));
    }
  }

  @Test
  public void testCreateUniverseWithRRV2() throws ApiException {
    UniverseApi api = new UniverseApi();
    UniverseCreateSpec universeCreateSpec = getUniverseCreateSpecWithRRV2();

    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    when(mockRuntimeConfig.getInt("yb.universe.otel_collector_metrics_port")).thenReturn(8889);
    YBATask createTask = api.createUniverse(customer.getUuid(), universeCreateSpec);
    assertThat(createTask.getTaskUuid(), is(fakeTaskUUID));
    ArgumentCaptor<UniverseDefinitionTaskParams> v1CreateParamsCapture =
        ArgumentCaptor.forClass(UniverseDefinitionTaskParams.class);
    verify(mockCommissioner).submit(eq(TaskType.CreateUniverse), v1CreateParamsCapture.capture());
    UniverseDefinitionTaskParams v1CreateParams = v1CreateParamsCapture.getValue();

    // validate that the Universe create params matches properties specified in the createSpec
    validateUniverseCreateSpec(universeCreateSpec, v1CreateParams);
  }

  @Test
  public void testDeleteUniverseV2() throws ApiException {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.DestroyUniverse);
    when(mockCommissioner.submit(any(TaskType.class), any(DestroyUniverse.Params.class)))
        .thenReturn(fakeTaskUUID);
    Universe u = createUniverse(customer.getId());
    // Add the cloud info into the universe.
    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = new UniverseDefinitionTaskParams();
          UniverseDefinitionTaskParams.UserIntent userIntent =
              new UniverseDefinitionTaskParams.UserIntent();
          userIntent.providerType = Common.CloudType.aws;
          universeDetails.upsertPrimaryCluster(userIntent, null);
          universe.setUniverseDetails(universeDetails);
        };
    // Save the updates to the universe.
    Universe.saveDetails(u.getUniverseUUID(), updater);

    UniverseApi api = new UniverseApi();
    YBATask deleteTask = api.deleteUniverse(customer.getUuid(), u.getUniverseUUID(), null);
    UUID taskUUID = deleteTask.getTaskUuid();
    assertThat(taskUUID, is(fakeTaskUUID));
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  @Parameters({
    "true, true, false",
    "false, true, true",
    "true, false, false",
    "false, false, true",
    "null, true, false",
  })
  public void testDeleteUniverseWithParamsV2(
      Boolean isForceDelete, Boolean isDeleteBackups, Boolean isDeleteAssociatedCerts)
      throws ApiException {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.DestroyUniverse);
    when(mockCommissioner.submit(any(TaskType.class), any(DestroyUniverse.Params.class)))
        .thenReturn(fakeTaskUUID);
    Universe u = createUniverse(customer.getId());
    // Add the cloud info into the universe.
    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = new UniverseDefinitionTaskParams();
          UniverseDefinitionTaskParams.UserIntent userIntent =
              new UniverseDefinitionTaskParams.UserIntent();
          userIntent.providerType = Common.CloudType.aws;
          universeDetails.upsertPrimaryCluster(userIntent, null);
          universe.setUniverseDetails(universeDetails);
        };
    // Save the updates to the universe.
    Universe.saveDetails(u.getUniverseUUID(), updater);

    UniverseDeleteSpec universeDeleteSpec = new UniverseDeleteSpec();
    universeDeleteSpec
        .isForceDelete(isForceDelete)
        .isDeleteBackups(isDeleteBackups)
        .isDeleteAssociatedCerts(isDeleteAssociatedCerts);
    UniverseApi api = new UniverseApi();
    YBATask deleteTask =
        api.deleteUniverse(customer.getUuid(), u.getUniverseUUID(), universeDeleteSpec);
    UUID taskUUID = deleteTask.getTaskUuid();
    assertThat(taskUUID, is(fakeTaskUUID));
    assertAuditEntry(1, customer.getUuid());
    ArgumentCaptor<DestroyUniverse.Params> destroyParams =
        ArgumentCaptor.forClass(DestroyUniverse.Params.class);
    verify(mockCommissioner).submit(eq(TaskType.DestroyUniverse), destroyParams.capture());
    assertThat(destroyParams.getValue().isForceDelete, is(isForceDelete));
    assertThat(destroyParams.getValue().isDeleteBackups, is(isDeleteBackups));
    assertThat(destroyParams.getValue().isDeleteAssociatedCerts, is(isDeleteAssociatedCerts));
  }
}
