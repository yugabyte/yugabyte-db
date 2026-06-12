// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.isNull;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.audit.AuditService;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.pitr.PitrConfigHelper;
import com.yugabyte.yw.common.services.config.YbClientConfig;
import com.yugabyte.yw.common.services.config.YbClientConfigFactory;
import com.yugabyte.yw.forms.CreatePitrConfigParams;
import com.yugabyte.yw.forms.RestoreSnapshotScheduleParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.PrevYBSoftwareConfig;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.SoftwareUpgradeState;
import com.yugabyte.yw.forms.UpdatePitrConfigParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.PitrConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.CommonTypes.TableType;
import org.yb.CommonTypes.YQLDatabase;
import org.yb.client.ListSnapshotSchedulesResponse;
import org.yb.client.SnapshotInfo;
import org.yb.client.SnapshotScheduleInfo;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo.SysSnapshotEntryPB.State;
import play.libs.Json;
import play.mvc.Result;

@RunWith(JUnitParamsRunner.class)
public class PitrControllerTest extends FakeDBApplication {

  public static final Logger LOG = LoggerFactory.getLogger(PitrControllerTest.class);
  private static final long DEFAULT_RETENTION_PERIOD_SEC = 7 * 86400L;
  private static final long DEFAULT_INTERVAL_SEC = 86400L;

  private Universe defaultUniverse;
  private Users defaultUser;
  private Customer defaultCustomer;
  private YBClient mockClient;
  private PitrController pitrController;
  private ListSnapshotSchedulesResponse mockListSnapshotSchedulesResponse;

  @Before
  public void setUp() {
    mockClient = mock(YBClient.class);
    mockListSnapshotSchedulesResponse = mock(ListSnapshotSchedulesResponse.class);
    when(mockService.getUniverseClient(any())).thenReturn(mockClient);
    lenient()
        .when(mockService.getClientWithConfig(any(YbClientConfig.class)))
        .thenReturn(mockClient);
    lenient().when(mockListSnapshotSchedulesResponse.hasError()).thenReturn(false);
    defaultCustomer = ModelFactory.testCustomer();
    defaultUser = ModelFactory.testUser(defaultCustomer);
    defaultUniverse = ModelFactory.createUniverse(defaultCustomer.getId());
    UniverseDefinitionTaskParams details = defaultUniverse.getUniverseDetails();
    details.getPrimaryCluster().userIntent.ybSoftwareVersion = "2.14.0.0-b111";
    defaultUniverse.setUniverseDetails(details);
    defaultUniverse.save();
    Commissioner commissioner = app.injector().instanceOf(Commissioner.class);
    RuntimeConfGetter confGetter = app.injector().instanceOf(RuntimeConfGetter.class);
    YbClientConfigFactory ybClientConfigFactory =
        app.injector().instanceOf(YbClientConfigFactory.class);
    PitrConfigHelper pitrConfigHelper =
        new PitrConfigHelper(commissioner, mockService, ybClientConfigFactory, confGetter);
    pitrController = new PitrController(commissioner, mockService, pitrConfigHelper);
    pitrController.setAuditService(new AuditService());
  }

  private PitrConfig createPitrConfigInDb() {
    return createPitrConfigInDb(
        UUID.randomUUID(), "yugabyte", TableType.PGSQL_TABLE_TYPE, DEFAULT_RETENTION_PERIOD_SEC);
  }

  private PitrConfig createPitrConfigInDb(
      UUID scheduleUuid, String keyspaceName, TableType tableType) {
    return createPitrConfigInDb(
        scheduleUuid, keyspaceName, tableType, DEFAULT_RETENTION_PERIOD_SEC);
  }

  private PitrConfig createPitrConfigInDb(
      UUID scheduleUuid, String keyspaceName, TableType tableType, long retentionPeriodSec) {
    CreatePitrConfigParams params = new CreatePitrConfigParams();
    params.retentionPeriodInSeconds = retentionPeriodSec;
    params.intervalInSeconds = PitrControllerTest.DEFAULT_INTERVAL_SEC;
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.customerUUID = defaultCustomer.getUuid();
    params.keyspaceName = keyspaceName;
    params.tableType = tableType;
    return PitrConfig.create(scheduleUuid, params);
  }

