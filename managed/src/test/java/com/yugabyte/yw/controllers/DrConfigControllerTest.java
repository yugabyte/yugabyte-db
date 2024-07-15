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
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.XClusterScheduler;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformGuiceApplicationBaseTest;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.backuprestore.BackupHelper;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.gflags.AutoFlagUtil;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.DrConfigCreateForm;
import com.yugabyte.yw.forms.DrConfigSetDatabasesForm;
import com.yugabyte.yw.forms.DrConfigSwitchoverForm;
import com.yugabyte.yw.forms.DrConfigTaskParams;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData.BootstrapParams.BootstrapBackupParams;
import com.yugabyte.yw.forms.XClusterConfigRestartFormData.RestartBootstrapParams;
import com.yugabyte.yw.metrics.MetricQueryHelper;
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
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.CommonTypes;
import org.yb.client.GetUniverseReplicationInfoResponse;
import org.yb.client.ListTablesResponse;
import org.yb.client.YBClient;
import org.yb.master.MasterReplicationOuterClass;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
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

  private Universe sourceUniverse;
  private Universe targetUniverse;
  private Customer defaultCustomer;
  private String authToken;
  private BootstrapBackupParams backupRequestParams;
  private SettableRuntimeConfigFactory settableRuntimeConfigFactory;

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
    createForm.dbs = Set.of("db1");
    createForm.bootstrapParams = new RestartBootstrapParams();
    createForm.bootstrapParams.backupRequestParams = backupRequestParams;

    if (dbScoped != null) {
      createForm.dbScoped = dbScoped;
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
    targetUniverse = createUniverse("target Universe");

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
    setDatabasesData.databases = new HashSet<>(Set.of("db1"));
    XClusterConfig xClusterConfig = drConfig.getActiveXClusterConfig();
    xClusterConfig.updateStatus(XClusterConfigStatusType.Running);
    xClusterConfig.updateStatusForNamespace("db1", XClusterNamespaceConfig.Status.Running);

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

  @Test
  public void testDbScopedSwitchover() throws Exception {
    String sourceNamespace = "sourceNamespace";
    DrConfig drConfig =
        DrConfig.create(
            "test",
            sourceUniverse.getUniverseUUID(),
            targetUniverse.getUniverseUUID(),
            new BootstrapBackupParams(),
            Set.of(sourceNamespace));
    drConfig.getActiveXClusterConfig().setStatus(XClusterConfigStatusType.Running);
    drConfig.getActiveXClusterConfig().update();
    UUID taskUUID = buildTaskInfo(null, TaskType.SwitchoverDrConfig);
    when(mockCommissioner.submit(any(), any())).thenReturn(taskUUID);
    String targetNamespace = "targetNamespace";
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
}
