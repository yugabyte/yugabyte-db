package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.FakeDBApplication.buildTaskInfo;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.common.TestHelper.testDatabase;
import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.ArgumentMatchers.nullable;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.protobuf.ByteString;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.XClusterScheduler;
import com.yugabyte.yw.common.DrConfigStates.State;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformGuiceApplicationBaseTest;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.audit.AuditService;
import com.yugabyte.yw.common.backuprestore.BackupHelper;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.gflags.AutoFlagUtil;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.DrConfigCreateForm;
import com.yugabyte.yw.forms.DrConfigCreateForm.PitrParams;
import com.yugabyte.yw.forms.DrConfigFailoverForm;
import com.yugabyte.yw.forms.DrConfigReplaceReplicaForm;
import com.yugabyte.yw.forms.DrConfigRestartForm;
import com.yugabyte.yw.forms.DrConfigSetDatabasesForm;
import com.yugabyte.yw.forms.DrConfigSwitchoverForm;
import com.yugabyte.yw.forms.DrConfigTaskParams;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData.BootstrapParams.BootstrapBackupParams;
import com.yugabyte.yw.forms.XClusterConfigRestartFormData.RestartBootstrapParams;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.ConfigType;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.XClusterNamespaceConfig;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.CommonTypes;
import org.yb.CommonTypes.TableType;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.GetUniverseReplicationInfoResponse;
import org.yb.client.ListTablesResponse;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo;
import org.yb.master.MasterDdlOuterClass;
import org.yb.master.MasterReplicationOuterClass;
import org.yb.master.MasterTypes;
import org.yb.master.MasterTypes.RelationType;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

@RunWith(MockitoJUnitRunner.class)
public class DrConfigControllerTest extends PlatformGuiceApplicationBaseTest {
  private final Commissioner mockCommissioner = mock(Commissioner.class);
  private final MetricQueryHelper mockMetricQueryHelper = mock(MetricQueryHelper.class);
  private final BackupHelper mockBackupHelper = mock(BackupHelper.class);
  private final CustomerConfigService mockCustomerConfigService = mock(CustomerConfigService.class);
  private final YBClientService mockYBClientService = mock(YBClientService.class);
  private final XClusterUniverseService mockXClusterUniverseService =
      mock(XClusterUniverseService.class);
  private final AutoFlagUtil mockAutoFlagUtil = mock(AutoFlagUtil.class);
  private final XClusterScheduler mockXClusterScheduler = mock(XClusterScheduler.class);
  private final YBClient mockYBClient = mock(YBClient.class);
  private final AuditService auditService = spy(new AuditService());

  private Universe sourceUniverse;
  private Universe targetUniverse;
  private Customer defaultCustomer;
  private String authToken;
  private BootstrapBackupParams backupRequestParams;
  private SettableRuntimeConfigFactory settableRuntimeConfigFactory;
  private String namespaceId;

  private CustomerConfig createData(Customer customer) {
    JsonNode formData =
        Json.parse(
            "{\"name\": \"Test\", \"configName\": \"Test\", \"type\": "
                + "\"STORAGE\", \"data\": {\"foo\": \"bar\"},"
                + "\"configUUID\": \"5e8e4887-343b-47dd-a126-71c822904c06\"}");
    return CustomerConfig.createWithFormData(customer.getUuid(), formData);
  }