  private ObjectNode defaultPitrConfigBodyJson() {
    return pitrConfigBodyJson(DEFAULT_RETENTION_PERIOD_SEC);
  }

  private ObjectNode pitrConfigBodyJson(long retentionPeriodSec) {
    return Json.newObject()
        .put("retentionPeriodInSeconds", retentionPeriodSec)
        .put("intervalInSeconds", PitrControllerTest.DEFAULT_INTERVAL_SEC);
  }

  private UpdatePitrConfigParams defaultUpdatePitrConfigParams(UUID pitrConfigUuid) {
    UpdatePitrConfigParams params = new UpdatePitrConfigParams();
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.pitrConfigUUID = pitrConfigUuid;
    params.retentionPeriodInSeconds = DEFAULT_RETENTION_PERIOD_SEC;
    params.intervalInSeconds = DEFAULT_INTERVAL_SEC;
    return params;
  }

  private RestoreSnapshotScheduleParams restorePitrParams(UUID pitrConfigUuid) {
    RestoreSnapshotScheduleParams params = new RestoreSnapshotScheduleParams();
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.pitrConfigUUID = pitrConfigUuid;
    params.restoreTimeInMillis = System.currentTimeMillis();
    return params;
  }

  private Result createPitrConfig(
      UUID universeUUID, String dbEndpoint, String keyspaceName, ObjectNode bodyJson) {
    String authToken = defaultUser.createAuthToken();
    String method = "POST";
    String url =
        "/api/customers/"
            + defaultCustomer.getUuid()
            + "/universes/"
            + universeUUID.toString()
            + "/keyspaces/"
            + dbEndpoint
            + "/"
            + keyspaceName
            + "/pitr_config";
    return doRequestWithAuthTokenAndBody(method, url, authToken, bodyJson);
  }

  private Result deletePitrConfig(UUID universeUUID, UUID pitrConfigUUID) {
    String authToken = defaultUser.createAuthToken();
    String method = "DELETE";
    String url =
        "/api/customers/"
            + defaultCustomer.getUuid()
            + "/universes/"
            + universeUUID.toString()
            + "/pitr_config/"
            + pitrConfigUUID.toString();
    return doRequestWithAuthToken(method, url, authToken);
  }

  private Result updatePitrConfig(UUID universeUUID, UUID pitrConfigUUID, JsonNode bodyJson) {
    String authToken = defaultUser.createAuthToken();
    String method = "PUT";
    String url =
        "/api/customers/"
            + defaultCustomer.getUuid()
            + "/universes/"
            + universeUUID.toString()
            + "/pitr_config/"
            + pitrConfigUUID.toString();
    return doRequestWithAuthTokenAndBody(method, url, authToken, bodyJson);
  }

  private Result performPitr(UUID universeUUID, JsonNode bodyJson) {
    String authToken = defaultUser.createAuthToken();
    String method = "POST";
    String url =
        "/api/customers/"
            + defaultCustomer.getUuid()
            + "/universes/"
            + universeUUID.toString()
            + "/pitr";
    return doRequestWithAuthTokenAndBody(method, url, authToken, bodyJson);
  }

  @Test
  public void testCreatePitrConfig() {
    UUID fakeTaskUUID = buildTaskInfo(null, TaskType.CreatePitrConfig);
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    ObjectNode bodyJson = defaultPitrConfigBodyJson();
    Result r = createPitrConfig(defaultUniverse.getUniverseUUID(), "YSQL", "yugabyte", bodyJson);
    assertOk(r);
    JsonNode resultJson = Json.parse(contentAsString(r));
    assertValue(resultJson, "taskUUID", fakeTaskUUID.toString());
    assertEquals(OK, r.status());
    verify(mockCommissioner, times(1)).submit(any(), any());
    assertAuditEntry(1, defaultCustomer.getUuid());
  }

  @Test
  public void testCreatePitrConfigWithIncompatibleUniverse() {
    UUID fakeTaskUUID = UUID.randomUUID();
    UniverseDefinitionTaskParams details = defaultUniverse.getUniverseDetails();
    details.getPrimaryCluster().userIntent.ybSoftwareVersion = "2.12.0.0-b111";
    defaultUniverse.setUniverseDetails(details);
    defaultUniverse.save();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    ObjectNode bodyJson = defaultPitrConfigBodyJson();
    Result r =
        assertPlatformException(
            () ->
                createPitrConfig(defaultUniverse.getUniverseUUID(), "YSQL", "yugabyte", bodyJson));
    assertEquals(BAD_REQUEST, r.status());
    verify(mockCommissioner, never()).submit(any(), any());
  }

  @Test
  public void testCreatePitrConfigWithoutUniverse() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    ObjectNode bodyJson = defaultPitrConfigBodyJson();
    Result r =
        assertPlatformException(
            () -> createPitrConfig(UUID.randomUUID(), "YSQL", "yugabyte", bodyJson));
    assertEquals(BAD_REQUEST, r.status());
    verify(mockCommissioner, never()).submit(any(), any());
  }

  @Test
  public void testCreatePitrConfigWithUniversePaused() {
    UUID fakeTaskUUID = UUID.randomUUID();
    UniverseDefinitionTaskParams details = defaultUniverse.getUniverseDetails();
    details.universePaused = true;
    defaultUniverse.setUniverseDetails(details);
    defaultUniverse.save();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    ObjectNode bodyJson = defaultPitrConfigBodyJson();
    Result r =
        assertPlatformException(
            () ->
                createPitrConfig(defaultUniverse.getUniverseUUID(), "YSQL", "yugabyte", bodyJson));
    assertEquals(BAD_REQUEST, r.status());
    verify(mockCommissioner, never()).submit(any(), any());
  }

  @Test
  public void testCreatePitrConfigWithUniverseUpdateInProgress() {
    UUID fakeTaskUUID = UUID.randomUUID();
    UniverseDefinitionTaskParams details = defaultUniverse.getUniverseDetails();
    details.updateInProgress = true;
    defaultUniverse.setUniverseDetails(details);
    defaultUniverse.save();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    ObjectNode bodyJson = defaultPitrConfigBodyJson();
    Result r =
        assertPlatformException(
            () ->
                createPitrConfig(defaultUniverse.getUniverseUUID(), "YSQL", "yugabyte", bodyJson));
    assertBadRequest(r, "Cannot enable PITR when the universe is in locked state");
    verify(mockCommissioner, never()).submit(any(), any());
  }

  @Test
  public void testCreatePitrConfigWithNegativeRetentionPeriod() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    ObjectNode bodyJson = pitrConfigBodyJson(-DEFAULT_RETENTION_PERIOD_SEC);
    Result r =
        assertPlatformException(
            () ->
                createPitrConfig(defaultUniverse.getUniverseUUID(), "YSQL", "yugabyte", bodyJson));
    JsonNode resultJson = Json.parse(contentAsString(r));
    assertValue(resultJson, "error", "PITR Config retention period cannot be less than 1 second");
    assertEquals(BAD_REQUEST, r.status());
    verify(mockCommissioner, never()).submit(any(), any());
  }

  @Test
  public void testCreatePitrConfigWithIntervalGreaterThanRetention() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    ObjectNode bodyJson = pitrConfigBodyJson(43200L);
    Result r =
        assertPlatformException(
            () ->
                createPitrConfig(defaultUniverse.getUniverseUUID(), "YSQL", "yugabyte", bodyJson));
    JsonNode resultJson = Json.parse(contentAsString(r));
    assertValue(resultJson, "error", "PITR Config interval cannot be less than retention period");
    assertEquals(BAD_REQUEST, r.status());
    verify(mockCommissioner, never()).submit(any(), any());
  }

  @Test
  public void testCreatePitrConfigWithIncorrectTableType() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    ObjectNode bodyJson = defaultPitrConfigBodyJson();
    assertThrows(
        IllegalArgumentException.class,
        () -> createPitrConfig(defaultUniverse.getUniverseUUID(), "YQL", "yugabyte", bodyJson));
  }

  @Test
  public void testCreatePitrConfigWithPitrConfigAlreadyPresent() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    ObjectNode bodyJson = defaultPitrConfigBodyJson();
    createPitrConfigInDb();
    Result r =
        assertPlatformException(
            () ->
                createPitrConfig(defaultUniverse.getUniverseUUID(), "YSQL", "yugabyte", bodyJson));
    JsonNode resultJson = Json.parse(contentAsString(r));
    assertValue(resultJson, "error", "PITR Config is already present");
    assertEquals(BAD_REQUEST, r.status());
    verify(mockCommissioner, never()).submit(any(), any());
  }

  @Test
  public void testListPitrConfigs() throws Exception {
    List<SnapshotScheduleInfo> scheduleInfoList = new ArrayList<>();
    Map<UUID, SnapshotScheduleInfo> scheduleInfoMap = new HashMap<>();

    long currentTime11 = System.currentTimeMillis();
    UUID scheduleUUID1 = UUID.randomUUID();
    createPitrConfigInDb(scheduleUUID1, "yugabyte", TableType.PGSQL_TABLE_TYPE);
    List<SnapshotInfo> snapshotList1 = new ArrayList<>();
    UUID snapshotUUID11 = UUID.randomUUID();
    SnapshotInfo snapshot11 =
        new SnapshotInfo(
            snapshotUUID11,
            currentTime11 + 1000 * 86500L,
            currentTime11 + 1000 * 100L,
            State.COMPLETE);
    snapshotList1.add(snapshot11);
    UUID snapshotUUID12 = UUID.randomUUID();
    SnapshotInfo snapshot12 =
        new SnapshotInfo(
            snapshotUUID12, currentTime11 + 1000 * 100L, currentTime11, State.COMPLETE);
    snapshotList1.add(snapshot12);
    SnapshotScheduleInfo schedule1 =
        new SnapshotScheduleInfo(
            scheduleUUID1,
            86400L,
            7L * 86400L,
            snapshotList1,
            null,
            null,
            YQLDatabase.YQL_DATABASE_PGSQL);
    scheduleInfoList.add(schedule1);
    scheduleInfoMap.put(scheduleUUID1, schedule1);

    long currentTime21 = System.currentTimeMillis();
    UUID scheduleUUID2 = UUID.randomUUID();
    createPitrConfigInDb(scheduleUUID2, "postgres", TableType.PGSQL_TABLE_TYPE);
    List<SnapshotInfo> snapshotList2 = new ArrayList<>();
    UUID snapshotUUID21 = UUID.randomUUID();
    SnapshotInfo snapshot21 =
        new SnapshotInfo(
            snapshotUUID21,
            currentTime21 + 1000 * 86500L,
            currentTime21 + 1000 * 100L,
            State.FAILED);
    snapshotList2.add(snapshot21);
    UUID snapshotUUID22 = UUID.randomUUID();
    SnapshotInfo snapshot22 =
        new SnapshotInfo(
            snapshotUUID22, currentTime21 + 1000 * 100L, currentTime21, State.COMPLETE);
    snapshotList2.add(snapshot22);
    SnapshotScheduleInfo schedule2 =
        new SnapshotScheduleInfo(
            scheduleUUID2,
            86400L,
            7L * 86400L,
            snapshotList2,
            null,
            null,
            YQLDatabase.YQL_DATABASE_PGSQL);
    scheduleInfoList.add(schedule2);
    scheduleInfoMap.put(scheduleUUID2, schedule2);

    long currentTime3 = System.currentTimeMillis();
    UUID scheduleUUID3 = UUID.randomUUID();
    createPitrConfigInDb(scheduleUUID3, "cassandra", TableType.YQL_TABLE_TYPE);
    List<SnapshotInfo> snapshotList3 = new ArrayList<>();
    UUID snapshotUUID31 = UUID.randomUUID();
    // First ever snapshot
    SnapshotInfo snapshot31 =
        new SnapshotInfo(snapshotUUID31, currentTime3 + 1000 * 100L, 0L, State.COMPLETE);
    snapshotList3.add(snapshot31);
    SnapshotScheduleInfo schedule3 =
        new SnapshotScheduleInfo(
            scheduleUUID3,
            86400L,
            7L * 86400L,
            snapshotList3,
            null,
            null,
            YQLDatabase.YQL_DATABASE_PGSQL);
    scheduleInfoList.add(schedule3);
    scheduleInfoMap.put(scheduleUUID3, schedule3);

    long currentTime4 = System.currentTimeMillis();
    UUID scheduleUUID4 = UUID.randomUUID();
    PitrConfig pitr4 =
        createPitrConfigInDb(scheduleUUID4, "test_update", TableType.YQL_TABLE_TYPE, 86400L);
    List<SnapshotInfo> snapshotList4 = new ArrayList<>();
    UUID snapshotUUID41 = UUID.randomUUID();
    SnapshotInfo snapshot41 =
        new SnapshotInfo(
            snapshotUUID41,
            currentTime4 + 2000 * 86500L,
            currentTime4 + 1000 * 100L,
            State.COMPLETE);
    snapshotList4.add(snapshot41);
    UUID snapshotUUID42 = UUID.randomUUID();
    SnapshotInfo snapshot42 =
        new SnapshotInfo(
            snapshotUUID42,
            currentTime4 + 1000 * 86500L,
            currentTime4 + 1000 * 100L,
            State.COMPLETE);
    snapshotList4.add(snapshot42);
    UUID snapshotUUID43 = UUID.randomUUID();
    SnapshotInfo snapshot43 =
        new SnapshotInfo(snapshotUUID43, currentTime4 + 1000 * 100L, currentTime4, State.COMPLETE);
    snapshotList4.add(snapshot43);
    SnapshotScheduleInfo schedule4 =
        new SnapshotScheduleInfo(
            scheduleUUID4,
            86400L,
            2L * 86400L,
            snapshotList4,
            null,
            null,
            YQLDatabase.YQL_DATABASE_PGSQL);
    scheduleInfoList.add(schedule4);
    scheduleInfoMap.put(scheduleUUID4, schedule4);
    // PITR config is updated to have a longer retention period.
    pitr4.setRetentionPeriod(4 * 86400L);
    // ERT is frozen at the ERT at the time of update.
    pitr4.setIntermittentMinRecoverTimeInMillis(currentTime4 + 1800 * 86500L);
    pitr4.save();

    when(mockListSnapshotSchedulesResponse.getSnapshotScheduleInfoList())
        .thenReturn(scheduleInfoList);
    when(mockClient.listSnapshotSchedules(any())).thenReturn(mockListSnapshotSchedulesResponse);
    Result r =
        pitrController.listPitrConfigs(
            defaultCustomer.getUuid(), defaultUniverse.getUniverseUUID());
    JsonNode json = Json.parse(contentAsString(r));
    assertEquals(OK, r.status());
    assertTrue(json.isArray());
    Iterator<JsonNode> it = json.elements();
    int count = 0;
    while (it.hasNext()) {
      JsonNode schedule = it.next();
      count++;
      long intervalInSecs = schedule.get("scheduleInterval").asLong();
      long retentionDurationInSecs = schedule.get("retentionPeriod").asLong();
      assertEquals(86400L, intervalInSecs);
      UUID scheduleUUID = UUID.fromString(schedule.get("uuid").asText());
      if (!scheduleInfoMap.containsKey(scheduleUUID)) {
        Assert.fail();
      }
      long minTime = schedule.get("minRecoverTimeInMillis").asLong();
      long maxTime = schedule.get("maxRecoverTimeInMillis").asLong();
      String state = schedule.get("state").asText();
      if (scheduleUUID.equals(scheduleUUID1)) {
        assertEquals(7 * 86400L, retentionDurationInSecs);
        assertTrue(System.currentTimeMillis() > minTime);
        assertTrue(currentTime3 < maxTime);
        assertEquals("COMPLETE", state);
      } else if (scheduleUUID.equals(scheduleUUID2)) {
        assertEquals(7 * 86400L, retentionDurationInSecs);
        assertTrue(System.currentTimeMillis() > minTime);
        assertTrue(currentTime3 < maxTime);
        assertEquals("FAILED", state);
      } else if (scheduleUUID.equals(scheduleUUID3)) {
        assertEquals(7 * 86400L, retentionDurationInSecs);
        assertTrue(System.currentTimeMillis() > minTime);
        assertEquals("COMPLETE", state);
      } else if (scheduleUUID.equals(scheduleUUID4)) {
        assertEquals(4 * 86400L, retentionDurationInSecs);
        assertEquals(currentTime4 + 1800 * 86500L, minTime);
        assertEquals("COMPLETE", state);
      } else {
        Assert.fail();
      }
    }
    assertEquals(4, count);
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testListPitrConfigsWithUniversePaused() {
    UniverseDefinitionTaskParams details = defaultUniverse.getUniverseDetails();
    details.universePaused = true;
    defaultUniverse.setUniverseDetails(details);
    defaultUniverse.save();

    UUID scheduleUUID1 = UUID.randomUUID();
    createPitrConfigInDb(scheduleUUID1, "yugabyte", TableType.PGSQL_TABLE_TYPE);

    UUID scheduleUUID2 = UUID.randomUUID();
    createPitrConfigInDb(scheduleUUID2, "postgres", TableType.PGSQL_TABLE_TYPE);

    UUID scheduleUUID3 = UUID.randomUUID();
    createPitrConfigInDb(scheduleUUID3, "cassandra", TableType.YQL_TABLE_TYPE);

    Result r =
        pitrController.listPitrConfigs(
            defaultCustomer.getUuid(), defaultUniverse.getUniverseUUID());
    JsonNode json = Json.parse(contentAsString(r));
    assertEquals(OK, r.status());
    assertTrue(json.isArray());
    Iterator<JsonNode> it = json.elements();
    int count = 0;
    while (it.hasNext()) {
      JsonNode schedule = it.next();
      count++;
      long intervalInSecs = schedule.get("scheduleInterval").asLong();
      long retentionDurationInSecs = schedule.get("retentionPeriod").asLong();
      assertEquals(86400L, intervalInSecs);
      assertEquals(7 * 86400L, retentionDurationInSecs);
      UUID scheduleUUID = UUID.fromString(schedule.get("uuid").asText());
      long minTime = schedule.get("minRecoverTimeInMillis").asLong();
      long maxTime = schedule.get("maxRecoverTimeInMillis").asLong();
      String state = schedule.get("state").asText();
      if (scheduleUUID.equals(scheduleUUID1)) {
        assertTrue(System.currentTimeMillis() > minTime);
        assertEquals(minTime, maxTime);
        assertEquals("UNKNOWN", state);
      } else if (scheduleUUID.equals(scheduleUUID2)) {
        assertTrue(System.currentTimeMillis() > minTime);
        assertEquals(minTime, maxTime);
        assertEquals("UNKNOWN", state);
      } else if (scheduleUUID.equals(scheduleUUID3)) {
        assertTrue(System.currentTimeMillis() > minTime);
        assertEquals(minTime, maxTime);
        assertEquals("UNKNOWN", state);
      } else {
        Assert.fail();
      }
    }
    assertEquals(3, count);
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testListPitrConfigsWithListSnapshotsFailure() throws Exception {
    createPitrConfigInDb(UUID.randomUUID(), "yugabyte", TableType.PGSQL_TABLE_TYPE);

    when(mockClient.listSnapshotSchedules(isNull())).thenThrow(new RuntimeException("master down"));

    assertThrows(
        PlatformServiceException.class,
        () ->
            pitrController.listPitrConfigs(
                defaultCustomer.getUuid(), defaultUniverse.getUniverseUUID()));
  }

  @Test
  public void testListPitrConfigsWithIncompatibleUniverse() {
    UUID fakeTaskUUID = UUID.randomUUID();
    UniverseDefinitionTaskParams details = defaultUniverse.getUniverseDetails();
    details.getPrimaryCluster().userIntent.ybSoftwareVersion = "2.12.0.0-b111";
    defaultUniverse.setUniverseDetails(details);
    defaultUniverse.save();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    Result r =
        assertPlatformException(
            () ->
                pitrController.listPitrConfigs(
                    defaultCustomer.getUuid(), defaultUniverse.getUniverseUUID()));
    assertEquals(BAD_REQUEST, r.status());
    verify(mockCommissioner, never()).submit(any(), any());
  }

  @Test
  public void testDeletePitrConfig() {
    PitrConfig pitr1 =
        createPitrConfigInDb(UUID.randomUUID(), "yugabyte", TableType.PGSQL_TABLE_TYPE);

    UUID fakeTaskUUID = buildTaskInfo(null, TaskType.DeletePitrConfig);
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    Result r = deletePitrConfig(defaultUniverse.getUniverseUUID(), pitr1.getUuid());
    assertOk(r);
    JsonNode resultJson = Json.parse(contentAsString(r));
    assertValue(resultJson, "taskUUID", fakeTaskUUID.toString());
    assertEquals(OK, r.status());
    verify(mockCommissioner).submit(any(), any());
    assertAuditEntry(1, defaultCustomer.getUuid());
  }

  @Test
  public void testDeletePitrConfigWithUniversePaused() {
    UUID scheduleUUID3 = UUID.randomUUID();
    createPitrConfigInDb(scheduleUUID3, "cassandra", TableType.YQL_TABLE_TYPE);

    UniverseDefinitionTaskParams details = defaultUniverse.getUniverseDetails();
    details.universePaused = true;
    defaultUniverse.setUniverseDetails(details);
    defaultUniverse.save();
    Result r =
        assertPlatformException(
            () ->
                pitrController.deletePitrConfig(
                    defaultCustomer.getUuid(),
                    defaultUniverse.getUniverseUUID(),
                    scheduleUUID3,
                    fakeRequest));
    assertEquals(BAD_REQUEST, r.status());
    verify(mockCommissioner, never()).submit(any(), any());
  }

  @Test
  public void testDeletePitrConfigWithUniverseUpdateInProgress() {
    UUID scheduleUUID3 = UUID.randomUUID();
    createPitrConfigInDb(scheduleUUID3, "cassandra", TableType.YQL_TABLE_TYPE);

    UniverseDefinitionTaskParams details = defaultUniverse.getUniverseDetails();
    details.updateInProgress = true;
    defaultUniverse.setUniverseDetails(details);
    defaultUniverse.save();
    Result r =
        assertPlatformException(
            () ->
                pitrController.deletePitrConfig(
                    defaultCustomer.getUuid(),
                    defaultUniverse.getUniverseUUID(),
                    scheduleUUID3,
                    fakeRequest));
    assertBadRequest(r, "Cannot delete PITR config when the universe is in locked state");
    verify(mockCommissioner, never()).submit(any(), any());
  }

  @Test
  public void testDeletePitrConfigWithIncompatibleUniverse() {
    UUID scheduleUUID3 = UUID.randomUUID();
    UniverseDefinitionTaskParams details = defaultUniverse.getUniverseDetails();
    details.getPrimaryCluster().userIntent.ybSoftwareVersion = "2.12.0.0-b111";
    defaultUniverse.setUniverseDetails(details);
    defaultUniverse.save();
    Result r =
        assertPlatformException(
            () ->
                pitrController.deletePitrConfig(
                    defaultCustomer.getUuid(),
                    defaultUniverse.getUniverseUUID(),
                    scheduleUUID3,
                    fakeRequest));
    assertEquals(BAD_REQUEST, r.status());
    verify(mockCommissioner, never()).submit(any(), any());
  }

  @Test
  public void testPerformPitr() throws Exception {
    List<SnapshotScheduleInfo> scheduleInfoList = new ArrayList<>();
    UUID pitrConfigUUID = UUID.randomUUID();
    createPitrConfigInDb(pitrConfigUUID, "cassandra", TableType.YQL_TABLE_TYPE);
    UUID fakeTaskUUID = buildTaskInfo(null, TaskType.CreatePitrConfig);
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    UUID snapshotUUID = UUID.randomUUID();
    long snapshotTime = System.currentTimeMillis() + 2 * 1000 * 86400L;
    List<SnapshotInfo> snapshotList = new ArrayList<>();
    SnapshotInfo snapshot =
        new SnapshotInfo(snapshotUUID, snapshotTime, snapshotTime - 1000 * 86400L, State.COMPLETE);
    snapshotList.add(snapshot);
    SnapshotScheduleInfo schedule =
        new SnapshotScheduleInfo(
            pitrConfigUUID,
            86400L,
            7L * 86400L,
            snapshotList,
            null,
            null,
            YQLDatabase.YQL_DATABASE_PGSQL);
    scheduleInfoList.add(schedule);
    when(mockListSnapshotSchedulesResponse.getSnapshotScheduleInfoList())
        .thenReturn(scheduleInfoList);
    when(mockClient.listSnapshotSchedules(any())).thenReturn(mockListSnapshotSchedulesResponse);

    RestoreSnapshotScheduleParams params = restorePitrParams(pitrConfigUUID);
    Result r = performPitr(defaultUniverse.getUniverseUUID(), Json.toJson(params));
    assertOk(r);
    JsonNode resultJson = Json.parse(contentAsString(r));
    assertValue(resultJson, "taskUUID", fakeTaskUUID.toString());
    assertEquals(OK, r.status());
    verify(mockCommissioner).submit(any(), any());
    assertAuditEntry(1, defaultCustomer.getUuid());
  }

  @Test
  public void testPerformPitrWithNoUniverse() {
    RestoreSnapshotScheduleParams params = restorePitrParams(UUID.randomUUID());

    Result r = assertPlatformException(() -> performPitr(UUID.randomUUID(), Json.toJson(params)));
    assertEquals(BAD_REQUEST, r.status());
    verify(mockCommissioner, never()).submit(any(), any());
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testPerformPitrWithUniversePaused() {
    RestoreSnapshotScheduleParams params = restorePitrParams(UUID.randomUUID());

    UniverseDefinitionTaskParams details = defaultUniverse.getUniverseDetails();
    details.universePaused = true;
    defaultUniverse.setUniverseDetails(details);
    defaultUniverse.save();

    Result r =
        assertPlatformException(
            () -> performPitr(defaultUniverse.getUniverseUUID(), Json.toJson(params)));
    assertEquals(BAD_REQUEST, r.status());
    verify(mockCommissioner, never()).submit(any(), any());
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testPerformPitrWithUniverseUpdateInProgress() {
    RestoreSnapshotScheduleParams params = restorePitrParams(UUID.randomUUID());

    UniverseDefinitionTaskParams details = defaultUniverse.getUniverseDetails();
    details.updateInProgress = true;
    defaultUniverse.setUniverseDetails(details);
    defaultUniverse.save();

    Result r =
        assertPlatformException(
            () -> performPitr(defaultUniverse.getUniverseUUID(), Json.toJson(params)));
    assertBadRequest(r, "Cannot perform PITR when the universe is in locked state");
    verify(mockCommissioner, never()).submit(any(), any());
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testPerformPitrWithIncompatibleUniverse() {
    RestoreSnapshotScheduleParams params = restorePitrParams(UUID.randomUUID());

    UniverseDefinitionTaskParams details = defaultUniverse.getUniverseDetails();
    details.getPrimaryCluster().userIntent.ybSoftwareVersion = "2.12.0.0-b111";
    defaultUniverse.setUniverseDetails(details);
    defaultUniverse.save();

    Result r =
        assertPlatformException(
            () -> performPitr(defaultUniverse.getUniverseUUID(), Json.toJson(params)));
    assertEquals(BAD_REQUEST, r.status());
    verify(mockCommissioner, never()).submit(any(), any());
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testPerformPitrWhenYsqlMajorVersionUpgradeInMonitoringPhase() {
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            universe -> {
              universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion =
                  "2025.1.0.0-b1";
              universe.getUniverseDetails().prevYBSoftwareConfig = new PrevYBSoftwareConfig();
              universe.getUniverseDetails().softwareUpgradeState = SoftwareUpgradeState.PreFinalize;
              universe
                  .getUniverseDetails()
                  .prevYBSoftwareConfig
                  .setSoftwareVersion("2024.2.3.0-b1");
            });

    ObjectNode bodyJson = defaultPitrConfigBodyJson();
    Result r =
        assertPlatformException(
            () ->
                createPitrConfig(defaultUniverse.getUniverseUUID(), "YSQL", "yugabyte", bodyJson));
    assertBadRequest(r, "Cannot enable PITR when the universe is not in ready state");
    verify(mockCommissioner, times(0)).submit(any(), any());
    assertAuditEntry(0, defaultCustomer.getUuid());

    RestoreSnapshotScheduleParams params = restorePitrParams(UUID.randomUUID());

    r =
        assertPlatformException(
            () -> performPitr(defaultUniverse.getUniverseUUID(), Json.toJson(params)));
    assertBadRequest(r, "Cannot perform PITR when the universe is not in ready state");
    verify(mockCommissioner, times(0)).submit(any(), any());
    assertAuditEntry(0, defaultCustomer.getUuid());

    UUID pitrConfigUuid = UUID.randomUUID();
    r =
        assertPlatformException(
            () ->
                updatePitrConfig(
                    defaultUniverse.getUniverseUUID(),
                    pitrConfigUuid,
                    Json.toJson(defaultUpdatePitrConfigParams(pitrConfigUuid))));
    assertBadRequest(r, "Cannot update PITR when the universe is not in ready state");
  }
}