  private DrConfigCreateForm createDefaultCreateForm(String name, Boolean dbScoped) {
    DrConfigCreateForm createForm = new DrConfigCreateForm();
    createForm.name = name;
    createForm.sourceUniverseUUID = sourceUniverse.getUniverseUUID();
    createForm.targetUniverseUUID = targetUniverse.getUniverseUUID();
    createForm.dbs = Set.of(namespaceId);
    createForm.bootstrapParams = new RestartBootstrapParams();
    createForm.bootstrapParams.backupRequestParams = backupRequestParams;

    if (dbScoped != null) {
      createForm.dbScoped = dbScoped;
    }

    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList = new ArrayList<>();
    MasterDdlOuterClass.ListTablesResponsePB.TableInfo.Builder table1TableInfoBuilder =
        MasterDdlOuterClass.ListTablesResponsePB.TableInfo.newBuilder();
    table1TableInfoBuilder.setTableType(TableType.PGSQL_TABLE_TYPE);
    table1TableInfoBuilder.setId(ByteString.copyFromUtf8(UUID.randomUUID().toString()));
    table1TableInfoBuilder.setName("table_1");
    table1TableInfoBuilder.setRelationType(RelationType.USER_TABLE_RELATION);
    table1TableInfoBuilder.setNamespace(
        MasterTypes.NamespaceIdentifierPB.newBuilder()
            .setName("db1")
            .setId(ByteString.copyFromUtf8(namespaceId))
            .build());
    tableInfoList.add(table1TableInfoBuilder.build());

    try {
      ListTablesResponse mockListTablesResponse = mock(ListTablesResponse.class);
      when(mockListTablesResponse.getTableInfoList()).thenReturn(tableInfoList);
      when(mockYBClient.getTablesList(nullable(String.class), eq(false), nullable(String.class)))
          .thenReturn(mockListTablesResponse);

      GetMasterClusterConfigResponse fakeClusterConfigResponse =
          new GetMasterClusterConfigResponse(
              0, "", CatalogEntityInfo.SysClusterConfigEntryPB.getDefaultInstance(), null);
      when(mockYBClient.getMasterClusterConfig()).thenReturn(fakeClusterConfigResponse);
    } catch (Exception e) {
      e.printStackTrace();
    }

    return createForm;
  }

  @Override
  protected Application provideApplication() {
    return new GuiceApplicationBuilder()
        .configure(testDatabase())
        .overrides(bind(Commissioner.class).toInstance(mockCommissioner))
        .overrides(bind(MetricQueryHelper.class).toInstance(mockMetricQueryHelper))
        .overrides(bind(BackupHelper.class).toInstance(mockBackupHelper))
        .overrides(bind(CustomerConfigService.class).toInstance(mockCustomerConfigService))
        .overrides(bind(AutoFlagUtil.class).toInstance(mockAutoFlagUtil))
        .overrides(bind(XClusterUniverseService.class).toInstance(mockXClusterUniverseService))
        .overrides(bind(YBClientService.class).toInstance(mockYBClientService))
        .overrides(bind(XClusterScheduler.class).toInstance(mockXClusterScheduler))
        .overrides(bind(AuditService.class).toInstance(auditService))
        .build();
  }

  @Before
  public void setUp() {
    when(mockYBClientService.getClient(any(), any())).thenReturn(mockYBClient);
    when(mockCustomerConfigService.getOrBadRequest(any(), any())).thenCallRealMethod();
    settableRuntimeConfigFactory = app.injector().instanceOf(SettableRuntimeConfigFactory.class);
    settableRuntimeConfigFactory.globalRuntimeConf().setValue("yb.xcluster.dr.enabled", "true");
    defaultCustomer = ModelFactory.testCustomer();
    Users user = ModelFactory.testUser(defaultCustomer);
    authToken = user.createAuthToken();
    CustomerConfig config = createData(defaultCustomer);
    sourceUniverse = createUniverse("source Universe");
    TestHelper.updateUniverseVersion(sourceUniverse, "2.23.0.0-b394");
    targetUniverse = createUniverse("target Universe");
    TestHelper.updateUniverseVersion(targetUniverse, "2.23.0.0-b394");
    namespaceId = UUID.randomUUID().toString().replace("-", "");

    backupRequestParams = new BootstrapBackupParams();
    backupRequestParams.storageConfigUUID = config.getConfigUUID();
  }

  @Test
  // Runtime config `yb.xcluster.db_scoped.enabled` = true and db scoped parameter is passed in
  // as true for request body.
  public void testCreateDbScopedSuccess() {
    settableRuntimeConfigFactory
        .globalRuntimeConf()
        .setValue("yb.xcluster.db_scoped.enabled", "true");
    DrConfigCreateForm data = createDefaultCreateForm("dbScopedDR", true);
    UUID taskUUID = buildTaskInfo(null, TaskType.CreateDrConfig);
    when(mockCommissioner.submit(any(), any())).thenReturn(taskUUID);
    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            "/api/customers/" + defaultCustomer.getUuid() + "/dr_configs",
            authToken,
            Json.toJson(data));

    assertOk(result);
    List<DrConfig> drConfigs =
        DrConfig.getBetweenUniverses(
            sourceUniverse.getUniverseUUID(), targetUniverse.getUniverseUUID());
    assertEquals(1, drConfigs.size());
    DrConfig drConfig = drConfigs.get(0);
    assertNotNull(drConfig);
    XClusterConfig xClusterConfig = drConfig.getActiveXClusterConfig();
    assertEquals(xClusterConfig.getType(), ConfigType.Db);
  }

  @Test
  // Runtime config `yb.xcluster.db_scoped.enabled` = true with no parameter.
  public void testSetDatabasesSuccess() {
    settableRuntimeConfigFactory
        .globalRuntimeConf()
        .setValue("yb.xcluster.db_scoped.enabled", "true");
    DrConfigCreateForm data = createDefaultCreateForm("dbScopedDR", null);
    UUID taskUUID = buildTaskInfo(null, TaskType.CreateDrConfig);
    when(mockCommissioner.submit(any(), any())).thenReturn(taskUUID);
    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            "/api/customers/" + defaultCustomer.getUuid() + "/dr_configs",
            authToken,
            Json.toJson(data));

    assertOk(result);
    List<DrConfig> drConfigs =
        DrConfig.getBetweenUniverses(
            sourceUniverse.getUniverseUUID(), targetUniverse.getUniverseUUID());
    assertEquals(1, drConfigs.size());
    DrConfig drConfig = drConfigs.get(0);
    assertNotNull(drConfig);
    UUID drConfigId = drConfig.getUuid();
    DrConfigSetDatabasesForm setDatabasesData = new DrConfigSetDatabasesForm();
    setDatabasesData.databases = new HashSet<>(Set.of("db1", "db2"));
    XClusterConfig xClusterConfig = drConfig.getActiveXClusterConfig();
    xClusterConfig.updateStatus(XClusterConfigStatusType.Running);
    drConfig.setState(State.Replicating);
    drConfig.update();

    taskUUID = buildTaskInfo(null, TaskType.EditDrConfig);
    when(mockCommissioner.submit(any(), any())).thenReturn(taskUUID);
    result =
        doRequestWithAuthTokenAndBody(
            "PUT",
            "/api/customers/"
                + defaultCustomer.getUuid()
                + "/dr_configs/"
                + drConfigId
                + "/set_dbs",
            authToken,
            Json.toJson(setDatabasesData));

    assertOk(result);

    // Try adding a database and deleting a database.
    setDatabasesData.databases = new HashSet<>(Set.of("db2", "db3"));
    xClusterConfig = drConfig.getActiveXClusterConfig();
    xClusterConfig.updateStatus(XClusterConfigStatusType.Running);

    taskUUID = buildTaskInfo(null, TaskType.EditDrConfig);
    when(mockCommissioner.submit(any(), any())).thenReturn(taskUUID);
    result =
        doRequestWithAuthTokenAndBody(
            "PUT",
            "/api/customers/"
                + defaultCustomer.getUuid()
                + "/dr_configs/"
                + drConfigId
                + "/set_dbs",
            authToken,
            Json.toJson(setDatabasesData));

    assertOk(result);
  }

  @Test
  // Runtime config `yb.xcluster.db_scoped.enabled` = true with no parameter.
  public void testSetDatabasesFailureNoChange() {
    settableRuntimeConfigFactory
        .globalRuntimeConf()
        .setValue("yb.xcluster.db_scoped.enabled", "true");
    DrConfigCreateForm data = createDefaultCreateForm("dbScopedDR", null);
    UUID taskUUID = buildTaskInfo(null, TaskType.CreateDrConfig);
    when(mockCommissioner.submit(any(), any())).thenReturn(taskUUID);
    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            "/api/customers/" + defaultCustomer.getUuid() + "/dr_configs",
            authToken,
            Json.toJson(data));
    assertOk(result);
    List<DrConfig> drConfigs =
        DrConfig.getBetweenUniverses(
            sourceUniverse.getUniverseUUID(), targetUniverse.getUniverseUUID());
    assertEquals(1, drConfigs.size());
    DrConfig drConfig = drConfigs.get(0);
    assertNotNull(drConfig);
    UUID drConfigId = drConfig.getUuid();
    DrConfigSetDatabasesForm setDatabasesData = new DrConfigSetDatabasesForm();
    setDatabasesData.databases = new HashSet<>(Set.of(namespaceId));
    XClusterConfig xClusterConfig = drConfig.getActiveXClusterConfig();
    xClusterConfig.updateStatus(XClusterConfigStatusType.Running);
    xClusterConfig.updateStatusForNamespace(namespaceId, XClusterNamespaceConfig.Status.Running);
    drConfig.setState(State.Replicating);
    drConfig.update();

    // Trying to add the existing databases.
    Exception exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                doRequestWithAuthTokenAndBody(
                    "PUT",
                    "/api/customers/"
                        + defaultCustomer.getUuid()
                        + "/dr_configs/"
                        + drConfigId
                        + "/set_dbs",
                    authToken,
                    Json.toJson(setDatabasesData)));
    assertThat(
        exception.getMessage(),
        containsString("The list of new databases to add/remove is empty."));
  }

  @Test
  // Runtime config `yb.xcluster.db_scoped.enabled` = true with no parameter.
  public void testSetDatabasesFailureNoDbs() {
    settableRuntimeConfigFactory
        .globalRuntimeConf()
        .setValue("yb.xcluster.db_scoped.enabled", "true");
    DrConfigCreateForm data = createDefaultCreateForm("dbScopedDR", null);
    UUID taskUUID = buildTaskInfo(null, TaskType.CreateDrConfig);
    when(mockCommissioner.submit(any(), any())).thenReturn(taskUUID);
    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            "/api/customers/" + defaultCustomer.getUuid() + "/dr_configs",
            authToken,
            Json.toJson(data));

    assertOk(result);
    List<DrConfig> drConfigs =
        DrConfig.getBetweenUniverses(
            sourceUniverse.getUniverseUUID(), targetUniverse.getUniverseUUID());
    assertEquals(1, drConfigs.size());
    DrConfig drConfig = drConfigs.get(0);
    assertNotNull(drConfig);
    UUID drConfigId = drConfig.getUuid();
    DrConfigSetDatabasesForm setDatabasesData = new DrConfigSetDatabasesForm();
    XClusterConfig xClusterConfig = drConfig.getActiveXClusterConfig();
    xClusterConfig.updateStatus(XClusterConfigStatusType.Running);
    drConfig.setState(State.Replicating);
    drConfig.update();

    // Try giving an empty list.
    setDatabasesData.databases = new HashSet<>();
    xClusterConfig = drConfig.getActiveXClusterConfig();
    xClusterConfig.updateStatus(XClusterConfigStatusType.Running);
    Exception exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                doRequestWithAuthTokenAndBody(
                    "PUT",
                    "/api/customers/"
                        + defaultCustomer.getUuid()
                        + "/dr_configs/"
                        + drConfigId
                        + "/set_dbs",
                    authToken,
                    Json.toJson(setDatabasesData)));
    assertThat(exception.getMessage(), containsString("error.required"));
  }

  @Test
  // Runtime config `yb.xcluster.db_scoped.enabled` is disabled but db scoped parameter is passed in
  // as true for request body.
  public void testCreateDbScopedDisabledFailure() {
    settableRuntimeConfigFactory
        .globalRuntimeConf()
        .setValue("yb.xcluster.db_scoped.enabled", "false");
    DrConfigCreateForm data = createDefaultCreateForm("dbScopedDR", true);
    buildTaskInfo(null, TaskType.CreateDrConfig);

    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthTokenAndBody(
                    "POST",
                    "/api/customers/" + defaultCustomer.getUuid() + "/dr_configs",
                    authToken,
                    Json.toJson(data)));

    assertBadRequest(result, "db scoped disaster recovery configs is disabled");
  }

  private void setupMockGetUniverseReplicationInfo(
      DrConfig drConfig, String sourceNamespace, String targetNamespace) throws Exception {
    GetUniverseReplicationInfoResponse mockResponse =
        new GetUniverseReplicationInfoResponse(
            0,
            "",
            null,
            CommonTypes.XClusterReplicationType.XCLUSTER_YSQL_DB_SCOPED,
            List.of(
                MasterReplicationOuterClass.GetUniverseReplicationInfoResponsePB.DbScopedInfoPB
                    .newBuilder()
                    .setSourceNamespaceId(sourceNamespace)
                    .setTargetNamespaceId(targetNamespace)
                    .build()),
            Collections.emptyList());
    when(mockYBClient.getUniverseReplicationInfo(
            eq(drConfig.getActiveXClusterConfig().getReplicationGroupName())))
        .thenReturn(mockResponse);
  }

  @Test
  public void testDbScopedSwitchover() throws Exception {
    String sourceNamespace = "sourceNamespace";
    DrConfig drConfig =
        DrConfig.create(
            "test",
            sourceUniverse.getUniverseUUID(),
            targetUniverse.getUniverseUUID(),
            new BootstrapBackupParams(),
            new PitrParams(),
            Set.of(sourceNamespace));
    drConfig.setState(State.Replicating);
    drConfig.getActiveXClusterConfig().setStatus(XClusterConfigStatusType.Running);
    drConfig.update();
    UUID taskUUID = buildTaskInfo(null, TaskType.SwitchoverDrConfig);
    when(mockCommissioner.submit(any(), any())).thenReturn(taskUUID);
    String targetNamespace = "targetNamespace";
    setupMockGetUniverseReplicationInfo(drConfig, sourceNamespace, targetNamespace);
    ListTablesResponse mockListTablesResponse = mock(ListTablesResponse.class);
    when(mockListTablesResponse.getTableInfoList()).thenReturn(Collections.emptyList());
    when(mockYBClient.getTablesList(nullable(String.class), eq(false), nullable(String.class)))
        .thenReturn(mockListTablesResponse);

    DrConfigSwitchoverForm form = new DrConfigSwitchoverForm();
    form.primaryUniverseUuid = sourceUniverse.getUniverseUUID();
    form.drReplicaUniverseUuid = targetUniverse.getUniverseUUID();

    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            String.format(
                "/api/customers/%s/dr_configs/%s/switchover",
                defaultCustomer.getUuid(), drConfig.getUuid()),
            authToken,
            Json.toJson(form));

    assertOk(result);
    verify(mockYBClient)
        .getUniverseReplicationInfo(
            eq(drConfig.getActiveXClusterConfig().getReplicationGroupName()));
    ArgumentCaptor<DrConfigTaskParams> paramsArgumentCaptor =
        ArgumentCaptor.forClass(DrConfigTaskParams.class);
    verify(mockCommissioner)
        .submit(eq(TaskType.SwitchoverDrConfig), paramsArgumentCaptor.capture());
    DrConfigTaskParams params = paramsArgumentCaptor.getValue();
    assertEquals(drConfig.getUuid(), params.getDrConfig().getUuid());
    assertEquals(1, params.getDbs().size());
    assertEquals(targetNamespace, params.getDbs().iterator().next());
    assertTrue(params.getNamespaceIdSafetimeEpochUsMap().isEmpty());
    assertTrue(params.getMainTableIndexTablesMap().isEmpty());
    assertTrue(params.getTableInfoList().isEmpty());
    assertTrue(params.getSourceTableIdsWithNoTableOnTargetUniverse().isEmpty());

    XClusterConfig oldXClusterConfig = params.getOldXClusterConfig();
    assertEquals(drConfig.getActiveXClusterConfig().getUuid(), oldXClusterConfig.getUuid());

    XClusterConfig switchoverConfig = params.getXClusterConfig();
    assertEquals(oldXClusterConfig.getType(), switchoverConfig.getType());
    drConfig.refresh();
    assertEquals(2, drConfig.getXClusterConfigs().size());
    assertEquals(switchoverConfig.getUuid(), drConfig.getFailoverXClusterConfig().getUuid());
    assertEquals(1, switchoverConfig.getNamespaces().size());
    XClusterNamespaceConfig namespaceConfig = switchoverConfig.getNamespaces().iterator().next();
    assertEquals(targetNamespace, namespaceConfig.getSourceNamespaceId());
    assertEquals(XClusterNamespaceConfig.Status.Validated, namespaceConfig.getStatus());
    assertTrue(switchoverConfig.getTableDetails().isEmpty());
  }

  @Test
  public void testDbScopedFailoverFailsWithSafetimeMissing() throws Exception {
    String sourceNamespace = "sourceNamespace";
    DrConfig drConfig =
        DrConfig.create(
            "test",
            sourceUniverse.getUniverseUUID(),
            targetUniverse.getUniverseUUID(),
            new BootstrapBackupParams(),
            new PitrParams(),
            Set.of(sourceNamespace));
    drConfig.setState(State.Replicating);
    drConfig.getActiveXClusterConfig().setStatus(XClusterConfigStatusType.Running);
    drConfig.update();

    String targetNamespace = "targetNamespace";
    setupMockGetUniverseReplicationInfo(drConfig, sourceNamespace, targetNamespace);

    DrConfigFailoverForm form = new DrConfigFailoverForm();
    form.primaryUniverseUuid = sourceUniverse.getUniverseUUID();
    form.drReplicaUniverseUuid = targetUniverse.getUniverseUUID();
    form.namespaceIdSafetimeEpochUsMap = new HashMap<>();

    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthTokenAndBody(
                    "POST",
                    String.format(
                        "/api/customers/%s/dr_configs/%s/failover",
                        defaultCustomer.getUuid(), drConfig.getUuid()),
                    authToken,
                    Json.toJson(form)));

    assertBadRequest(result, "Safetime must be specified for all the databases");
    assertEquals(1, drConfig.getXClusterConfigs().size());
  }

  @Test
  public void testDbScopedFailover() throws Exception {
    String sourceNamespace = "sourceNamespace";
    DrConfig drConfig =
        DrConfig.create(
            "test",
            sourceUniverse.getUniverseUUID(),
            targetUniverse.getUniverseUUID(),
            new BootstrapBackupParams(),
            new PitrParams(),
            Set.of(sourceNamespace));
    drConfig.setState(State.Replicating);
    drConfig.getActiveXClusterConfig().setStatus(XClusterConfigStatusType.Running);
    drConfig.update();
    UUID taskUUID = buildTaskInfo(null, TaskType.FailoverDrConfig);
    when(mockCommissioner.submit(any(), any())).thenReturn(taskUUID);

    String targetNamespace = "targetNamespace";
    setupMockGetUniverseReplicationInfo(drConfig, sourceNamespace, targetNamespace);

    DrConfigFailoverForm form = new DrConfigFailoverForm();
    form.primaryUniverseUuid = sourceUniverse.getUniverseUUID();
    form.drReplicaUniverseUuid = targetUniverse.getUniverseUUID();
    form.namespaceIdSafetimeEpochUsMap = Map.of(targetNamespace, 100L);

    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            String.format(
                "/api/customers/%s/dr_configs/%s/failover",
                defaultCustomer.getUuid(), drConfig.getUuid()),
            authToken,
            Json.toJson(form));

    assertOk(result);
    verify(mockYBClient)
        .getUniverseReplicationInfo(
            eq(drConfig.getActiveXClusterConfig().getReplicationGroupName()));

    ArgumentCaptor<DrConfigTaskParams> paramsArgumentCaptor =
        ArgumentCaptor.forClass(DrConfigTaskParams.class);
    verify(mockCommissioner).submit(eq(TaskType.FailoverDrConfig), paramsArgumentCaptor.capture());

    DrConfigTaskParams params = paramsArgumentCaptor.getValue();
    assertEquals(drConfig.getUuid(), params.getDrConfig().getUuid());
    assertEquals(1, params.getDbs().size());
    assertEquals(targetNamespace, params.getDbs().iterator().next());
    assertEquals(form.namespaceIdSafetimeEpochUsMap, params.getNamespaceIdSafetimeEpochUsMap());
    assertTrue(params.getMainTableIndexTablesMap().isEmpty());
    assertTrue(params.getTableInfoList().isEmpty());
    assertTrue(params.getSourceTableIdsWithNoTableOnTargetUniverse().isEmpty());

    XClusterConfig oldXClusterConfig = params.getOldXClusterConfig();
    assertEquals(drConfig.getActiveXClusterConfig().getUuid(), oldXClusterConfig.getUuid());

    XClusterConfig failoverConfig = params.getXClusterConfig();
    assertEquals(oldXClusterConfig.getType(), failoverConfig.getType());
    drConfig.refresh();
    assertEquals(2, drConfig.getXClusterConfigs().size());
    assertEquals(failoverConfig.getUuid(), drConfig.getFailoverXClusterConfig().getUuid());
    assertEquals(1, failoverConfig.getNamespaces().size());
    XClusterNamespaceConfig namespaceConfig = failoverConfig.getNamespaces().iterator().next();
    assertEquals(targetNamespace, namespaceConfig.getSourceNamespaceId());
    assertEquals(XClusterNamespaceConfig.Status.Validated, namespaceConfig.getStatus());
    assertTrue(failoverConfig.getTableDetails().isEmpty());
  }

  @Test
  public void testDbScopedRepair() {
    String sourceNamespace = "sourceNamespace";
    DrConfig drConfig =
        DrConfig.create(
            "test",
            sourceUniverse.getUniverseUUID(),
            targetUniverse.getUniverseUUID(),
            new BootstrapBackupParams(),
            new PitrParams(),
            Set.of(sourceNamespace));
    drConfig.setState(State.Halted);
    drConfig.getActiveXClusterConfig().setStatus(XClusterConfigStatusType.Initialized);
    drConfig.update();
    UUID taskUUID = buildTaskInfo(null, TaskType.RestartDrConfig);
    when(mockCommissioner.submit(any(), any())).thenReturn(taskUUID);

    DrConfigRestartForm form = new DrConfigRestartForm();
    form.dbs = Set.of(sourceNamespace);

    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            String.format(
                "/api/customers/%s/dr_configs/%s/restart",
                defaultCustomer.getUuid(), drConfig.getUuid()),
            authToken,
            Json.toJson(form));

    assertOk(result);
    ArgumentCaptor<XClusterConfigTaskParams> paramsArgumentCaptor =
        ArgumentCaptor.forClass(XClusterConfigTaskParams.class);
    verify(mockCommissioner).submit(eq(TaskType.RestartDrConfig), paramsArgumentCaptor.capture());
    XClusterConfigTaskParams params = paramsArgumentCaptor.getValue();
    assertNotNull(params.getPitrParams());
    assertEquals(drConfig.getActiveXClusterConfig().getDbIds(), params.getDbs());
    assertTrue(params.isForceBootstrap());
  }

  @Test
  public void testDbScopedReplicaReplacement() {
    Universe newReplica = createUniverse("new replication target");
    DrConfig drConfig =
        spy(
            DrConfig.create(
                "test",
                sourceUniverse.getUniverseUUID(),
                targetUniverse.getUniverseUUID(),
                new BootstrapBackupParams(),
                new PitrParams(),
                Set.of("sourceNamespace")));
    drConfig.setState(State.Replicating);
    drConfig.update();
    UUID taskUUID = buildTaskInfo(null, TaskType.EditDrConfig);
    when(mockCommissioner.submit(any(), any())).thenReturn(taskUUID);

    DrConfigReplaceReplicaForm form = new DrConfigReplaceReplicaForm();
    form.primaryUniverseUuid = sourceUniverse.getUniverseUUID();
    form.drReplicaUniverseUuid = newReplica.getUniverseUUID();

    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            String.format(
                "/api/customers/%s/dr_configs/%s/replace_replica",
                defaultCustomer.getUuid(), drConfig.getUuid()),
            authToken,
            Json.toJson(form));

    assertOk(result);
    ArgumentCaptor<XClusterConfigTaskParams> paramsArgumentCaptor =
        ArgumentCaptor.forClass(XClusterConfigTaskParams.class);
    verify(mockCommissioner).submit(eq(TaskType.EditDrConfig), paramsArgumentCaptor.capture());
    XClusterConfigTaskParams params = paramsArgumentCaptor.getValue();
    assertEquals(drConfig.getActiveXClusterConfig().getDbIds(), params.getDbs());
    assertNotNull(params.getPitrParams());

    drConfig.refresh();
    XClusterConfig newConfig = drConfig.getFailoverXClusterConfig();
    assertNotNull(newConfig);
    assertEquals(newConfig.getDbIds(), drConfig.getActiveXClusterConfig().getDbIds());
    assertTrue(newConfig.getTableIds().isEmpty());
  }

  private void testToggleState(String operation) {
    DrConfig drConfig =
        DrConfig.create(
            "test",
            sourceUniverse.getUniverseUUID(),
            targetUniverse.getUniverseUUID(),
            new BootstrapBackupParams(),
            new PitrParams(),
            Set.of("sourceNamespace"));
    drConfig.setState(State.Replicating);
    drConfig.getActiveXClusterConfig().setStatus(XClusterConfigStatusType.Running);
    drConfig.update();
    UUID taskUUID = buildTaskInfo(null, TaskType.EditXClusterConfig);
    when(mockCommissioner.submit(any(), any())).thenReturn(taskUUID);

    Result result =
        doRequestWithAuthToken(
            "POST",
            String.format(
                "/api/customers/%s/dr_configs/%s/%s",
                defaultCustomer.getUuid(), drConfig.getUuid(), operation),
            authToken);

    assertOk(result);
    ArgumentCaptor<XClusterConfigTaskParams> paramsArgumentCaptor =
        ArgumentCaptor.forClass(XClusterConfigTaskParams.class);
    verify(mockCommissioner)
        .submit(eq(TaskType.EditXClusterConfig), paramsArgumentCaptor.capture());
    XClusterConfigTaskParams params = paramsArgumentCaptor.getValue();
    String status = params.getEditFormData().status;
    ArgumentCaptor<Audit.ActionType> auditActionTypeCaptor =
        ArgumentCaptor.forClass(Audit.ActionType.class);
    verify(auditService)
        .createAuditEntryWithReqBody(
            any(Http.Request.class),
            eq(Audit.TargetType.DrConfig),
            eq(drConfig.getUuid().toString()),
            auditActionTypeCaptor.capture(),
            any(),
            eq(taskUUID));
    Audit.ActionType auditActionType = auditActionTypeCaptor.getValue();

    if (operation.equals("pause")) {
      assertEquals("Paused", status);
      assertEquals(Audit.ActionType.Pause, auditActionType);
    } else {
      assertEquals("Running", status);
      assertEquals(Audit.ActionType.Resume, auditActionType);
    }
  }

  @Test
  public void testPause() {
    testToggleState("pause");
  }

  @Test
  public void testResume() {
    testToggleState("resume");
  }
}
